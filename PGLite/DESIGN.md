# PGLite design

Status: decided design as of 2026-10-11, including Hakan's decisions on the roadblocks and differences (no phases,
blocks sorted as offline, weighted pair check kept, no caches or equivalence tooling, in-game notice when disabled,
memory kept low). Section 10 lists the remaining defaults. Facts marked *verified* were read in this repository
(branch `pglite`, commit fd2fd4c), in the Community Shaders (CS) sources on GitHub or in the IDA database of
`SkyrimSE_1.7.99.exe`. Facts marked *to verify* are confirmed in the binary or in game when the code that depends on
them is written.

## 1. What PGLite is

PGLite is an SKSE plugin (one statically linked DLL, CommonLibSSE-NG, Dear ImGui) that produces the same meshes the
offline PGPatcher produces, at the moment the game loads them. It runs the unchanged PGLib patcher stack on the bytes
of every NIF the engine reads, hands the engine the patched bytes, and reproduces the offline plugin step (mesh
duplicates and texture set records) in memory. Nothing is written to the data folder. Any behavior difference that
section 7 does not list is a bug.

## 2. The central decision: patch the stream, not the scene

The specification proposed a wrapper over NIF shapes with a nifly back end offline and a CommonLib back end at
runtime. That wrapper is not built. The patchers keep calling nifly through `PGNIFUtil` and `PGMaterialState` as they
do today, and PGLite applies them to the NIF data in memory before the engine parses it. Three verified facts decide
this:

1. CS decides that a shape is PBR inside its hook on `BSLightingShaderProperty::LoadBinary`, while the engine is
   parsing the stream and before the texture set block is linked. Nothing later re-evaluates the flag. Any edit made
   to loaded objects is therefore invisible to PBR, and "set the flag before CS reads it" can only be done by having
   the flag in the parsed data. The stream patch does exactly that, without any CS class, export or API.
2. The patchers change geometry, not only materials: vanilla parallax adds a vertex color stream, TruePBR entries
   add or remove that stream, rewrite vertex colors in HSL (`vertex_color_lum_mult`, `vertex_color_sat_mult`),
   recompute normals and tangents (`smooth_angle`), delete shapes and add or delete blocks. On loaded objects these
   are GPU-resident buffers with no engine API; on the stream they are the existing nifly calls.
3. Only the stream patch is comparable to the offline output: with blocks sorted the same way, the bytes PGLite
   hands the engine for a mesh are the bytes the offline run writes for the same use.

What remains engine specific is the environment: one load hook, a memory stream, the record data (`TESDataHandler`
instead of Mutagen), the in-memory record edits of the permutation system, settings, logging and the GUI.

## 3. Engine and CS facts the design relies on

### 3.1 NIF loading (verified in the 1.7.99 database)

| Function (1.7.99 image) | Role |
|---|---|
| `BSModelDB::Demand` 0x140FC9060 and `sub_140FC8D80` | Prefix `meshes\`, look the path up in the model entry database, load or queue the load. Every record model, including facegen heads (`sub_1403C2EC0` formats `FaceGeom\%s\%08X.NIF` and demands it), goes through here. |
| Model loader `sub_140FC8480` | Constructs a `BSResourceNiBinaryStream` for the path, a `BSStream` (the game's `NiStream` subclass, vtable 0x1417EEDC8), calls `BSStream::Load(path, stream)` unconditionally, falls back to `Meshes\Marker_Error.NIF` when no top object came out, builds a root `NiNode` for several top objects, runs `BSModelProcessor::PostCreate` when `args.postProcess` is set. |
| `BSStream::Load` 0x140FD12D0 `(this, const char* path, NiBinaryStream* stream)` | Opens a `BSResourceNiBinaryStream` itself when `stream` is null, copies the path into `NiStream::inputFilePath` (offset 0x108, which CS copies into its PBR material), then calls the load helper. Nine call sites: the model loader (two: normal and error fallback), the `.bto` distant object block loader (`sub_1405048A0`), a generic path loader (`sub_1401970B0`, sky and other hard coded NIFs) and five more. |
| Load helper 0x140EE40A0 | `if (stream->good())`: store the stream in `NiStream::iStr` (offset 0x298), call vfunc 15 `LoadStream` (the parser), clear `iStr`. A stream that failed to open never reaches the parser. |
| `NiMemStream` (vtable 0x141B4AEE8, ctor `sub_14156A6A0(this, buffer, size)`), `NiStream::Load2(buffer, len)` | The engine supports parsing from memory. |
| `NiStream` vtable 0x141A47168 | Load1 at +8, Load2 at +16, Load3 at +24, LoadStream at +0x78. |

The engine's 64-bit shader property flag word is a positional copy of the NIF's SLSF1 and SLSF2 words (verified
against CommonLibSSE-NG's `EShaderPropertyFlag8`: `kMenuScreen` is 55, the loaded form of `SLSF2_UNUSED01` at bit
23; `kVertexColors` 37; `kTreeAnim` 61). The stream patch never touches these.

### 3.2 Community Shaders (verified in the `dev` sources)

- TruePBR hooks `BSLightingShaderProperty` vfunc 0x18 (`LoadBinary`): after the original ran, `if
  (property->flags.any(kMenuScreen))` the material is replaced with `BSLightingShaderMaterialPBR::Make()`, the PBR
  flags are derived from the shader flags, `kVertexLighting` is set as the internal marker and legacy bits are
  cleared. Textures load later from the texture set through the PBR material's own `OnLoadTextureSet`.
- Other TruePBR hooks: `GetRenderPasses` (vfunc 0x2A), `BSLightingShader::SetupMaterial/SetupGeometry`,
  `BGSTextureSet::ToShaderTextureSet` and `BSLightingShaderProperty::OnLoadTextureSet` (track the texture set being
  applied), `TESObjectSTAT::Clone3D` (PBR material objects). PBR alternate textures are recognized only through
  `Data\PBRTextureSets\*.json` by TXST editor ID, a registry read at startup.
- Complex material and parallax are decided in the pixel shader: an environment mask whose mip 15 alpha is below
  `1 - 4/255` is a complex material, the material class selects the `ENVMAP` and `PARALLAX` techniques. No vertex
  color requirement in CS.
- CS hooks the input dispatch call site (`BSInputDeviceManager::PollInputDevices`, IDs 67315/68617 + 0x7B) with a
  call-site hook and detours `IDXGISwapChain::Present` on the live swap chain. Both chain when PGLite stores and
  calls the previous target, as every other ImGui-based plugin does.

## 4. Runtime architecture

### 4.1 Startup

At `kDataLoaded`, in this order:

1. Detect the disable conditions (4.9). If any holds, log it, show the in-game notice, keep the GUI with the reason,
   and leave the hook in pass-through mode.
2. Build the uses map from `TESDataHandler` on the main thread (4.6). It is small and fast, and it is what lets the
   hook pass through meshes without uses immediately (main menu and loading screen art).
3. Start the background initialization: `BethesdaGame` and `BethesdaDirectory` for the running game (loose files and
   archives in the engine's archive order, the active plugin list injected from `TESDataHandler`), `findFiles` and
   suffix classification (4.5), TruePBR configs (`loadStatics` over the `pbrnifpatcher` JSONs in file map order),
   the mod manager file map and `updateStateFromModlist` when a mod manager is configured, then the permutation
   plan (4.4) and the record retargets on the main thread.
4. Mark ready. Until then the hook passes through paths without uses and blocks loads of paths with uses on the
   readiness latch, so no patchable mesh is ever cached unpatched for the session. This lengthens the first loading
   screen by the initialization time; it never patches at boot.

### 4.2 The load hook

The hook is on `BSStream::Load` itself: a call-site hook (`write_thunk_call`) at the two calls in the model loader,
with an inline detour of the function as the fallback. It is not the `LoadStream` vtable hook, because virtual
variant paths (4.4) have no file, the engine's own stream fails to open, and the load helper's `good()` check then
never reaches `LoadStream`; the substitution has to happen before that check. The seven other call sites of
`BSStream::Load` load sky, water, `.bto` blocks and other non-record NIFs that the offline patcher never touches, so
leaving them unhooked is correct.

Per call, with `path` normalized to lower case and backslashes:

1. Disabled, or `path` does not end in `.nif`, or does not start with `meshes\`: call the original.
2. Facegen path test as offline (`PGNIFUtil::isFacegenMesh`; BodySlide ShapeData never loads in game), otherwise
   look the path up in the uses map. No use and not facegen: call the original.
3. Wait on the readiness latch.
4. Resolve the request: a real path is its own base; a virtual variant path resolves to its original path and
   variant number. Fetch or compute the path's patch plan (4.4), which patches every context of the original from
   one parse the first time any member of the path is requested, or re-patches the one requested context when the
   plan already exists.
5. Hand a memory stream of the requested output to the original `BSStream::Load` with the requested name, so
   `inputFilePath` carries it. For a real path whose output is unchanged, the original bytes are served. The input
   bytes come from the engine's own stream for a real path and from a `BSResourceNiBinaryStream` on the original path
   for a variant, so the input is always the bytes the engine would have parsed.

Rules: any exception or nifly failure passes the original bytes through and logs once per path; the hook must never
take the game down. `PGNIFUtil::loadNIFFromBytes` already wraps nifly in SEH and rejects truncated files, null
shapes, dangling texture set references and non-ASCII texture paths exactly as offline. The hook runs on whichever
thread the engine loads on: the resource loader threads for queued loads during cell attach and streaming, the main
thread for synchronous demands such as equipping an item. Everything it touches is either per call (nifly objects),
read only after startup (maps, TruePBR tables), or guarded (`PGD3D`, the Logger, the plan table). Two threads
requesting members of the same path serialize on that path's plan entry, so a path is patched once.

The memory stream is a PGLite subclass of `RE::NiBinaryStream` (CommonLib declares the virtual layout and the read
and write function pointers), so no engine allocator is involved. `NiMemStream` is the fallback.

### 4.3 The patch of one path

For one original path the shared code runs exactly as `PGPatcher::patchNIF` runs offline, minus files: load the
bytes once, create a `PGMeshPermutationTracker`, and for every use in the offline order stage a copy, run
`processNIF` with that use's attributes (`createNIFPatcherObjects`, shapes in 3D index order, pre patchers with
`StockMarker` first, `matches` and `sortMatches`, the winner's `applyPatch`, post patchers, texture set write-back or
alternate texture result, `finalizeMarker`, `removeEmptyMarkers`, global patchers), then `commitMesh`, which merges
identical results and keeps a use whose result equals the original on the original mesh. `saveMeshes` then sorts the
blocks with `PrettySortBlocks`, exactly as offline, serializes every distinct output with
`Save(ostream, { .optimize = false, .sortBlocks = false })`, and returns the index corrections of every output. The
tracker gains a mode that returns the serialized bytes instead of queuing files; nothing else in it changes.

The result of the path is its plan: which uses map to which output, the bytes of the requested output (served and
dropped), the alternate texture results per use, the index corrections per output, and for weighted paths the block
type signature of each output. Only the plan is kept, not the bytes; a later request for another output of the same
path re-runs the patch for that context, which is deterministic.

Patchers available at runtime: the stock marker (restore half; its create half only fires on BodySlide ShapeData),
Fix Mesh Lighting, Fix Texture Slot Count, Parallax, Complex Material, True PBR, the Default shader patcher, Restore
Default Shaders, Hair Flow Map. Excluded because they generate textures: the Parallax to Complex Material transform
and Fix SSS (section 7). Excluded as offline-only: Particle Lights to Light Placer, Convert to HDR.

### 4.4 The permutation system

Offline, one NIF path serves many records while the result can depend on the record, so distinct results become
duplicate files under `_pgpatcher_dups\N`, records are retargeted to them, alternate texture indices are corrected
for the sorted and possibly reduced block layout, and patched alternate textures become new TXST records
(`PGPatcher.cpp:561-633`, `PGMutagen.cs:961-1268`). The engine's model cache has the same shape as the output folder,
one model per path, so the runtime keeps the mechanism and replaces files with virtual paths and plugin writes with
in-memory record edits. It is part of the initial implementation, not a later phase.

**Context.** The per-use inputs that can change a result: alternate textures (3D index to the eight TXST paths, with
the `textures\` prefix), whether the record type is in the allowed set, the record ignore flag (header flag
0x01000000), mod level "meshes ignored", single-pass material object (STAT `DNAM` material with the MATO single-pass
flag), the grass record type, and the weighted flag. Uses with equal contexts always patch identically.

**Plan at boot (records only, no mesh reading).** For every path with more than one distinct context, or with a
context that must keep the original mesh, assign paths:

- The original path goes to the primary context, which is the first use in the offline ordering (uses without
  alternate textures first, then weighted, then plugin name, form ID, sub model; `PGPlugin.cpp:136-150`). If any use
  is filtered or ignored, the original path serves the unpatched mesh instead, exactly as `ignoreBaseMesh` does
  offline, and every patched context gets a variant.
- Every other context gets a virtual variant path, `meshes\_pgpatcher_dups\<n>\<original path>`, and the model
  strings of its records (`TESModel::model`, including the ARMO and ARMA sub-models and destructible stage models)
  are set to it. This is the runtime form of MODL retargeting and happens before anything loads.

**First request of a path (loader thread, inside the hook).** Patch every context from one parse as in 4.3, which
also yields the offline merge: contexts whose results are identical share one output. The output of a group takes
the lowest variant number of its members as its name; records of the other members are retargeted to that name by a
main-thread task (`SKSE::TaskInterface`), so later instances share one model. A request for a member that was merged
away is still served its group's bytes under the requested name, which leaves at most one transient duplicate model
until the engine evicts it. Then the TXST step for every use of the path: every alternate texture whose patched slots
differ from the TXST becomes a `BGSTextureSet` created through the form factory, deduplicated by its eight strings
(case sensitive dedupe, case insensitive "unchanged" test, as `PGMutagen.cs:1194-1212`), the record's alternate
texture entry is repointed to it and its 3D index replaced by the output's corrected index (`slotIDNew` logic of
`PGPlugin::setModelUses`), and the entry is removed when its shape was deleted. The record edits of the requested
context run before the load returns, and the engine applies the swap at `Clone3D` on the main thread afterwards, so
even the instance that triggered the load sees the patched set; at that moment no instance of any record of that
context exists yet, so the edits have no concurrent readers. Edits for merged-away contexts go through the same
main-thread task as their retarget.

**Weighted pairs.** The engine derives `_1.nif` from a record's `_0.nif` by suffix, so a weighted use is registered
under both paths (as `PGMutagen.cs:398-446` does) and the variant number of a context is the same for both paths of
the pair. The offline check that both files of a pair produce the same block layout (`validateWeightedVariants`,
block types only) is kept: each weighted path's plan stores the block type signature of every output, and when the
counterpart's plan is computed the signatures are compared per variant and a mismatch is logged as an error and
shown in the GUI warnings, exactly the offline behavior.

**Lifecycle.** Variants are ordinary model cache entries: created when a reference needs them, shared by every record
of the context, released when the last instance is gone, rebuilt from the original file on the next visit. PGLite
keeps plans, not bytes. The dynamic TXST forms get FF form IDs, are never saved and are recreated on the next game
start.

### 4.5 File map and texture classification

`BethesdaDirectory` is reused for the file map: loose files walked under the data folder (works through MO2's
virtual file system) and archive directory tables read with rsm-bsa in the engine's archive order (INI lists plus the
archives of each active plugin, with the plugin list taken from `TESDataHandler`). It answers `isFile`, which
archive a file comes from (needed by `vanillabsalist` and by mod attribution), reads for DDS headers and JSONs, and
the texture enumeration.

Classification is by file name suffix (`PGNIFUtil::defaultsFromSuffix`), texture rules from the settings, and the
`vanillabsalist` exclusion of height maps and environment masks inside the listed archives, with no mesh reading pass
(section 7). Environment masks are added to the maps as environment masks first and classified as complex material
lazily: the patchers resolve candidates through the directory (`PGDirectory::texturesForBase`, a small shared
refactor replacing raw map access in `texMatch` callers), and the runtime directory classifies the masks of a base
the first time a lookup touches it, through `PGD3D::checkIfCM` on PGLite's own D3D11 device with the compute shader
compiled into the binary. Results are cached on disk by file identity like `PGRunCache` caches them offline, so a
second session never decodes a mask again. Aspect ratio checks read DDS headers through the file map on demand.

### 4.6 Plugin uses from the loaded game

A runtime implementation of the `PGPlugin` interface replaces the Mutagen one and reproduces
`PGMutagen.GetModelElems` and `PopulateModelUses`: the 35 record types (ADDN is enumerated offline but never patched
and stays out), `MODL` with alternate textures for the modeled types, ARMO world models male and female, ARMA world
and first person models per sex, destructible stage models for the types that have them, the lower case model path
with the `meshes\` prefix, alternate textures by 3D index with the eight TXST paths, weighted registration under both
suffixes when the ARMA weight slider flag is set for that sex, the ignore flag from the form header, and single pass
from the STAT material's MATO. The loaded form is what the engine uses, so runtime record edits by other tools are
seen (section 7). The record type of a use is the loaded form's type, mapped to the same 35 signatures.

The allowed record types setting works as offline: the filter is applied per use with the use's record type
(`PGPatcher.cpp:564-575` semantics), a filtered use keeps the original, unpatched mesh, and the remaining uses get
variants; facegen is patched regardless of the setting, as its dummy use bypasses the filter offline.

Verification: dump the runtime uses map (path, record, sub model, context) once and compare it with what the
offline run derives from the same load order, to confirm the loaded forms match Mutagen's winning overrides when no
runtime tool edits records.

### 4.7 Conflict resolution

`PGModManager` is reused as is: MO2 (`modorganizer.ini`, profile `modlist.txt`, mod folders walked for the folders
to map) and Vortex (`vortex.deployment.json`). `modByFileSmart` resolves archived files through the file map's
archive name. Mod shader attribution comes from the texture classification, so `updateStateFromModlist` runs after
it, as offline. The MO2 instance is auto-detected (the running MO2 from the injected `usvfs_x64.dll` via
`mo2DirFromUSVFS`, the global instance from MO2's `CurrentInstance` registry value, to verify) and confirmed in the
GUI rather than typed.

### 4.8 Lifecycle and apply

Patching happens only when the engine loads a file: cell attach behind loading screens, exterior streaming as cells
enter the grid, actors and equipment, facegen heads, effects, menu previews, and the engine's own preloading. The
engine's model cache serves repeat requests without a file load, buffered cells keep models resident after detach,
and once the last instance is gone the model is dropped; the next visit reads the file and runs the hook again. The
patched state lives only in the engine's objects, so unloading needs no PGLite bookkeeping, and PGLite stores no
mesh bytes at all. Performance is evaluated after the first in-game tests; caches are not part of the design.

Apply in the GUI saves the settings, re-registers the patchers, reloads texture rules and mod rules, drops the plan
table, recomputes the permutation plan and the record retargets on the main thread. From then on every file load
uses the new settings. What does not change is what is already loaded: PGLite never modifies loaded objects, so a
model that is resident in the engine's cache (the current cell, buffered cells, equipped items, loaded actors) keeps
the bytes it was built from until the engine drops it, which happens when its last instance is gone (leaving the
area beyond the cell buffer, unequipping, cell purge, or loading a save). Returning to a place after the engine
evicted its models shows the new settings; standing in the same cell after apply does not. A "reload the current
cell's 3D" action (release and re-create the 3D of loaded references, reset actors) would make it immediate and is a
later addition if wanted; it is not part of apply.

### 4.9 Disable conditions

- An offline output is present: `ParallaxGen_Diff.json` in the load order (loose or archived) or `PGPatcher.esp`
  active. Same check the offline patcher makes (`main.cpp:600-606`).
- Auto Parallax is installed (`AutoParallax.dll` in `SKSE\Plugins`): the two would edit the same materials.
- Address Library missing or unsupported for the running game version.

In every case PGLite logs the reason, shows a notice ("PGLite is disabled because X is present", an in-game message
box after data load; a Windows message box at plugin load for the Address Library case, when the game UI does not
exist yet), and the GUI stays available and states the reason.

### 4.10 GUI (Dear ImGui)

Hotkey from the ini. Device and context from `BSGraphics::Renderer`, present and input hooks chained like every other
ImGui plugin. Pages: mod manager (type, detected MO2 instance with confirm, MO2 loose file order) and the mod list
(mods with shaders or meshes as `updateStateFromModlist` numbers them, drag to reorder, enable, meshes ignored; apply
writes `modrules.json`); patcher checkboxes (Fix Mesh Lighting, Parallax, Complex Material, True PBR, Restore
Default Shaders, Hair Flow Map); allowed record types; mesh allow list and block list; texture rules; a status panel
(ready state, patched mesh and variant counts, disable reason) and the warnings and errors collected through the
Logger capture callback, deduplicated (weighted pair mismatches land here). English only. No conflict highlighting or
viewer, as specified.

### 4.11 Files

- `Data\SKSE\Plugins\PGLite.ini`: hotkey, log level, exclude facegens, disable dynamic cubemap.
- `Data\SKSE\Plugins\PGLite\settings.json`: the offline `params.*` keys and nesting, so settings can be copied
  between the two programs; `vanillabsalist` and the default mesh block list stay hidden defaults. The serialized
  keys are never renamed.
- `Data\SKSE\Plugins\PGLite\modrules.json`: the offline schema.
- `Data\SKSE\Plugins\PGLite\cmcache.bin`: the complex material classification cache (4.5), the one thing PGLite
  persists.
- `My Games\Skyrim Special Edition\SKSE\PGLite.log` through spdlog and the Logger facade.
- The package ships `textures\cubemaps\dynamic1pxcubemap_black.dds`, which the offline run deploys into its output.

## 5. Shared code: PGLib changes

Prerequisite: the `featalphaprop` branch (copy-on-write texture sets and alpha properties in `PGNIFUtil`, which
removes the per-path static texture set cache of `PatcherMesh`) is merged before implementation starts, so two
paths can be patched on two threads without shared state.

All of the following are behavior preserving, covered by unit tests as `CLAUDE.md` requires, and the offline output
must stay byte identical on a reference load order.

1. Split `PGPatcher.cpp`: the per-mesh pipeline (`processNIF`, `processNIFShape`, `matches`, `sortMatches`,
   `applyTransformIfNeeded`, `createNIFPatcherObjects`, patcher registry) moves to a shared unit; the offline runner
   (`patchMeshes`, `patchNIF`, `replayNIF`, output directory, diff JSON, textures) stays offline.
2. `PGPlugin` becomes an interface with the Mutagen implementation and the runtime one of 4.6. The header already
   has no Mutagen dependency.
3. `PGMeshPermutationTracker` gets a save mode that returns the serialized bytes per output, with the same sorting
   and index corrections, instead of queuing files, so the runtime uses the same staging, comparison, ordering and
   weighted pair logic.
4. `PGDirectory`: a mode without the mesh reading pass; `texturesForBase` with a classification hook for the lazy
   complex material detection; the archive load order takes an injected plugin list.
5. The update cache recording calls in `PGDirectory`, `PGNIFUtil::texMatch`, `PGModManager` and `matches()` go
   behind a small observer interface that `PGRunCache` implements offline, so `PGRunCache` (which pulls in the
   permutation tracker and the texture hooks) is not compiled into the DLL.
6. `PGConfig`'s parameter struct and JSON (de)serialization move to PGLib without the wxWidgets-only parts.
7. `PGD3D::initShader` loads compiled bytecode: the four shaders are compiled at build time with `fxc` and embedded.
   This removes the `cshaders` folder and the `D3DCompiler` dependency from both programs.
8. `StringUtil` replaces its four `boost::locale::conv` calls with the Win32 conversion functions, so boost.locale
   is not linked into the DLL.

## 6. Build and packaging

```
PGLite/
  CMakeLists.txt        standalone configure: x64-windows-static, C++23 (CommonLibSSE-NG), /MT
  vcpkg.json            own manifest and baseline
  package/              SKSE/Plugins/PGLite.ini, textures/cubemaps/dynamic1pxcubemap_black.dds
  include/ src/         entry point, hook and memory stream, runtime environment, permutation plan, GUI
external/CommonLibSSE-NG   submodule (alandtse, ng branch): tests off, SE and AE on, VR off
```

The root configure uses a dynamic CRT, C++20 and builds PGLib as a DLL; a configure has one triplet, so PGLite is a
second configure that compiles the shared PGLib sources a second time as a static library. `buildRelease.ps1` builds
both and produces `PGLite.zip` as the second release artifact; `build.yml` gets a job that runs
`check_build_warnings.py` on the PGLite log as well. Statically linked: CommonLibSSE-NG, nifly, spdlog, nlohmann-json,
imgui (dx11 and win32), simpleini, DirectXTex (CPU side, DDS headers and `PGD3D`), rsm-bsa with lz4 and zlib, the
header-only boost parts the shared code uses. Not linked: wxWidgets, CLI11, miniz, cpptrace, the .NET host,
json-schema-validator. WebKit style and the review checklist apply; `run_clang_tidy.py` gets the PGLite compile
commands. No unit tests for the PGLite module itself, as specified. The test deployment target is
`C:\Users\hakasapl\AppData\Local\ModOrganizer\VANILLA\mods\PGRunner`.

## 7. Differences from the offline patcher

1. No texture generation. The Parallax to Complex Material transform and Fix SSS are GUI patchers offline that
   produce new DDS files; at runtime they do not exist. A shape whose parallax match fails `canApply` and would have
   been upgraded to complex material offline falls through to the next match. Soft lighting shapes keep their diffuse
   in the glow slot.
2. Texture classification is by suffix, texture rules and the vanilla archive list only. Offline, textures used by
   meshes are classified by a vote of the shapes that use them (`PGDirectory.cpp:395-597`); a texture whose name
   contradicts its use (an `_m` used as a glow map, a normal map without `_n`) is classified differently, which can
   change the matches of every mesh sharing its base name.
3. Records are the live runtime state. Base Object Swapper, Seasons of Skyrim, SPID, SkyPatcher and similar tools
   change records or references in memory; PGLite sees the changed record, the offline run sees the plugin files
   (4.6 has the check that the two agree otherwise).
4. Nothing is written: no duplicate files, no plugin files, no texture set plugin records (they are dynamic forms),
   no diff JSON, no zip, no update cache, no Light Placer forwarding, no BodySlide ShapeData handling, no
   `--consider-allmeshes`, no pgtools-only patchers.
5. Settings changes apply to files loaded afterwards; models already resident in the engine keep the bytes they were
   built from until the engine evicts them (4.8).
6. A merged-away variant that was requested before its merge can be held once more in memory until evicted (4.4).
   Visually identical to offline.
7. The offline DynDOLOD refusal has no runtime equivalent and is not needed: `.bto` files never pass the hook and the
   default block list keeps LOD and DynDOLOD meshes out.
8. New behaviors: PGLite disables itself with a notice when an offline output or Auto Parallax is present; the GUI
   is English only; `exclude facegens` and `disable dynamic cubemap` are ini options instead of command line flags.

Explicitly the same: block order of the patched mesh (sorted as offline), the weighted `_0`/`_1` layout check, the
allowed record types filter, `PG_IGNORE` extra data, `PG_STOCK` restore, Fix Texture Slot Count, facegen patched for
a dummy use with the TruePBR `nif_filter` disabled, skin tint shapes ignoring alternate textures, the use ordering
that picks which use keeps the original path, TruePBR config order (file map order), the dynamic cubemap texture,
the `vanillabsalist` rule, mod attribution and priority semantics, the complex material classification thresholds
(same shader), alternate texture and duplicate handling, and every shader, flag, slot, field and geometry edit the
patchers make.

## 8. Implementation items to verify in the binary or in game

1. **Hook targets per game version.** `BSStream::Load` and the two call sites in the model loader need Address
   Library IDs and offsets for 1.5.97, 1.6.1170 and 1.7.x; 1.7.x needs Address Library format 5 and a
   CommonLibSSE-NG revision that supports it. The inline detour is the fallback if call-site offsets prove fragile.
   Reverse engineering happens when this is implemented.
2. **Virtual paths reach the hook.** The decompiled loader calls `BSStream::Load` after a failed stream open, so
   substitution works in principle; confirm in game that a missing file produces no earlier bail-out and no error
   model, and that the entry database does not cache the failure.
3. **Record edits at runtime.** Rewriting `TESModel::model` strings and alternate texture entries and creating
   `BGSTextureSet` forms is the offline plugin step applied to the loaded forms. Verify that form creation from a
   loader thread is safe or must be deferred (which would delay the TXST repoint past the first instance), and that
   the engine's weighted suffix derivation works on the variant name.
4. **CS and swapped texture sets.** Confirm that a PBR material stays PBR when an unregistered TXST is swapped onto
   it and loads the new RMAOS and feature textures; the CS sources only show the swap hooks tracking the current set.
5. **Startup gating.** The file map and the MO2 folder walk take seconds under usvfs; the first loading screen waits
   for them. Order the startup so the main menu is not delayed (uses map first, pass-through for use-less paths).
6. **Lazy complex material classification** runs on loader threads behind the `PGD3D` mutex; confirm device
   creation inside the game process and the shader embedding.
7. **Memory stream class**: a `NiBinaryStream` subclass has to satisfy the parser's read patterns (`get_info` is
   used by the engine's buffered stream; the base implementation must suffice).
8. **Memory.** Plans not bytes, the merge of identical results, and the disk-backed classification cache keep
   PGLite's own footprint small; the transient duplicate of 4.4 is the one known waste.
9. **Performance** is evaluated after the first in-game tests, not designed for up front.

## 9. Milestones

1. PGLib seam work (section 5, after `featalphaprop` is merged) with tests; offline output byte identical on a
   reference load order.
2. PGLite scaffold: configure, submodule, entry point, logging, ini, disable conditions with notices, the load hook
   in pass-through mode, memory stream, release packaging and CI job. Deploy to the MO2 test mod folder.
3. Runtime patching: file map and archive list, uses map, suffix classification and lazy complex material detection
   with its cache, TruePBR configs, mod manager, readiness latch, the full per-path patch with the permutation
   system (contexts, plan, record retargets, variants, merge, TXST forms, index corrections, weighted pair check).
4. ImGui menu with all pages and apply.
5. In-game testing, performance evaluation, and the differences list in the README.

## 10. Working assumptions

Defaults the design proceeds with unless overruled:

1. Suffix-only classification without an import of the offline classification.
2. Complex material detection through `PGD3D` on PGLite's own device with embedded shaders, lazily, with the disk
   cache of 4.5.
3. Apply affects the next load only; no forced scene refresh.
4. English-only GUI.
5. Supported runtimes 1.5.97, 1.6.1170 and 1.7.x; no VR.
6. Call-site hooks on the two model loader calls to `BSStream::Load`, inline detour as fallback.

## Appendix A: reverse engineering notes (SkyrimSE_1.7.99)

`BSModelDB::Demand` 0x140FC9060 -> `sub_140FC8D80` (prefixes `meshes\`, entry database at `qword_14219DEE8 + 208`)
-> loader `sub_140FC8480`. In the loader: `sub_140F01090` constructs the `BSResourceNiBinaryStream` (0x38 bytes),
`sub_140EE3720` is the `NiStream` constructor (vtable 0x141A47168) followed by the `BSStream` vtable (0x1417EEDC8),
`sub_140FD12D0` is `BSStream::Load(this, path, stream)`, the error model name lives in `qword_14331F590`,
`qword_1421A3780` is the `TESModelDB::TESProcessor` singleton (vtable 0x1417F75F0, `PostCreate` at slot 1), and the
notification after a load is a vfunc at +0x60 of `qword_14331D7C0`. `BSStream::Load` copies the path to +0x108 with
`strcpy_s` and normalizes it with `sub_140EACC30`, then calls `sub_140EE40A0`, which checks `stream->good()`, stores
the stream at +0x298 and calls vfunc 15. `sub_1403C2EC0` is the NPC head loader (formats the FaceGeom path through
`sub_1403C2E00`, demands `.p` and `.nif` through `sub_140FC8D80`). `sub_1405048A0` loads `BGSDistantObjectBlock`
(`.bto`) through `BSStream::Load`. `NiMemStream` constructors at `sub_14156A6A0`, `sub_14156A710`, `sub_14156A780`,
`sub_14156A9B0`. `BSShaderProperty` flag word at offset 0x38 (CommonLib).

## Appendix B: offline facts that constrain the runtime (verified in the repository)

Alternate textures are matched by 3D index counted over `NiShape` and `NiParticleSystem` blocks in tree order
(`PGNIFUtil.cpp:516-542`); the MODS name is never used. For an alternate texture shape the NIF keeps the vanilla
texture set and gets the patched shader state, the patched paths go to a new TXST (`PGPatcher.cpp:773-779`,
`886-892`; `PGMutagen.cs:1142-1255`). Record ignore is header flag 0x01000000 (`PGMutagen.cs:917-918`). The allowed
record type filter is applied in C++ only (`PGPatcher.cpp:564-575`). Single pass comes from the STAT material's MATO
flags and only disables parallax and restricts complex material (`PGMutagen.cs:901-906`,
`PatcherMeshShaderVanillaParallax.cpp:58`, `PatcherMeshShaderComplexMaterial.cpp:101-106`). Weighted ARMA models are
registered under both `_0` and `_1` (`PGMutagen.cs:398-446`), and the two outputs of a pair are compared by block
types only (`PGMeshPermutationTracker.cpp:297-352`). The offline save sorts blocks and reports index corrections to
the plugin (`PGMeshPermutationTracker.cpp:198-234`). TXST slot order equals NIF slot order 0 to 7; the NIF's ninth
slot never reaches a plugin. `pbrtexturesets` appears only in the output cleanup list (`PGPatcher.cpp:306`); nothing
writes it.
