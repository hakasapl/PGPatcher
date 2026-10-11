# PGLite design review

Status: review of the proposed design, before any code. Section 9 lists the decisions that have to be made before
implementation starts. Everything marked *verified* was read in the code of this repository (branch `pglite`, commit
fd2fd4c), in the Community Shaders (CS) sources on GitHub, or in the IDA database of `SkyrimSE_1.7.99.exe`.
Everything marked *to verify* is my understanding of the engine that still has to be confirmed in the binary.

## 1. Summary

The proposal is sound in what it wants (1:1 behavior, shared code, ImGui front end, no CS material classes) but
the proposed mechanism for sharing code, a wrapper over NIF shapes with a nifly back end offline and a CommonLib back
end at runtime, is the wrong seam. Two facts decide it:

1. CS decides that a shape is PBR inside its hook on `BSLightingShaderProperty::LoadBinary`, that is while the
   engine is still parsing the NIF stream, before the texture set block is even linked. Anything that patches the
   loaded scene graph afterwards is too late for PBR, and "set the flags before CS reads them" cannot be done from
   loaded objects at all.
2. The patchers do not only write material fields. They add and remove vertex colors (with per-vertex storage),
   rewrite vertex colors in HSL, recompute normals and tangents, delete shapes, add and delete blocks, grow texture
   sets to nine slots, and read extra data blocks. A CommonLib back end has to re-implement every one of these on
   GPU-resident engine geometry, with no way to prove it matches.

The recommendation (section 2) is to keep the entire nifly patching stack as it is and to run it at the one place
where the engine loads a NIF file: the engine's `BSStream::Load(path, stream)` routine. PGLite reads the file bytes
the engine is about to parse, patches them in memory with the unchanged PGLib code, and hands the engine a memory
stream of the result. The engine, and CS inside the engine, then see a pre-patched NIF exactly as if the offline
patcher had written it. This satisfies "1:1" by construction, satisfies "before CS reads them" trivially, needs no
material or flag mapping, and reduces the engine-specific surface of PGLite to one hook plus the environment
(file map, plugin records, texture classification, settings, GUI).

The rest of this document lists the behaviors the design does not mention yet (section 3), the interactions with
other runtime tools (section 4), the GUI and settings review (section 5), the build and refactoring plan (section
6), how to prove equivalence (section 7), milestones (section 8) and the open decisions (section 9).

## 2. The patch point

### 2.1 What CS actually reads, and when (verified in CS sources, `dev` branch)

TruePBR installs its hooks in `TruePBR::PostPostLoad()`:

| Hook | Mechanism | What it does |
|---|---|---|
| `BSLightingShaderProperty` vfunc 0x18 (`LoadBinary`) | `write_vfunc` | After the original ran: `if (property->flags.any(kMenuScreen))` it replaces the material with `BSLightingShaderMaterialPBR::Make()` and maps the remaining flags to PBR flags. `kMenuScreen` is the engine's loaded form of the NIF flag `SLSF2_UNUSED01` that the TruePBR patcher sets. |
| `BSLightingShaderProperty` vfunc 0x2A (`GetRenderPasses`) | `write_vfunc` | Rewrites pass techniques for PBR materials. |
| `BSLightingShader` vfunc 0x4/0x6 (`SetupMaterial`/`SetupGeometry`) | `write_vfunc` | Binds PBR textures and constants. |
| `BGSTextureSet::ToShaderTextureSet`, `BSLightingShaderProperty::OnLoadTextureSet` | detours | Track the texture set being applied; PBR alternate textures are recognized by TXST editor ID through `Data\PBRTextureSets\*.json`. |
| `TESObjectSTAT` vfunc 0x4A (`Clone3D`) | `write_vfunc` | PBR material objects (`Data\PBRMaterialObjects`). |

So the PBR decision is made per shader property block during `NiStream` parsing. At that moment the property's own
fields (flags, type, floats) have been read but the `BSShaderTextureSet` it references is only a link ID, not yet an
object. There is no point in the loaded scene graph at which flags can be set "before CS reads them"; the only way
to be earlier than CS is to be earlier than the parser, that is to change the bytes.

Complex material and parallax are different: CS decides those in the pixel shader (`Lighting.hlsl`), by sampling the
environment mask's alpha (`mipSample.w < 1.0 - 4/255` at mip 15 marks a complex material; the full resolution alpha
between the two epsilons enables parallax), and by the material class (`ENVMAP`, `PARALLAX` techniques). For these
a scene graph edit before rendering would work, but only if the material class is swapped correctly, which is the
expensive part (see 2.3).

### 2.2 Where the engine loads NIFs (verified in the 1.7.99 database)

| Function (1.7.99 image) | Role |
|---|---|
| `BSModelDB::Demand` 0x140FC9060, `sub_140FC8D80` | Prefixes `meshes\`, looks the path up in the model entry database, queues or loads it. |
| `sub_140FC8480` | The model loader: constructs a `BSResourceNiBinaryStream` for the path, a `BSStream` (the game's `NiStream` subclass, vtable 0x1417EEDC8), calls `BSStream::Load(path, stream)`, falls back to `Meshes\Marker_Error.NIF` on failure, builds a root `NiNode` when the file has several top objects, then calls `BSModelProcessor::PostCreate` (`TESModelDB::TESProcessor`, vtable 0x1417F75F0) when `args.postProcess` is set. |
| `BSStream::Load` 0x140FD12D0 `(this, const char* path, NiBinaryStream* stream)` | Copies the path into `NiStream::inputFilePath` (offset 0x108, the field CS copies into its PBR material), opens a `BSResourceNiBinaryStream` itself when none is given, then calls `Load1`. Nine call sites: the model loader (two, normal and error fallback), the `.bto` distant object block loader (`sub_1405048A0`, `BGSDistantObjectBlock`), a generic path loader (`sub_1401970B0`, used by sky and similar hard coded NIFs) and five more. |
| Load helper 0x140EE40A0 (the `NiStream::Load1` equivalent `BSStream::Load` calls directly) | Checks `stream->good()`, stores the stream in `NiStream::iStr` (offset 0x298), calls vfunc 15 (`LoadStream`, the actual parser), clears `iStr`. |
| Facegen heads | `sub_1403C2E00` formats `Meshes\Actors\Character\FaceGenData\FaceGeom\%s\%08X.NIF` and `sub_1403C2EC0` demands it through the same model database, so heads take the same path. |
| `NiMemStream` (vtable 0x141B4AEE8, ctor `sub_14156A6A0(this, buffer, size)`) and `NiStream::Load2(buffer, len)` | The engine already supports loading a NIF from memory. |

Every NIF file the game opens, whatever the record type and whether it is loose or in a BSA, goes through
`BSStream::Load` with the path and an open stream. That is the choke point. Three ways to hook it, in order of
preference:

1. A vtable hook on `BSStream::LoadStream` (slot 15 of the `BSStream` vtable). When it runs, `iStr` and
   `inputFilePath` are set. The thunk reads the whole file from `iStr`, patches it, swaps `iStr` for a memory stream
   over the result, calls the original and restores `iStr`. Vtable hooks chain with other plugins and need no
   call site offsets, only the vtable address, which the Address Library provides per version.
2. A `write_thunk_call` at the two calls to `BSStream::Load` in the model loader (version specific offsets, three
   game versions).
3. An inline detour of `BSStream::Load` itself.

*To verify in the binary:* that `BSStream` does not override `LoadStream` with something that bypasses `iStr`, the
Address Library IDs of the vtable and functions for 1.5.97, 1.6.1170 and 1.7.x, and the exact memory stream class
layout (I recommend our own `NiBinaryStream` subclass over the engine's `NiMemStream`, so no engine allocator is
involved; CommonLib declares the base class with its virtual layout and the two read and write function pointers).

### 2.3 What the proposed wrapper would have to cover (verified, inventory of PGLib)

The patchers, through `PGNIFUtil` and `PGMaterialState`, use these distinct nifly operations:

- Shader state: type get and set (default, parallax, envmap, multilayer parallax), 25 individual flag bits set or
  cleared across SLSF1 and SLSF2, 14 float fields, the vector and color fields, the shader side vertex color flag,
  the complete 28 field table of `PGMaterialState` for stock restore, and the texture set block (presence, block
  ID, slot count, nine slots).
- Shape and geometry state: shape enumeration in plugin 3D index order (counting `NiParticleSystem` blocks too),
  block type names, skinning, alpha property, vertex color presence, vertex color bytes, triangles, UVs and
  positions, extra data blocks on shape and shader (`PG_IGNORE`, `PG_STOCK`), whole tree scans for
  `BSBehaviorGraphExtraData`.
- Geometry writes: add or remove the vertex color stream (`NiShape::SetVertexColors`, which resizes per-vertex
  storage), rewrite vertex colors in HSL (`vertex_color_lum_mult`, `vertex_color_sat_mult`), recompute normals and
  tangents (`smooth_angle`).
- Structural edits: delete a shape (`delete`), add a `BSShaderTextureSet` block and repoint the shader (texture set
  copy on write), add and delete `NiStringExtraData` blocks, grow a texture set to nine slots.

On the engine side each of those is a separate problem: the engine's texture set has eight slots in a different
order (its `kEnvironmentMask`, `kGlowMap`, `kHeight`, `kEnvironment` order is a permutation of the NIF order), the
64-bit engine flag word is not a positional copy of SLSF1/SLSF2, a shader type change is a material class swap
(`CreateMaterial`, copy members, `SetMaterial`) on materials that the engine pools between instances, slot writes
must rebind `NiSourceTexture` objects, vertex streams are already uploaded, and render passes must be rebuilt.
None of this can be validated against the offline output except by eye. The byte patch has none of it.

### 2.4 Cost of the byte patch

- CPU: one nifly load, patch and save per NIF file load, on the engine's resource loader threads (model loads are
  queued through `BSResource::EntryDB`; synchronous demands from the main thread exist, for example equipping an
  item, and those get longer by the patch time). Offline throughput today is roughly 600 meshes per second on 16
  threads including BSA extraction and file writes, so a few to a few tens of milliseconds per mesh single threaded;
  the in-memory patch alone is less. This has to be measured in the first in-game milestone.
- Mitigation if needed: an in-session LRU cache of patched bytes, and optionally a disk cache
  (`Data\SKSE\Plugins\PGLite\cache`) keyed by file identity and settings fingerprint. The disk cache is the offline
  output built lazily; with it the steady state cost is reading a loose file.
- Memory: transient per load; nothing is retained unless caching is enabled.
- Block order: the offline permutation tracker sorts blocks before saving (`PGMeshPermutationTracker.cpp:200`)
  and reports the resulting 3D index changes to the plugin. At runtime nothing can fix record indices after the
  fact, so PGLite must save with `sortBlocks = false` (as `saveMeshes` already does for the in-memory save at
  `PGMeshPermutationTracker.cpp:251`) and keep the original block order. Shape deletion still shifts indices; see
  3.4.
- Failure policy: any exception or nifly failure in the hook passes the original bytes through. The patch must
  never take the game down; `PGNIFUtil::loadNIFFromBytes` already wraps nifly in SEH for this reason.

### 2.5 What this means for "share as much code as possible"

With the byte patch, the shared part is the whole mesh pipeline: `processNIF`, `processNIFShape`, `matches`,
`sortMatches`, every patcher, `PGNIFUtil`, `PGMaterialState`, `PGModManager`, `BethesdaDirectory`, the texture
classification, `PGD3D` for complex material detection, the TruePBR config loader, the Logger and the settings
model. Any future PGLib change (for example the pending alpha property and copy-on-write work on
`featnialphapatching`) is picked up by PGLite without a second implementation. The code that is genuinely
different between the two programs is exactly what the design already calls out: plugin record access (Mutagen vs
`TESDataHandler`), output (files vs memory), and the front end. The seams go there, not into the NIF layer.

## 3. Behavioral differences the design does not list yet

Numbered for reference. "Phase" says where I would put the work.

1. **Alternate textures (TXST) are the largest gap.** Offline, a record's alternate textures replace the shape's
   texture set as the patch input (`PGPatcher.cpp:773-779`), the patched paths are written to a *new* TXST record
   in `PGPatcher.esp` while the NIF keeps the vanilla texture set and gets only the patched shader flags
   (`PGPatcher.cpp:886-892`, `PGMutagen.cs:1142-1255`), and distinct per-use results become duplicate meshes under
   `_pgpatcher_dups\N` with the record's model path retargeted. At runtime the engine applies the record's TXST on
   top of whatever template was loaded, so a template patched for the base textures plus an unpatched TXST is an
   inconsistent shape (parallax flag without a height map, PBR flag without RMAOS). Proposal: phase 1 does not
   shader-patch a shape that has an alternate texture in any use of the mesh (logged); phase 2 reproduces the plugin
   step in memory: create `BGSTextureSet` forms with the patched paths, repoint `alternateTextures[i].textureSet`,
   and serve divergent uses from virtual model paths (set the record's model string to `path|pgN`; the hook
   recognizes the suffix and supplies the variant bytes, the engine caches it separately). That is the same
   algorithm as `PGMutagen.SetModelUses`, applied to live forms.
2. **Per-use divergence from record filters.** A use is skipped when its record type is not in
   `allowedmodelrecordtypes` or the record carries header flag bit 24 (0x01000000) or its mod has meshes ignored
   (`PGPatcher.cpp:493-575`); offline the skipped use keeps the original mesh and the other uses get duplicates. One
   template cannot express both. Proposal: phase 1 patches when any non-skipped use exists and logs the count of
   skipped uses that share the mesh; phase 2 uses the virtual variant mechanism of item 1.
3. **Two GUI patchers cannot run at runtime because they generate textures:** the Parallax to Complex Material
   transform (`shadertransforms.parallaxtocm`, generates `_m` from `_p` through `PatcherTextureHookConvertToCM`) and
   `FixSSS` (`postpatcher.fixsss`, generates `_s` from the diffuse). The design excludes "handler patchers" and
   "pgtools-only patchers"; these two are neither, they are GUI checkboxes. They must be listed as excluded, and
   the consequence of the first one spelled out: a shape whose parallax match fails `canApply` and would have been
   upgraded to CM offline falls through to the next match at runtime. A later phase could serve generated textures
   through a virtual texture path with the same hook idea, but not in v1.
4. **PBR `delete` entries shift 3D indices.** Deleting a shape renumbers the shapes after it; offline the plugin's
   alternate texture indices are corrected (`idxCorrections`). At runtime a record whose alternate texture index
   points past a deleted shape lands on the wrong shape. Phase 1: skip `delete` entries for meshes that have
   alternate texture uses and log; phase 2: update the live `TESModelTextureSwap` indices together with item 1.
5. **Suffix-only texture classification is a real difference, not a free one.** Offline, textures used by meshes
   are classified by a vote of the shapes that use them (slot and type by shader type and flags,
   `PGDirectory.cpp:395-597`); the suffix table is only the fallback for unused textures. A texture whose name
   contradicts its use (an `_m` used in the glow slot, a normal map without `_n`, a `_p` under `textures\pbr`
   used by a non-PBR shader) is classified differently, which changes which matches exist for every mesh that
   references its base name. Accept it as the design says, but document it, and consider an optional bridge: the
   offline patcher can export its final classification (path, slot, type, attributes) and PGLite loads that file
   when present, which makes the two programs agree for users who run both.
6. **Complex material detection needs the texture pixels.** `PGD3D::checkIfCM` decodes the environment mask on the
   GPU and counts channel values, with a power-of-two size requirement and an alpha format allow list
   (`PGD3D.hpp:129-147`). The runtime needs the same result. Options: reuse `PGD3D` with its own D3D11 device and
   the compute shader compiled into the binary (identical results, GPU contention negligible), or decode on the CPU
   with DirectXTex (different BC decoder, results identical in practice but not provably). Classification of all
   environment masks of a large load order takes seconds; it should run in the background from `kDataLoaded`, be
   cached on disk by file identity like `PGRunCache` does, and the NIF hook should wait for it rather than patch
   with an incomplete map (3.14).
7. **Aspect ratio checks** (`checkIfAspectRatioMatches`) read DDS headers of candidate textures through
   `BethesdaDirectory::file`. Cheap, but the runtime file map must be able to read BSA members; rsm-bsa stays a
   dependency.
8. **`vanillabsalist`** decides that height maps and environment masks inside the listed archives are not mapped.
   The engine does not tell us which archive a file came from, so PGLite needs its own archive listing (the
   existing `BethesdaDirectory` does this). The design drops this setting from the GUI; it should stay as a hidden
   default in the settings file.
9. **Record state at runtime differs from the plugin files.** Base Object Swapper, Seasons of Skyrim, SPID,
   SkyPatcher and similar tools change records or references in memory. PGLite sees the live state (uses map built
   from `TESDataHandler`), the offline patcher sees the files. This is a desirable, intentional difference and
   should be documented as such.
10. **Weighted `_0`/`_1` validation** (`validateWeightedVariants`) warns offline when the two variants of an armor
    produce different block layouts. There is nothing to act on at runtime; drop it and say so.
11. **Facegen** meshes are patched offline for a dummy use unless `--exclude-facegens` is given, and TruePBR
    disables `nif_filter` for them (`PatcherMeshShaderTruePBR.cpp:340-344`). The runtime must build the same dummy
    use from the path (`PGNIFUtil::isFacegenMesh`), and `exclude facegens` becomes an ini option.
12. **The stock marker** (`PatcherMeshPreStockMarker`) is registered first in the GUI run too
    (`PGPatcher/src/main.cpp:627`). Its restore half matters at runtime (mods shipping pre-patched meshes with
    `PG_STOCK` blocks), its create half only fires for BodySlide ShapeData, which never loads in game. Keep it.
13. **`FixTextureSlotCount`** stays (nifly grows the set in memory; harmless).
14. **Readiness.** The first cell load can happen a few seconds after `kDataLoaded`. If the maps are not ready,
    passing meshes through unpatched would cache unpatched templates for the session. The hook should block the
    loader thread until the environment is ready (longer first loading screen), not pass through.
15. **Offline output detection** (design requirement): offline checks `ParallaxGen_Diff.json` in the data folder
    (`main.cpp:601`); the runtime should check that file (loose or archived) and whether `PGPatcher.esp` is in the
    load order, log one clear line, and keep the GUI available to show the reason.
16. **The dynamic cubemap texture** `textures\cubemaps\dynamic1pxcubemap_black.dds` is deployed into the offline
    output (`main.cpp:156-175`). PGLite's package must ship it, and the CM patcher's `disable dynamic cubemap` flag
    becomes an ini option.
17. **DynDOLOD.** Offline refuses to run with `dyndolod.esp` active (`main.cpp:471-476`) and users regenerate LOD
    after patching. At runtime LOD files (`.bto`) are never NIF model loads and the default block list already skips
    `*\lod\*`, `*\dyndolod\*`, `*_lod*`; both should be kept. TexGen and DynDOLOD read the unpatched files on disk,
    which is fine because object LOD does not use parallax or complex material. The refusal itself is not needed at
    runtime; replace it with nothing.
18. **Mesh permutations, Light Placer forwarding, diff JSON, update cache, zip output, plugin output:** not needed,
    as the design says. The update cache has a runtime analog only if the disk cache of 2.4 is built.

## 4. Interactions with other runtime tools

- **Community Shaders.** PBR: with the byte patch the flag is in the stream and CS converts the material exactly as
  for an offline-patched mesh. CM and parallax: shader side, no interaction. Hooks: CS hooks the input dispatch call
  site (`BSInputDeviceManager::PollInputDevices`, IDs 67315/68617 + 0x7B) with `write_thunk_call` and detours
  `IDXGISwapChain::Present` on the live swap chain; both chain correctly when PGLite stores and calls the previous
  target, which the CommonLib helpers do. CS recognizes PBR alternate textures only through its own
  `Data\PBRTextureSets\*.json` registry (by TXST editor ID). Nothing in the repository writes that registry today
  (`pbrtexturesets` only appears in the output cleanup list, `PGPatcher.cpp:306`), and at runtime it could not be
  injected into CS anyway, which is another reason phase 2 bakes alternate textures into mesh variants instead of
  relying on TXST swaps.
- **ENB.** Parallax and complex material are evaluated in ENB's shaders from the material class and textures, the
  same inputs an offline-patched mesh provides. No interaction expected.
- **Auto Parallax (doodlum).** Does the same job at runtime by editing materials. Both running would fight; detect
  `AutoParallax.dll` in `SKSE\Plugins` and disable PGLite with a log line, or at least warn in the GUI.
- **Base Object Swapper, Seasons, SPID, SkyPatcher, Synthesis-style runtime patchers.** See 3.9; PGLite sees the
  swapped records because it keys on the model path at load time and builds uses from live forms.
- **RaceMenu / NiOverride.** Replaces textures on actors after load, on offline-patched meshes just the same. No new
  interaction.
- **Grass mods (NGIO, Grass Cache).** Grass models are rejected by the parallax and CM `canApply` through the GRAS
  record type, same as offline; the runtime uses map must carry record types for that.
- **Other SKSE plugins hooking the model loader.** A vtable hook on `BSStream` composes; two inline detours on the
  same function would not. Prefer the vtable hook.
- **Mod managers.** Under MO2's virtual file system the engine resolves files normally, PGLite reads MO2's
  `modorganizer.ini`, profile `modlist.txt` and the mods folder through the real paths (`PGModManager` already does
  this), and settings written to `Data\SKSE\Plugins\PGLite` land in Overwrite. The MO2 instance can be found without
  typing: `PGModManager::mo2DirFromUSVFS` locates the running MO2 from the injected `usvfs_x64.dll`, and the
  registry key `HKCU\Software\Mod Organizer Team\Mod Organizer\CurrentInstance` names the global instance (to
  verify against the MO2 version in use). Vortex
  needs only `vortex.deployment.json` in the data folder.
- **Game versions.** The database is 1.7.99; the template's notes say 1.7.x needs Address Library format 5 and SKSE
  2.3.x. Every hook target needs its IDs resolved for 1.5.97, 1.6.1170 and 1.7.x, and the CommonLibSSE-NG revision
  must support the 1.7.x library format. VR is not in scope (different layouts). The Address Library is a hard
  dependency to list for users.
- **Saves and persistence.** Nothing is written to saves; patched data lives in the model cache for the session.

## 5. GUI and settings

What the design lists is right. Additions:

- Patcher checkboxes that can exist at runtime: Fix Mesh Lighting, Parallax, Complex Material, True PBR, Restore
  Default Shaders (`disableprepatchedmaterials`), Hair Flow Map. Parallax to CM and Fix SSS are excluded (3.3).
- Keep hidden with defaults from `PGConfig::defaultParams`: `vanillabsalist`, the default mesh block list.
- ini (`Data\SKSE\Plugins\PGLite.ini`, bootstrap only): hotkey, log level, exclude facegens, disable dynamic
  cubemap, cache on/off.
- Settings file: `Data\SKSE\Plugins\PGLite\settings.json` with the offline `params.*` keys and nesting, so a user
  can copy settings between the two programs; `modrules.json` with the offline schema. The JSON keys are serialized
  strings and must not be renamed.
- Mod manager page: type, MO2 instance (auto-detected, confirm button), "use MO2 loose file order", then the mod
  list (mods with shaders or meshes, as `updateStateFromModlist` numbers them), drag to reorder, enable and
  "meshes ignored" per mod, apply writes `modrules.json`.
- Apply semantics: new loads use the new settings immediately (re-register patchers, reload texture rules, drop the
  patched bytes cache). Refreshing meshes already in the scene means releasing and reloading the 3D of every loaded
  reference, which is intrusive (actors included). Recommend "takes effect on the next load; reload the cell to
  see it" for v1 and a "reload loaded 3D" button as a later experiment.
- Status and warnings: scan progress, patched mesh count, whether PGLite is disabled and why (offline output
  present, Auto Parallax present), and the warnings and errors collected through the Logger capture callback,
  deduplicated. Most offline warnings (plugin saving, output folder) do not exist at runtime.
- Language: English only. The ImGui default font has no CJK glyphs and the translation files live in the offline
  application; the `PGTr` rule applies to PGPatcher's wxWidgets strings, not to this module. Revisit if demand
  appears.

## 6. Repository and build

```
PGLite/
  CMakeLists.txt        standalone configure: x64-windows-static, C++23 (CommonLibSSE-NG), /MT
  vcpkg.json            own manifest and baseline
  package/              SKSE/Plugins/PGLite.ini, textures/cubemaps/dynamic1pxcubemap_black.dds
  include/ src/         entry point, hooks, runtime environment, GUI
external/CommonLibSSE-NG   submodule (alandtse, ng branch), add_subdirectory with tests off, SE and AE on, VR off
```

The root configure uses a dynamic CRT and C++20 and builds PGLib as a DLL; one configure has one triplet, so PGLite
is a second configure that compiles the shared PGLib sources a second time as a static library. `buildRelease.ps1`
builds both and produces `PGLite.zip` as the second release artifact; `build.yml` gets a job for it and runs
`check_build_warnings.py` on its log too.

Statically linked into the DLL: CommonLibSSE-NG, nifly (already a static target), spdlog, nlohmann-json, imgui
(dx11 and win32 bindings), simpleini, DirectXTex (CPU side only, for DDS headers and `PGD3D`), rsm-bsa with lz4
and zlib, boost header-only parts (algorithm, crc, gil, iostreams array device, asio). Not linked: wxWidgets, CLI11,
miniz, cpptrace, the .NET host, json-schema-validator (only `PGConfig.hpp` in the GUI uses it).

PGLib changes needed before PGLite can consume it (all behavior preserving, all with unit tests per `CLAUDE.md`):

1. Split `PGPatcher.cpp`: the per-mesh pipeline (`processNIF`, `processNIFShape`, `matches`, `sortMatches`,
   `applyTransformIfNeeded`, `createNIFPatcherObjects`, the patcher registry) moves to a shared unit; the offline
   runner (`patchMeshes`, `patchNIF`, `replayNIF`, output directory, diff JSON, textures) stays offline.
2. `PGPlugin` becomes an interface with two implementations: the existing Mutagen one and a runtime one built from
   `TESDataHandler` (the 35 record types; ADDN is enumerated offline but never patched, the model fields and sub
   model tags of `PGMutagen.GetModelElems`, alternate textures by 3D index, weighted ARMA registration under both
   `_0` and `_1`, header flag 0x01000000, single pass from the STAT material's MATO flags). The header already has
   no Mutagen dependency.
3. `PGDirectory::mapFiles` gets a mode without the mesh reading step (classification from suffix, texture rules,
   vanilla BSA list, CM classification), and the engine's exact plugin list is injectable into
   `BethesdaDirectory::bsaLoadOrder` instead of re-reading `plugins.txt`.
4. The update cache recording calls sprinkled through `PGDirectory`, `PGNIFUtil::texMatch`, `PGModManager` and
   `matches()` go behind a small observer interface that `PGRunCache` implements offline and the runtime leaves
   unset, so `PGRunCache` (which drags in the permutation tracker, texture hooks and `PGPatcher.hpp`) is not
   compiled into the DLL.
5. `PGConfig`'s parameter struct and JSON (de)serialization move to PGLib without the wxWidgets-only parts
   (`ignoredMessagesConfig` keeps `wxString` and stays in the GUI).
6. `PGD3D::initShader` loads compiled bytecode instead of calling `D3DCompileFromFile` on `cshaders\*.hlsl`:
   compile the four shaders at build time with `fxc` and embed them. This removes the `cshaders` folder and the
   `D3DCompiler` dependency from both programs and makes startup faster.
7. `StringUtil` replaces its four `boost::locale::conv` calls with `MultiByteToWideChar`/`WideCharToMultiByte`, so
   boost.locale (and potentially ICU) is not linked into the DLL. `StringUtilTest` covers it.
8. `PGModManager::populateModFileMapMO2` tolerates an empty output directory (it already does; confirm with a test).

PGLite's own code: the SKSE entry (`SKSEPluginLoad`, messaging, ini, logging to the SKSE log folder), the load hook
and memory stream, the runtime environment (`BethesdaGame`/`BethesdaDirectory`/`PGModManager`/`PGD3D` set up from
the running game, offline output detection, readiness latch), the runtime `PGPlugin`, the optional cache, and the
ImGui menu (device and context from `BSGraphics::Renderer`, present and input hooks as CS does them). WebKit style
and the review checklist apply unchanged; `run_clang_tidy.py` needs the PGLite compile commands added.

## 7. Proving 1:1

- A patch trace shared by both programs: for every (mesh, use, shape) the sorted matches, the winner, the patchers
  applied and the resulting slots, type and flags, written as JSON lines when enabled. Diffing the offline trace of
  a load order against the runtime trace of a play session is the equivalence test.
- Byte comparison: for meshes with a single use and no alternate textures, the offline output file and the bytes
  PGLite hands the engine must be identical apart from block order (offline sorts). A debug option dumps the
  runtime bytes to a folder for a canonical compare.
- The existing unit tests keep PGLib honest through the refactor; no new PGLite tests, as requested.

## 8. Milestones

1. PGLib seam work (section 6, items 1 to 8) with tests; offline behavior unchanged, byte-identical regression on
   a known load order.
2. PGLite scaffold: configure, submodule, entry point, logging, ini, offline output detection, the load hook in
   pass-through mode, release packaging and CI job.
3. Runtime environment and first patched meshes: file map and BSA list, uses map from `TESDataHandler`, suffix
   classification and background CM classification with cache, TruePBR configs, mod manager, readiness latch, the
   patch inside the hook. Measure per-load cost; decide on the cache. Deploy to the MO2 test mod folder.
4. ImGui menu with all pages and apply.
5. Phase 2 items: alternate textures and virtual variants, per-use divergence, `delete` index handling.
6. Verification tooling of section 7 and a documented differences list in the README.

## 9. Decisions needed

1. Byte-level patching in the engine's NIF loader instead of a CommonLib scene graph wrapper (section 2). This is
   the one that changes the plan.
2. Phase 1 handling of alternate textures and filtered uses: patch the template for the primary use and log the
   divergence, with the in-memory record patching in phase 2 (3.1, 3.2, 3.4).
3. Classification: suffix only as proposed, with or without the optional import of the offline classification (3.5).
4. CM detection: reuse `PGD3D` with its own device and embedded shaders (my recommendation) or CPU decode (3.6).
5. Apply semantics: next load only, or also reload loaded 3D (section 5).
6. English-only GUI for v1.
7. Supported runtimes: 1.5.97, 1.6.1170 and 1.7.x, no VR.
8. The hook mechanism: `BSStream::LoadStream` vtable hook (recommended) versus call site hooks in the model loader.
