# PGLite design

Status: decided design as of 2026-10-11, including Hakan's decisions on the roadblocks and differences (no phases,
blocks sorted as offline, weighted pair check kept, no equivalence tooling, in-game notice when disabled, memory
kept low), revised after the adversarial review of the same day and after the first in-game tests, which brought the
classification cache of 4.5 and the texture cache of 4.12. The review showed that the engine never loads
a model by path, so the load hook of the first version is replaced by resource streams registered in the engine's
resource registry (sections 3.1 and 4.2). Section 7 is the complete list of differences from the offline program.
Section 10 lists the remaining defaults. Facts marked *verified* were read in this repository (branch `pglite`,
commit fd2fd4c), in the Community Shaders (CS) sources on GitHub, in the CommonLibSSE-NG headers or in the IDA
database of `SkyrimSE_1.7.99.exe`. Facts marked *to verify* are confirmed in the binary or in game when the code that
depends on them is written.

Implementation status (2026-10-11, branch `pglite`): sections 4 to 6 and the milestones of section 9 are
implemented. Deviations from the text: `PGPlugin` keeps its static interface
and the runtime provides its own implementation unit (`PGLite/src/PGPluginRuntime.cpp`) instead of a virtual
interface. Texture set objects are deduplicated per session rather than per family. The menu draws from the swap
chain's Present and takes its input from the call in `BSInputDeviceManager::PollInputDevices` that hands the polled
events to the game's event source; while the menu is open that call receives an empty list, so the game gets no
input at all (Community Shaders and Wheeler redirect the same call, and the redirects chain). The DLL
imports two system runtimes besides Windows itself: `d3dcompiler_47.dll` (a Windows component; the Dear ImGui
DirectX 11 backend compiles its two shaders at startup) and `vcomp140.dll` (the OpenMP runtime of the Visual
C++ redistributable the game needs anyway, pulled in by DirectXTex as in the offline PGLib). The offline
binaries no longer import the shader compiler. The two texture generating patchers run at runtime with a texture cache
(4.12), which replaces the first difference the original section 7 listed, and the mesh reading pass runs at startup
with its votes cached per mesh (4.5), which replaces the second.

## 1. What PGLite is

PGLite is an SKSE plugin (one statically linked DLL, CommonLibSSE-NG, Dear ImGui) that produces the same meshes the
offline PGPatcher produces, at the moment the game loads them. It runs the unchanged PGLib patcher stack on the bytes
of every NIF the engine reads, hands the engine the patched bytes, and reproduces the offline plugin step (mesh
duplicates and texture set records) in memory. Nothing of the offline output is written to the data folder; PGLite's
own files live under `SKSE\Plugins\PGLite`, and the Light Placer config of 4.13 under `LightPlacer`. Any behavior
difference that section 7 does not list is a bug.

## 2. The central decision: patch the stream, not the scene

The specification proposed a wrapper over NIF shapes with a nifly back end offline and a CommonLib back end at
runtime. That wrapper is not built. The patchers keep calling nifly through `PGNIFUtil` and `PGMaterialState` as they
do today, and PGLite applies them to the NIF data in memory before the engine parses it. Three verified facts decide
this:

1. CS decides that a shape is PBR inside its replacement of `BSLightingShaderProperty::LoadBinary`, while the engine
   is parsing the stream and before the texture set block is linked. Nothing later re-evaluates the flag. Any edit
   made to loaded objects is therefore invisible to PBR, and "set the flag before CS reads it" can only be done by
   having the flag in the parsed data. The stream patch does exactly that, without any CS class, export or API.
2. The patchers change geometry, not only materials: vanilla parallax adds a vertex color stream, TruePBR entries
   add or remove that stream, rewrite vertex colors in HSL (`vertex_color_lum_mult`, `vertex_color_sat_mult`),
   recompute normals and tangents (`smooth_angle`), delete shapes and add or delete blocks. On loaded objects these
   are GPU-resident buffers with no engine API; on the stream they are the existing nifly calls.
3. Only the stream patch is comparable to the offline output: with blocks sorted the same way, the bytes PGLite
   hands the engine for a mesh are the bytes the offline run writes for the same use.

What remains engine specific is the environment: the resource streams PGLite registers with the engine, the record
data (`TESDataHandler` instead of Mutagen), the in-memory record edits of the permutation system, file reads through
the engine's resource layer, settings, logging and the GUI.

## 3. Engine and CS facts the design relies on

### 3.1 How the engine finds and reads a mesh (verified in the 1.7.99 database; the two functions PGLite calls also in 1.5.97)

The engine never opens a model by path. The chain is: path, 12-byte resource ID, registry record, stream object.

| Step | What the engine does |
|---|---|
| Path to ID | `BSResource::ID::GenerateFromPath` (CommonLib `RELOCATION_ID(68635, 69979)`, 1.7.99 `sub_140ED3EE0`) hashes the path into `{file hash, extension, directory hash}`. The lookup is case insensitive. |
| Registry | One global registry (`qword_14331B4C0`: a `std::shared_mutex` at +0, the tables at +16). A record is 40 bytes: the ID at 0, flags at 12 (bit 31 marks an archive record), size at 16, a `BSFixedString` name at 24 and at 32 either a `BSResource::Stream*` (loose record) or the archive object (archive record). The record constructor `sub_140EC9A60` asks the stream for its name through `DoGetName`. |
| Filling the registry | At startup (`sub_14065DBE0`): the INI archive lists, the hard coded `Update.bsa`, then for every plugin in load order `<name>.bsa` and `<name> - Textures.bsa` (`sub_1401C4CB0`), then the loose files through `BSResource::RegisterPrefix(prefix, root, Location*)` (`sub_140ECF570`; list form `sub_140ECF670`), whose traverser `sub_140ED2AF0` hashes each name, asks the location for a stream (`DoCreateStream`) and inserts the record. Loose records hold one lazily opened `LooseFileStream` each. |
| Insert semantics | `sub_140EC8170(ctx, id, stream)`: a new ID gets a new record; an existing loose record gets its stream replaced; an existing archive record is converted to a loose record with the given stream. This is the rule that makes loose files override archives, and it is what PGLite relies on. |
| Register a path with a stream | `sub_140ECEE30(const char* path, BSTSmartPointer<Stream>&)`: ID from the path, then the insert above. The engine uses it for sound aliases (`sub_140559E30`) and for its file cache validation (`sub_140ED9240`, "OnValid"). It takes the registry mutex itself. In 1.5.97 it is `sub_140C45280` (ID 68491), the same body with no caller left in that build; its insert `sub_140C3E2E0` replaces an existing loose record's stream and converts an archive record (`sub_140C3E3F0`), and its return value is meaningless there, so PGLite ignores it on both runtimes. |
| Resolve a path to a stream | `sub_140ECE860(const char* path, BSTSmartPointer<Stream>&, bool writeable, Location* optional)`: registry first (a loose record's stream is cloned, an archive record gets a fresh `ArchiveStream` or `CompressedArchiveStream`), then the location tree (`GlobalPaths`, `GlobalLocations`, `LooseFileLocation`) for files on disk that are not registered. It is the first call of the CommonLib-bound `BSResourceNiBinaryStream::ctor` (`RELOCATION_ID(69636, 71014)`, 1.7.99 0x140F00F80) and takes the registry mutex itself. In 1.5.97 it is `sub_140C44CB0` (ID 68484), the resolver call of the same constructor (`sub_140C76060`). |
| Model entry | `BSModelDB::Demand` 0x140FC9060 and `sub_140FC8D80` prefix `meshes\` (`sub_140ECDC50`, which accepts an embedded `meshes\` and forward slashes), hash the path and look the ID up in the model entry database (`sub_14020BD20`). On a miss an entry is created only when the ID has a registry record, and the entry's stream is the record's stream (`sub_140EC8570`: loose records share their stream object, archive records spawn a stream). Facegen heads (`sub_1403C2EC0`) and queued and asynchronous loads go through the same entry database. |
| Loader | `sub_140FC8480(BSTSmartPointer<Stream>*, ...)` receives the resolved stream. `sub_140F01090` wraps it in a `BSResourceNiBinaryStream`: sets the open flags, calls `DoOpen` (vfunc 1), allocates a buffer of `totalSize + 56` (`StreamBase::totalSize` at +8, 0x10000 when the stream is writable), and reads through `DoRead` (vfunc 6) and `DoSeek` (vfunc 8). Its destructor (0x140F01260) clears the flags, calls `DoClose` (vfunc 2) and releases the reference. Then `BSStream::Load` parses, falls back to `Meshes\Marker_Error.NIF` when no top object came out, builds a root `NiNode` for several top objects and runs `BSModelProcessor::PostCreate`. The path argument of `BSStream::Load` is the stream's own name and is null for files from archives that do not retain names (no vanilla archive does: BSA flag 0x10 is absent and `sResourceArchiveRetainFileNameList` is empty). |
| Stream contract | CommonLib declares `BSResource::StreamBase` and `BSResource::Stream` with the full virtual layout: dtor, `DoOpen`, `DoClose`, `DoGetKey`, `DoGetInfo`, `DoClone`, `DoRead`, `DoWrite`, `DoSeek`, `DoSetEndOfStream`, `DoGetName`, `DoCreateAsync`. The reference count lives in the flags word (`kRefCountBeg` 0x1000), which sits at +0xC on SE and +0x10 on AE; CommonLib's `GetFlags()` resolves that at runtime. One entry is loaded by one thread at a time (other demanders spin in `sub_14020B970`), so a stream object is opened by one loader at a time; direct opens by path clone it. |
| Registered locations | `BSResource::RegisterLocation` (CommonLib `RELOCATION_ID(68476, 69829)`) only inserts a `Location` into the priority list of `GlobalLocations` and mounts it. It registers no IDs. |

Two consequences shape PGLite: a virtual path that has no registry record is never loaded, whatever hook sits in the
loader; and a registered record decides which bytes the engine parses, whatever the path string says. The engine's
64-bit shader property flag word is a positional copy of the NIF's SLSF1 and SLSF2 words (verified against
CommonLibSSE-NG's `EShaderPropertyFlag8`: `kMenuScreen` is 55, the loaded form of `SLSF2_UNUSED01` at bit 23;
`kVertexColors` 37; `kTreeAnim` 61). The stream patch never touches these.

### 3.2 Community Shaders (verified in the `dev` sources)

- TruePBR replaces `BSLightingShaderProperty::LoadBinary` (vfunc 0x18) with its own parser; the original is not
  called. After parsing, `if (property->flags.any(kMenuScreen))` the material is replaced with
  `BSLightingShaderMaterialPBR::Make()`, the PBR flags are derived from the shader flags, `kVertexLighting` is set as
  the internal marker and legacy bits are cleared. Textures load later from the texture set through the PBR
  material's own `OnLoadTextureSet`. `inputFilePath` is used for logging only, so the name a PGLite stream reports
  is irrelevant to CS.
- Other TruePBR hooks: `GetRenderPasses` (vfunc 0x2A), `BSLightingShader::SetupMaterial/SetupGeometry`,
  `BGSTextureSet::ToShaderTextureSet` and `BSLightingShaderProperty::OnLoadTextureSet` (track the texture set being
  applied), `TESObjectSTAT::Clone3D` (PBR material objects). PBR alternate textures are recognized only through
  `Data\PBRTextureSets\*.json` by TXST editor ID, a registry read at startup.
- Alternate texture swaps keep PBR: the engine swap (`sub_1401EE0E0`) re-sets the material, assigns the new texture
  set, clears the textures and calls `OnLoadTextureSet`; CS's PBR material reloads RMAOS and the feature textures
  from the new set. A texture set without a `PBRTextureSets` entry simply gets no extra data, which is what the
  offline `PG_` TXST records get as well (verified in the 1.7.99 database and the CS sources).
- Complex material and parallax are decided in the pixel shader: an environment mask whose mip 15 alpha is below
  `1 - 4/255` is a complex material, the material class selects the `ENVMAP` and `PARALLAX` techniques. No vertex
  color requirement in CS.
- CS hooks the input dispatch call site (`BSInputDeviceManager::PollInputDevices`, IDs 67315/68617 + 0x7B) with a
  call-site hook and detours `IDXGISwapChain::Present` on the live swap chain. Both chain when PGLite stores and
  calls the previous target, as every other ImGui-based plugin does.

## 4. Runtime architecture

### 4.1 Startup

The game loads its data on a thread of its own while the main thread waits for the load and a loading-screen thread
presents frames; `kDataLoaded` is sent on the loading thread, to every plugin's handler in turn, and the main thread
opens the main menu the moment the load completes. PGLite's handler checks the disable conditions (4.9), then
creates the menu, installs its hooks (4.10) and does the data-load work itself, on the thread that owns the records
at this point as every other handler does. SKSE dispatches the message to the plugins in their load order, so the
record edits of the handlers that ran before PGLite's are what it plans from; a handler that runs after it and
edits a model string keeps its value, since PGLite never overwrites a value it did not write itself (4.8), and the
mesh that string names loads as the plan of its own path says. The handler does, synchronously:

1. Detect the disable conditions (4.9) before anything is installed. If any holds, log it, show the in-game notice
   and install nothing: no hooks, no menu, no streams.
2. Snapshot every record field the permutation system edits (model strings of every sub-model, alternate texture
   index and texture set per entry) so Apply (4.8) can restore them.
3. Build the uses map from `TESDataHandler` (4.6) and the record-only permutation plan (4.4): contexts, variant
   numbers, which path keeps the original. A facegen path with record uses is left out with the offline warning.
4. Register one PGLite stream (4.2) for every path with uses and every variant path, through the engine's own
   register-path function of 3.1. For a base path the stream wraps the engine's own stream for that path, resolved
   through the engine's resolver before the registration replaces the record. A path whose file the engine cannot
   resolve gets no stream and is left to the engine, exactly like a missing mesh offline; a weighted pair with one
   such file is patched on the other. Then point the model strings of the records that get variants at them, through
   the snapshot of step 2, for the families that got streams: a record is only ever pointed at a path that is served.
5. Start the background initialization thread, then wait until it is done. The load does not complete while the
   handler waits, so the main menu opens with PGLite ready and no save is loaded into a half built environment; the
   loading-screen thread keeps presenting frames meanwhile, with the loading line of 4.10 and the progress of the
   mesh reading pass drawn into them. This is how Community Shaders holds the main menu for its shader compilation,
   and plugins that wait this way take their turns. The wait gives up with a warning after ten minutes.

The background thread registers the facegen streams (one per NPC form whose `FaceGeom\<plugin>\<formid>.nif`
resolves, unless `exclude facegens` is set), then builds the runtime environment: the mod manager (its file map and
the mod rules), the file map (4.5), the mesh reading pass and the texture classification with the complex material
classification of every environment mask, the TruePBR configs (`loadStatics` over the `pbrnifpatcher` JSONs in file
map order), the switch of the directory's reads to the engine's resource layer, `updateStateFromModlist` when a mod
manager is configured, and the patchers. While PGLite is disabled in the settings it registers nothing and builds no
environment; the Apply that enables PGLite does. Then it marks the environment ready, which ends the wait of the
data-load handler; a loader thread that
opens a registered stream before the ready signal waits for it inside `DoOpen`. It never deadlocks, because
initialization waits for nothing that runs on the main thread or the loading thread, and it never patches at boot.

Paths without uses are never registered and are never touched: main menu and loading screen art without record
uses, sky, LOD, `.bto` blocks, interface meshes.

### 4.2 The resource streams

PGLite does not hook the loader or `BSStream::Load`. It owns the stream objects the engine reads patched meshes from.

A PGLite stream is a `BSResource::Stream` subclass (CommonLib declares the virtual layout, 3.1) created for one
registered path: a base path or a variant path, always with the family it belongs to (4.3) and its context. It is
registered in the resource registry at startup and lives for the session; the engine's model entry for that ID
shares it. Its behavior:

- `DoOpen`: wait for the environment (4.1); take the pipeline reader lock (4.8); fetch or compute the family's plan,
  which patches every context of the family on the first open of any member (4.3, 4.4); produce the bytes of this
  stream's context; set `totalSize`. On any exception or nifly failure, log once per path and serve the original
  bytes instead; a stream must never take the game down. `PGNIFUtil::loadNIFFromBytes` already wraps nifly in SEH
  and rejects truncated files, null shapes, dangling texture set references and non-ASCII texture paths exactly as
  offline.
- `DoRead`, `DoSeek`, `DoGetInfo`: serve the produced bytes. `DoClose`: drop them. Bytes exist only between open and
  close; PGLite keeps plans, never meshes.
- `DoClone`: a new PGLite stream for the same path and context, so a direct open by path (the resolver clones) has
  its own position and buffer.
- `DoGetName`: the registered path, which the registry stores as the record name and the loader passes to
  `BSStream::Load`. `DoWrite`, `DoSetEndOfStream` and `DoCreateAsync` are unsupported, as for read-only engine streams.
- The input bytes of a base path come from the wrapped engine stream (opened, read, closed inside `DoOpen`), so the
  input is always what the engine itself would have parsed, with the engine's own loose-over-archive precedence. A
  variant shares its base's wrapped stream.

Threads: `DoOpen` runs on whichever thread loads the model, the resource loader threads for queued loads during cell
attach and streaming, the main thread for synchronous demands such as equipping an item. Everything it touches is
either per open (nifly objects, bytes), read only while the reader lock is held (maps, TruePBR tables, settings), or
guarded (`PGD3D`, the Logger, the plan table). Two threads opening members of the same family serialize on the
family's plan entry, so a family is patched once.

### 4.3 The patch of one family

A family is one original mesh with all its uses; for a weighted pair it is both files (4.4). For one family the
shared code runs exactly as `PGPatcher::patchNIF` runs offline, minus files: load the bytes once, create a
`PGMeshPermutationTracker`, and for every use in the offline order stage a copy, run `processNIF` with that use's
attributes (`createNIFPatcherObjects`, shapes in 3D index order, pre patchers with `StockMarker` first, `matches` and
`sortMatches`, the winner's `applyPatch`, post patchers, texture set write-back or alternate texture result,
`finalizeMarker`, `removeEmptyMarkers`, global patchers), then `commitMesh`, which merges identical results and keeps
a use whose result equals the original on the original mesh. `saveMeshes` then sorts the blocks with
`PrettySortBlocks`, exactly as offline, serializes every distinct output with
`Save(ostream, { .optimize = false, .sortBlocks = false })`, and returns the index corrections of every output. The
tracker gains a mode that returns the serialized bytes instead of queuing files; nothing else in it changes.

The result of the family is its plan: which uses map to which output (the merge groups), the alternate texture
results per use, the index corrections per output, and for weighted pairs the block type signature of each output.
The bytes of the requested context are handed to the stream and dropped at close; a later open of another member
re-runs the patch for that member's group (deterministic), always from the group's first member's context, because
`compareMesh` ignores normals, tangents, positions and most effect shader fields and a merged-away member's own run
could differ in exactly those (`smooth_angle` is the example).

The first open of a family runs the whole family on the requesting thread, the main thread included. That is a
hitch on synchronous demands of families with many contexts; it is measured in the in-game tests, not designed
around up front (Hakan's ruling).

Patchers available at runtime: the stock marker (restore half; its create half only fires on BodySlide ShapeData),
Fix Mesh Lighting, Fix Texture Slot Count, Parallax, Complex Material, True PBR, the Default shader patcher, the
Parallax to Complex Material transform and Fix SSS (whose textures are generated at first use, 4.12), Restore
Default Shaders, Hair Flow Map. Excluded as offline-only: Particle Lights to Light Placer, Convert to HDR.

### 4.4 The permutation system

Offline, one NIF path serves many records while the result can depend on the record, so distinct results become
duplicate files under `meshes\_pgpatcher_dups\N\<path without meshes\>`, records are retargeted to them, alternate
texture indices are corrected for the sorted and possibly reduced block layout, and patched alternate textures
become new TXST records (`PGPatcher.cpp:561-633`, `PGMutagen.cs:961-1268`, `PGMeshPermutationTracker.cpp:616-630`).
The engine's model cache has the same shape as the output folder, one model per path, so the runtime keeps the
mechanism and replaces files with registered virtual paths and plugin writes with in-memory record edits. It is part
of the initial implementation, not a later phase.

**Context.** The per-use inputs that can change a result: alternate textures (3D index to the eight TXST paths in
NIF slot order, with the `textures\` prefix), whether the record type is in the allowed set (an ADDN use is always
filtered, 4.6), the record ignore flag (header flag 0x01000000), single-pass material object (STAT `DNAM` material
with the MATO single-pass flag), the grass record type, and the weighted flag. Uses with equal contexts always patch
identically.

**Plan at data load (records only, no mesh reading, in the data-load handler, 4.1).** Paths that the mesh block
list and allow
list exclude are skipped entirely: no plan, no stream, the engine loads the original, as offline. For every other
path with more than one distinct context, or with a context that must keep the original mesh, assign paths:

- The original path goes to the primary context, which is the first use in the offline ordering (uses without
  alternate textures first, then weighted, then origin file name, local form ID, sub model; `PGPlugin.cpp:136-150`).
  If any use is filtered or ignored, the original path serves the unpatched mesh instead, exactly as
  `ignoreBaseMesh` does offline, and every patched context gets a variant.
- Every other context gets a virtual variant path, `meshes\_pgpatcher_dups\<n>\<path without meshes\>`, and the
  model strings of its records (`TESModel::model`, including the ARMO and ARMA sub-models and destructible stage
  models) are set to it. Variant numbers come from one session counter that never restarts, so a plan computed after
  Apply never reuses a name and a resident model of the previous plan is never served for a new context.

Mod-level "meshes ignored" is a per-path rule that needs the mod manager file map, which is not available at data
load, so it is applied when the family is first opened: every stream of an ignored path serves the original bytes.
An ignored path with several contexts therefore holds several identical copies in the model cache until they are
evicted; this is the price of planning from records alone (section 7).

**First open of a family (the requesting thread, inside `DoOpen`, 4.2).** Patch every context from one parse as in
4.3, which also yields the offline merge: contexts whose results are identical share one output and the group is
named after the member with the lowest variant number. Then, before any bytes are returned, every record edit the
patch determines is applied for every use of the family, not only the requested one:

- Index corrections: each alternate texture entry's 3D index is replaced by its output's corrected index
  (`slotIDNew` logic of `PGPlugin::setModelUses`); an entry whose shape was deleted gets index -1, which the engine's
  swap application (`sub_1401ED800`) skips, so no array is resized.
- Texture sets: every alternate texture whose patched slots differ from the TXST becomes a `BGSTextureSet` created
  through the form factory and left unregistered (form ID 0, not in the form map, never saved, never purged with the
  save's dynamic forms; the factory's constructor assigns the next dynamic form ID and inserts the form into the
  global map, verified in the 1.7.99 database, which PGLite undoes right after creation), carrying the eight paths only, as the offline records do (`PGMutagen.cs:1230-1241`),
  deduplicated per session by its eight lower cased strings (case sensitive dedupe, case insensitive "unchanged" test,
  as `PGMutagen.cs:1194-1212`), and the entry's texture set pointer is repointed to it. The swap needs only the
  pointer. The eight strings are read and written in NIF slot order through the engine's `GetTexturePath` and
  `SetTexturePath`, which map a slot to its record field themselves (Appendix A), without the `textures\` prefix.

These are single stores into records whose instances do not exist yet (no model of the family has finished loading
when the first open runs, and a second thread opening a member waits on the family's plan entry), so they have no
concurrent readers. The engine applies the swap when the instance's 3D is built, after the load returns.

Records of merged-away members are then retargeted to the group's name by a main-thread task
(`SKSE::TaskInterface`), so later instances share one model. Until that task runs, and for instances already created
from a merged-away name, the engine holds one identical copy per merged-away context; it is evicted with its last
instance. The bytes are identical in every case, so there is no visible difference.

**TXST slot order.** The engine stores `BGSTextureSet::textures[8]` in record order (TX00 to TX07: diffuse, normal,
environment mask, glow, height, environment, multilayer, backlight), while `GetTexturePath` and `SetTexturePath`
(vfuncs 0x25 and 0x27) take NIF slot order. The runtime converts with the `BSTextureSet::Texture` enum: NIF slots 0
to 7 map to record indices 0, 1, 3, 4, 5, 2, 6, 7. Reading a TXST for the uses map produces the eight paths in NIF
slot order with the `textures\` prefix added, as Mutagen's named fields do offline (`PGMutagen.cs:1232-1239`,
`1751-1758`); writing one reverses that. Paths are `BSFixedString`s (case insensitive), so they are lower cased
before the dedupe. An alternate texture whose TXST does not resolve is eight empty strings (`PGMutagen.cs:851-872`).

**Weighted pairs.** The engine derives `_1.nif` from a record's `_0.nif` by suffix, so a weighted use is registered
under both paths (as `PGMutagen.cs:398-446` does) and both paths of a pair form one family: one plan, the same
contexts and variant numbers for both files, and a context merges only when its results are identical in both files,
so a record's single model string never points at `_0` and `_1` meshes of different contexts. The first open of
either file patches both. The offline check that both files of a pair produce the same block layout
(`validateWeightedVariants`, block types only) is kept on the two outputs of each context: a mismatch is logged as
an error and shown in the GUI warnings, exactly the offline behavior, and the record components of a pair are edited
once, with the index corrections of the output of the file the record names. Nothing retains NifFile copies waiting
for a counterpart.

**Lifecycle.** Variants are ordinary model cache entries: created when a reference needs them, shared by every record
of the context, released when the last instance is gone, rebuilt through their stream on the next visit. PGLite
keeps plans, not bytes. The unregistered TXST objects live for the session.

### 4.5 File map and texture classification

The runtime file map holds the textures, the TruePBR configs and every mesh of the load order. During the background
initialization the directory reads as offline: loose files from the data folder (the mod manager's virtual file
system is process wide, so they are the files the game sees) and archived files from its own archive tables, built
from the engine's archive set below. The engine's resolver (3.1) takes the registry lock exclusively for every read,
and the mesh reading pass reads every mesh on the task pool, which would starve the engine's own loading. Once the
pass and the TruePBR configs are done the archive tables are dropped and every later read (DDS metadata during a
patch, mod rules) goes through the engine's resource layer: resolve the path with the engine's resolver and read the
stream, exactly what the game loads for the path.

Texture enumeration follows the engine's archive rule, read from the engine's INI settings at runtime: the two
`sResourceArchiveList` lists, `Update.bsa`, then `<plugin>.bsa` and `<plugin> - Textures.bsa` in the engine's own
order (verified in the 1.7.99 database: the plugins of its built-in list, the masters and the Creation Club content,
then the active lines of `Plugins.txt` in file order, light and regular plugins interleaved), their directory tables
read with rsm-bsa for the mesh reading pass and dropped after it, then the loose files under `Data\textures`,
`Data\pbrnifpatcher` and `Data\meshes` (no hidden-file skip; the engine registers hidden files). Each file entry
stores its
path once and the archive it came from (needed by `vanillabsalist` and by mod attribution). Offline's broader
heuristic (`<name>N.bsa`, `<name> - Meshes.bsa`, `<name> - *.bsa`, INI lists from every `Data\*.ini`,
`BethesdaDirectory.cpp:585-674`) accepts archives the engine never loads; that is an offline bug, listed as a
follow-up and not changed by this design.

Classification is the offline one, `PGDirectory::mapFiles`: the mesh reading pass reads every mesh of the load order
on the task pool during the background initialization and casts the texture votes of its shader patchable shapes,
the votes decide the slot and type of every texture a mesh uses, a texture no mesh uses is classified by its file
name suffix (`PGNIFUtil::defaultsFromSuffix`), then the texture rules from the settings and the `vanillabsalist`
exclusion of height maps and environment masks inside the listed archives apply. The mesh allow list and block list
skip meshes as offline, and the pass marks the mod of every mesh it reads as having meshes (`hasMeshes`), which is
what `updateStateFromModlist` lists. The uses of a mesh come from the loaded game (`PGPluginRuntime`). Complex
material detection runs in the same pass over every environment mask, as offline runs it before
`updateStateFromModlist`: `PGD3D::checkIfCM` on PGLite's own D3D11 device with the compute shader compiled into the
binary, masks that fail classification dropped from the maps as offline does (`PGDirectory.cpp:305-308`).

The votes of every mesh and the classification of every mask are cached on disk by file identity (the modification
time and size of a loose file, the name, modification time and size of the archive of an archived one, the identity
the offline update cache uses) in `classification.bin` through the `PGRunObserver` calls the shared code makes
(`tryGetCachedMeshVotes`, `storeMeshVotes`, `tryGetCachedCMClassification`, `storeCMClassification`), so a session
reads only the meshes and masks that are new or changed, and the first session pays once. The file carries the PGLite
version that wrote it and is rebuilt by another version, since the votes depend on the code that casts them; when the
pass added entries it is rewritten with the entries of the current load order only, and the entries leave memory once
the pass saved them (an Apply reads the file again). While the pass runs, the loading line shows the meshes read so
far (4.10). Nothing classifies lazily, so the maps are immutable once the environment is ready and `texMatch` reads
them without locks.

### 4.6 Plugin uses from the loaded game

A runtime implementation of the `PGPlugin` interface replaces the Mutagen one and reproduces
`PGMutagen.GetModelElems` and `PopulateModelUses`: the 36 record types, `MODL` with alternate textures for the
modeled types, ARMO world models male and female, ARMA world and first person models per sex, destructible stage
models for the types that have them, the lower case model path with the `meshes\` prefix, alternate textures by 3D
index with the eight TXST paths (4.4), weighted registration under both suffixes when the ARMA weight slider flag is
set for that sex, the ignore flag from the form header, and single pass from the STAT material's MATO. Details that
make the runtime map equal the offline one:

- ADDN uses are enumerated with the `Unknown` type, as offline (`GetXEditTypeFromType` has no ADDN case,
  `PGMutagen.cs:1809-1848`), so the always-on record type filter always filters them and a mesh an ADDN shares with
  other records keeps an unpatched base path, exactly as `ignoreBaseMesh` does offline.
- Model strings are normalized the way the engine resolves them (`sub_140ECDC50`: an embedded `meshes\` is not
  doubled, forward slashes are accepted), decoded as Windows-1252, lower cased. Offline keys on the lower cased
  record string as written (`PGMutagen.cs:388`), which section 7 lists.
- Engine defaults are not uses: `TESClimate::InitializeData` sets `Sky\Stars.nif` on every climate, which Mutagen
  never sees; a CLMT night sky model equal to that default is skipped. The dump-and-compare check below finds any
  other default.
- The use ordering keys are the origin file name (`TESForm::GetFile(0)`) and the local form ID
  (`TESForm::GetLocalFormID`, 24 bits, 12 for light plugins), which is what Mutagen's `FormKey` gives offline.
- The ignore flag is bit 24 of `formFlags` (`kUnk24` in CommonLib); that it survives loading for all 36 types is
  verified in game.
- A facegen path that also has record uses is left unpatched with a log line, as offline (`PGPatcher.cpp:551-555`).
- Facegen heads get one stream per NPC form (4.1); the dummy use bypasses the record type filter and disables the
  TruePBR `nif_filter`, as offline.

The loaded form is what the engine uses, so record edits made at data load by other tools are seen (section 7). The
record type of a use is the loaded form's type, mapped to the same 36 signatures. The allowed record types setting
works as offline: the filter is applied per use with the use's record type (`PGPatcher.cpp:564-575` semantics), a
filtered use keeps the original, unpatched mesh, and the remaining uses get variants.

Verification: dump the runtime uses map (path, record, sub model, context) once and compare it with what the
offline run derives from the same load order, to confirm the loaded forms match Mutagen's winning overrides when no
runtime tool edits records, and that no engine default other than the climate one creates a use.

### 4.7 Conflict resolution

`PGModManager` is reused: MO2 (`modorganizer.ini`, profile `modlist.txt`, mod folders walked for the folders to map)
and Vortex (`vortex.deployment.json`). The file map it builds covers the mapped folders (the textures, the TruePBR
configs and the meshes). `modByFileSmart` resolves archived files through the file map's archive name. Mod shader
attribution comes from the texture classification, so `updateStateFromModlist` runs after it, as offline, and
`hasMeshes` comes from the mesh reading pass (4.5), so the mod list and the priority numbers are the offline ones. The
MO2 instance location is typed or picked with a folder dialog, as in the offline launcher.

### 4.8 Lifecycle and apply

Patching happens only when the engine opens a registered stream: cell attach behind loading screens, exterior
streaming as cells enter the grid, actors and equipment, facegen heads, effects, menu previews, and the engine's own
preloading. The engine's model cache serves repeat requests without a file load, buffered cells keep models resident
after detach, and once the last instance is gone the model is dropped; the next visit opens the stream again. The
patched state lives only in the engine's objects, so unloading needs no PGLite bookkeeping, and PGLite stores no
mesh bytes at all. The first in-game tests brought the two caches of 4.5 and 4.12; nothing else is cached.

The pipeline has one reader-writer lock. Every `DoOpen` holds it shared for the duration of its patch. Apply in the
GUI takes it exclusively on a worker thread while the game's main thread waits inside Present and presents the menu's
progress frames (in-flight patches finish first, new opens wait, no 3D is built meanwhile), then: restores every
record from the snapshot of 4.1 (the menu saved the settings before it started the apply), re-registers the
patchers, reads the texture rules and the mod rules again, rebuilds the environment (the file map, the mesh reading
pass from its cache, the TruePBR tables and the mod states), drops the plan table, recomputes the plan from the
restored records with fresh variant numbers, registers the new variant streams (re-registering an existing base path
replaces its stream, 3.1), retargets the records, and releases the lock. Restoring from the snapshot is what makes
Apply idempotent: indices are corrected once per plan, contexts are built from unmodified data, and no edit is
applied twice. The restore, the retargets and the record edits of a patch go through the snapshot, which remembers
every model string and alternate texture entry PGLite wrote: a value another plugin wrote since is kept, and a record
whose model string another plugin replaced gets no entry edits, so PGLite never undoes another plugin's edit (4.1).
Instances of
edited records exist at this point; the restore and the re-plan are plain pointer and integer sized stores made
while the main thread is parked, and a loader thread that applies a swap meanwhile (Appendix A) reads either the old
or the new value of an entry, both of which name a live texture set. A model that stays resident keeps the layout of
the old plan while its records carry the restored alternate texture entries until the first open of its family under
the new plan edits them again, so a new instance of such a model can show an alternate texture on the wrong shape
until the engine reloads it (section 7, item 14). A failed apply restores the records, installs an empty plan and
leaves PGLite disabled; its Enable button is the next attempt.

From then on every open uses the new settings. What does not change is what is already loaded: PGLite never modifies
loaded objects, so a model that is resident in the engine's cache (the current cell, buffered cells, equipped items,
loaded actors) keeps the bytes it was built from until the engine drops it, which happens when its last instance is
gone (leaving the area beyond the cell buffer, unequipping, cell purge, or loading a save). Variants of the new plan
have new names and are loaded fresh; the original path is the one name a resident model keeps. Returning to a place
after the engine evicted its models shows the new settings; standing in the same cell after apply does not. A
"reload the current cell's 3D" action (release and re-create the 3D of loaded references, reset actors) would make it
immediate and is a later addition if wanted; it is not part of apply.

### 4.9 Disable conditions

- An offline output is present: `ParallaxGen_Diff.json` in the load order (loose or archived) or `PGPatcher.esp`
  active. Offline checks only the loose `ParallaxGen_Diff.json`, and refuses to run while VRAMr's output mod is
  enabled (`main.cpp:600-612`), which has no runtime counterpart (section 7, item 19).
- Auto Parallax is installed (`AutoParallax.dll` in `SKSE\Plugins`): the two would edit the same materials.
- Address Library missing for the running game version.

In every case PGLite logs the reason, shows a notice ("PGLite is disabled because X.", translated like the menu; an
in-game message box after data load, a Windows message box at plugin load for the Address Library case, when the
game UI does not exist yet) and installs nothing else: no hooks, no menu, no streams, and the hotkey does nothing. A
failure after the hooks are installed (the data-load work or the background initialization) disables PGLite the same
way: every stream serves the original mesh (a failure of the data-load work also puts the records back), the hooks
stay, and the menu refuses to open.

### 4.10 GUI (Dear ImGui)

Hotkey from the ini. Device and context from the game's swap chain, present and input hooks chained like every other
ImGui plugin. Pages: mod manager (type, MO2 instance location with a folder dialog, MO2 loose file order) and the mod list
(mods with shaders or meshes as `updateStateFromModlist` numbers them, drag to reorder, enable, meshes ignored; apply
writes `modrules.json`); patcher checkboxes (Fix Mesh Lighting, Parallax, Complex Material, True PBR, Upgrade
Parallax to Complex Material, Disable Pre-Patched Materials, Fix Vanilla Subsurface Scattering, Hair Flow Map);
allowed record types; mesh allow list and block list; texture rules; a status panel (ready state, patched family and
variant counts, generated texture counts, a toggle that disables PGLite through an apply with an empty plan and
enables it through a full one, persisted in `settings.json`) and the warnings and errors collected by a log sink,
deduplicated (weighted pair mismatches land here). Apply refuses what the offline launcher refuses
(`PGParams::validate`, run on the rules spelled as a read of the file spells them, `PGParams::normalizeRules`: the
transform without its patchers, duplicate rule entries, two spellings of one rule among them; empty entries are
dropped by the menu, and the vanilla archive list, which the menu does not edit, only warns) and shows the first
problem in its row, every one in the log; disabling PGLite is never refused. The conflict manager page builds its mod
list from the mod manager settings as edited: while they are the applied ones the list is the environment's own mod
manager, attributed by the mesh reading pass; otherwise the mod
manager is read, the mods get their shaders from the classified textures of the current environment and their meshes
from the mesh reading pass (`PGDirectory::attributeMods`), and the shared ordering numbers them, so the list is the
one the next apply installs. The menu opens only once the runtime is ready. While the runtime initializes (and the
main menu waits, 4.1) a "Loading PGLite" line with the step, and the meshes read so far during the mesh reading pass,
shows in the top right corner; after the first start a welcome line with the menu key shows there until the menu was
opened once (recorded in `settings.json`); the ini can hide either line. The title bar carries
the icon of the offline launcher. The strings come from the
offline translation files through PGLib's PGTranslation (the offline PGLocale wraps it for wxWidgets); the plugin
ships the files under `SKSE\Plugins\PGLite\translations`, its Settings page picks the language (a working copy
applied with Apply, like every other setting; the tabs keep their identity across the label change), and the font atlas
is rebuilt with the glyphs of the loaded translation, a CJK font merged in when it needs one. No conflict highlighting or viewer, as
specified.

### 4.11 Files

- `Data\SKSE\Plugins\PGLite.ini`: hotkey, log level, exclude facegens, disable dynamic cubemap, force always CM,
  loading and welcome messages.
- `Data\SKSE\Plugins\PGLite\settings.json`: the offline `params.*` keys and nesting, so settings can be copied
  between the two programs, and as there the file holds every parameter with its full lists, the defaults applying
  only when there is no file, and a `pglite` object of PGLite's own (`enabled`, `language`, `didOpenMenu`). The
  serialized
  keys are never renamed.
- `Data\SKSE\Plugins\PGLite\modrules.json`: the offline schema.
- `Data\SKSE\Plugins\PGLite\classification.bin`: the classification cache (4.5), the texture votes of every mesh and
  the complex material classification of every environment mask by file identity.
- `Data\SKSE\Plugins\PGLite\cache\`: the generated textures with their key files (4.12). Both caches can be deleted
  at any time.
- `Data\LightPlacer\PGLite_Variants.json`: the Light Placer config of the variant paths (4.13), rewritten at data
  load and at every Apply.
- `My Games\Skyrim Special Edition\SKSE\PGLite.log` through spdlog and the Logger facade.
- The package ships `textures\cubemaps\dynamic1pxcubemap_black.dds`, which the offline run deploys into its output.

### 4.12 Generated textures

The Parallax to Complex Material transform and Fix SSS are the two patchers that produce textures. Offline, the mesh
patcher registers the source with a texture hook (`PatcherTextureHookConvertToCM`, `PatcherTextureHookFixSSS`),
points the slot at the hook's output name, and the texture phase after every mesh writes the file into the output
folder. At runtime the same hooks run, and the registered textures are made the moment they are first needed:

- `Runtime::bytesFor` drains the hooks' process lists right after a family was patched, before the bytes reach the
  engine, so the output path is registered before the engine parses the mesh that names it. `TextureGenerator` runs
  one caller at a time; a texture is made once per session, and one that fails is logged once and stays missing, as
  offline.
- The source is read through the engine's resource layer (the bytes the game itself loads for the path), decoded
  with DirectXTex, run through the hook's `generate()` (the compute shader and the block compression of the offline
  output, unchanged) and serialized with `SaveToDDSMemory`.
- The result is kept under `Data\SKSE\Plugins\PGLite\cache\<output path>` with a `.key` file next to it. The key
  holds everything the output depends on: the hook, the signature of its generation (a CRC32 of the shader bytecode,
  the formats and the parameters), the DirectXTex version, the PGLite version, the source path, and the size and
  CRC32 of the source bytes; the size of the output is appended once the file is written. A texture is taken from the
  cache only when the key matches exactly and the file has the recorded size; anything else generates it again. The
  texture is written before its key, each through a temporary file and a rename, so a crash in between leaves a stale
  key and never a stale texture. The cache lives outside `Data\textures`, so the file map never sees it, and it can
  be deleted at any time.
- The engine reads the output through a `PatchStream` registered for the output path, whose provider is the
  generator and serves the cache file at every open. The path is registered in the resource registry like a mesh
  (3.1): a file of the same name in the load order is shadowed, as the offline output folder shadows it.
- As offline, a generated texture is not a texture map entry: `setTextureType` records its type for the type
  lookups, and `addGeneratedFile`, done by the hook when a mesh registers the source, answers the existence checks.
  Apply replaces the directory, so the generator gives the new one the file and the type of every texture of the
  session and empties the hooks' lists; the first patch after Apply registers its textures with the new directory.
- The ini flag `bForceAlwaysCM` is the offline `--force-always-cm`: every parallax match is upgraded, not only the
  ones whose shape cannot keep parallax.

### 4.13 Light Placer

Light Placer reads its JSON configs under `Data\LightPlacer` at its plugin load (`std::filesystem`, so through the
mod manager's virtual file system and never through the engine's resource layer), builds its tables at data load,
and attaches the lights of a `models` entry by the model string of the record whose 3D loads, compared case
insensitively and otherwise literally (`LightManager::AttachLightsImpl`, `SourceData`). A record PGLite points at a
variant path has another model string, so its lights would be lost; offline, `HandlerLightPlacerTracker` appends
every duplicate's path to the entries that list the original and writes the changed configs to the output.

`LightPlacerBridge` is the runtime counterpart. After the plan is built, at data load and at every Apply, it reads
the configs as Light Placer does (the same folder walk, the same parser rules through the handler's
`parseLightPlacerJSON`), and for every `models` entry that lists a model of a family with variants (the model strings
compared lower case with backslashes, as the offline handler compares them) writes one entry with the same `lights`
and the variant strings as the records hold them (without the `meshes\` prefix) into
`Data\LightPlacer\PGLite_Variants.json`, deleting the file when there is nothing to write. Light Placer has no API,
but its `ReloadLP` console command reads every config again and rebuilds its tables, so the bridge runs it through a
`Script` form on the main thread once the data load (or Apply) is done. The file also stays on disk, so the next
session's Light Placer reads it at its plugin load; a stale entry names a variant path that embeds the original mesh
path and can only ever carry that mesh's lights. Nothing happens when `po3_LightPlacer.dll` is not loaded.

## 5. Shared code: PGLib changes

Prerequisite: the `featalphaprop` branch (copy-on-write texture sets and alpha properties in `PGNIFUtil`, which
removes the per-path static texture set cache of `PatcherMesh`) is merged before implementation starts, so two
families can be patched on two threads without shared state.

All of the following are behavior preserving, covered by unit tests as `CLAUDE.md` requires, and the offline output
must stay byte identical on a reference load order.

1. Split `PGPatcher.cpp`: the per-mesh pipeline (`processNIF`, `processNIFShape`, `matches`, `sortMatches`,
   `applyTransformIfNeeded`, `createNIFPatcherObjects`, patcher registry) moves to a shared unit; the offline runner
   (`patchMeshes`, `patchNIF`, `replayNIF`, output directory, diff JSON, textures) stays offline. The miniz include
   (`PGPatcher.hpp:17`) goes with the runner.
2. `PGPlugin` becomes an interface with the Mutagen implementation and the runtime one of 4.6. The header already
   has no Mutagen dependency.
3. `PGMeshPermutationTracker` gets a save mode that returns the serialized bytes per output, with the same sorting
   and index corrections as data, instead of queuing files, so the runtime uses the same staging, comparison and
   ordering logic. The weighted pair check is split into the block type signature comparison, which the runtime
   uses (offline keeps its `compareMesh` check, which decides the same), and the offline bookkeeping that keeps
   NifFile copies until the counterpart arrives (`processWeightVariant`, `PGMeshPermutationTracker.cpp:328-352`),
   which the runtime does not use. A commit can skip the comparison with the original, so the runtime serves the
   bytes of a first patch again.
4. `PGDirectory` and `BethesdaDirectory`: a read hook (`setFileReader`, the engine-backed reads of 4.5 at runtime), an
   injected archive load order, the archive tables droppable after the mesh reading pass, and the mod attribution
   replayable onto another mod manager (`attributeMods`, for the menu's mod list).
5. The update cache recording calls in `PGDirectory`, `PGNIFUtil::texMatch`, `PGModManager`, `matches()`, `PGD3D`
   (`SuspendRecording`) and `saveMeshes` (`recordOutputFile`) go behind a small observer interface that `PGRunCache`
   implements offline, so `PGRunCache` (which pulls in the permutation tracker and the texture hooks) is not compiled
   into the DLL.
6. `PGConfig`'s parameter struct and JSON (de)serialization move to PGLib without the wxWidgets-only parts.
7. `PGD3D::initShader` loads compiled bytecode: the four shaders are compiled at build time with `fxc` and embedded.
   This removes the `cshaders` folder and the `D3DCompiler` dependency from both programs.
8. `StringUtil` replaces its four `boost::locale::conv` calls with the Win32 conversion functions, so boost.locale
   is not linked into the DLL.
9. `TaskQueue` and `TaskPoolRunner` lose cpptrace and boost.asio, and the `ExceptionHandler` latch that stops all
   work stays offline behind a task guard seam (`ExceptionHandler::setTaskGuard`): the offline programs install the
   cpptrace guard, which records the exception and stops every runner, and the runtime installs a guard that logs
   it, so one failed task does not stop a batch. `PGDirectory.cpp`'s `boost/asio.hpp` include goes with it, so no
   winsock initialization runs in the DLL.
10. The texture hooks report through `PGRunObserver` (`recordHookRegistration`, `tryReuseHookOutput`,
    `recordHookOutput`; `HookKind` lives there and `PGRunCache` forwards), and each hook splits its generation into
    `generate()` (the shader and the compression, in memory) and the write of `applyPatch()`, and exposes its process
    list and the signature of its generation, so the runtime makes and caches the same textures (4.12).

## 6. Build and packaging

```
PGLite/
  CMakeLists.txt        standalone configure: x64-windows-static, C++23 (CommonLibSSE-NG), /MT
  vcpkg.json            own manifest and baseline
  package/              SKSE/Plugins/PGLite.ini, textures/cubemaps/dynamic1pxcubemap_black.dds
  include/ src/         entry point, resource streams and registration, runtime environment, permutation plan, GUI
external/CommonLibSSE-NG   submodule (alandtse, ng branch): tests off, SE and AE on, VR off
```

The root configure uses a dynamic CRT, C++20 and builds PGLib as a DLL; a configure has one triplet, so PGLite is a
second configure that compiles the shared PGLib sources a second time as a static library. `buildRelease.ps1` builds
both and produces `PGLite.zip` as the second release artifact; `build.yml` gets a job that runs
`check_build_warnings.py` on the PGLite log as well. Statically linked: CommonLibSSE-NG, nifly, spdlog, nlohmann-json,
imgui (the dx11 backend), simpleini, DirectXTex (CPU side, DDS headers and `PGD3D`), rsm-bsa with lz4 and zlib (the
directory tables and the archived meshes of the mesh reading pass), the header-only boost parts the shared code uses.
Not linked, after the changes of section 5: wxWidgets, CLI11, miniz, cpptrace, boost.asio, boost.locale, the .NET
host, json-schema-validator. WebKit style and the review checklist apply; `run_clang_tidy.py` gets the PGLite compile
commands. No unit tests for the PGLite module itself, as specified.

## 7. Differences from the offline patcher

This is the complete list. A difference that is not here is a bug (section 1).

**Patchers and textures**

1. Texture generation happens at first use and goes to a cache. The Parallax to Complex Material transform and Fix
   SSS make the same textures as offline (same shaders, formats and compression), but a texture is generated the
   moment the first patched mesh that names it is produced, from the bytes the engine serves for its source, and is
   kept under `SKSE\Plugins\PGLite\cache` (4.12) instead of the output folder; the engine reads it through a
   registered stream. Offline generates every registered texture after all meshes, in the texture phase.
2. The texture votes of the mesh reading pass and the complex material classifications are cached across sessions
   by file identity in `SKSE\Plugins\PGLite\classification.bin` (4.5); offline keeps them in the update cache of the
   output folder, and only when the output is updated. The classification itself is the same code
   (`PGDirectory::mapFiles`).
3. No pgtools-only patchers (Particle Lights to Light Placer, Convert to HDR) and no `--consider-allmeshes`: only
   meshes with record uses and facegen heads are patched, which is the GUI program's behavior.

**Records and the plugin step**

4. Records are the live runtime state. Tools that edit records in a data-load handler that runs before PGLite's
   (SKSE dispatches the handlers in plugin load order) are seen; a handler that runs after it, or a script, that
   changes a model string keeps its value, since PGLite never overwrites a value it did not write (4.8): such a
   record loads what its string names, patched as the plan of that path says, and PGLite's own plan for the record
   is not applied. Tools that swap base objects per reference (Base Object Swapper, Seasons of Skyrim) make the
   engine load another record's model, which has its own uses. The offline run sees the plugin files (4.6 has the
   check that the two agree when no runtime tool edits records).
5. Light Placer gets the lights of its model entries on the variant paths through a config PGLite writes and a
   reload it asks for (4.13), where offline the handler patcher adds every duplicate's path to the configs
   themselves (`HandlerLightPlacerTracker`, `PGPatcher.cpp:636-639`); within a session this depends on Light
   Placer's `ReloadLP` console command. Any other mod that keys on model paths loses its effect on records that
   PGLite retargets to a variant path. Variant names also use a session counter instead of the offline `1..N` per
   path (4.4); only path-keyed mods can tell.
6. Texture sets are in-memory objects: the eight paths only, as the offline records (`PGMutagen.cs:1230-1241`), but
   with no form ID, no `PG_` editor ID, no plugin record and no persistence, created at first open instead of at the
   end of the run, and deduplicated per session instead of per run. CS's `PBRTextureSets` registry knows neither
   kind, so the result is the same.
7. Weighted pairs are planned as one family (4.4): a context merges only when both files agree, and each record
   component is edited once. Offline numbers each file's duplicates separately, relies on the layout check to flag a
   divergence, and applies the index corrections once per file of the pair (Appendix B). Where offline would log a
   mismatch and leave the record pointing at meshes of two contexts, the runtime logs the same mismatch and never
   does. A use that is not weighted but names one file of a pair is patched on the other file too, the "meshes
   ignored" rule of a pair follows the first file's mod, where offline decides each file on its own, and a pair whose
   other file the engine cannot resolve is patched on the file it can.
8. Model strings are normalized as the engine resolves them (4.6). Offline keys on the lower cased record string as
   written (`PGMutagen.cs:388`), so a record whose model string uses forward slashes or carries its own `meshes\`
   prefix is unpatched offline and patched at runtime.
9. Facegen heads are enumerated from NPC forms, not from the file map: a facegen file for an NPC that is not in the
   load order is patched offline and never considered at runtime, where it is never loaded either.
10. A CLMT night sky model equal to the engine default `Sky\Stars.nif` is not a use (4.6); offline never sees it,
    and a plugin that sets exactly that string is the one case the two disagree on.

**Files and archives**

11. Nothing of the offline output is written to the data folder: no duplicate files, no plugin files, no texture set
    plugin records, no diff JSON, no zip, no update cache, no BodySlide ShapeData handling, no dynamic cubemap
    deployment (the package ships the texture). PGLite's own files live under `SKSE\Plugins\PGLite` (the settings,
    the mod rules, the classification cache and the generated textures of 4.12), and the Light Placer config of item 5
    under `LightPlacer`.
12. The archive set and the loose file set are the engine's (4.5), in the engine's own order: the INI lists,
    `Update.bsa`, `<plugin>.bsa` and `<plugin> - Textures.bsa` of the built-in plugins and then of the active lines of
    `Plugins.txt`, every loose file. Offline also reads `<name>N.bsa`, `<name> - Meshes.bsa` and `<name> - *.bsa`, INI
    lists from every `Data\*.ini`, and skips hidden files; those are offline bugs left to a follow-up, and the runtime
    does not reproduce them.
13. Input bytes, the texture metadata of a patch and the sources of the generated textures come from the engine's
    resource layer, so they are what the game loads; the mesh reading pass and the TruePBR configs read the data
    folder and the archives natively before the first patch (4.5), with the archive set and order of item 12, and the
    complex material classification reads a loose environment mask natively. Offline reads the data folder and
    archives itself. With the archive set of item 12 the bytes are the same.

**Lifecycle**

14. Settings changes apply to streams opened afterwards; models already resident in the engine keep the bytes they
    were built from until the engine evicts them (4.8), and a new instance of such a model can show an alternate
    texture on the wrong shape until then, since its records carry the entries of the new plan. A generated
    texture's stream stays registered for the session, whatever a later Apply enables, and a texture the engine
    loaded before the generation stays in its cache. Every Apply registers the variant streams of its plan, and the
    registry keeps them all for the session (3.1); a path of an earlier plan serves the original mesh. Offline, a
    run rewrites every output.
15. Duplicates in memory that offline does not have: contexts of a family that merge offline load as separate
    models until the main-thread retarget runs (4.4), a model instanced from a merged-away name stays until evicted,
    and a path ignored by a mod rule holds one copy per context because the rule is applied at first open rather
    than at planning (4.4). All copies are byte identical; the waste is memory only.
16. A failure while patching a family (exception, nifly failure) leaves that family unpatched, the original bytes
    served and one log line written, and the session continues. Offline, an exception stops the run
    (`ExceptionHandler`).
17. The first open of a family runs on the requesting thread (4.3), the main thread included; offline patches on a
    worker pool.

**Program behavior**

18. The offline DynDOLOD refusal has no runtime equivalent and is not needed: `.bto` files have no uses and the
    default block list keeps LOD and DynDOLOD meshes out.
19. PGLite disables itself with a notice when an offline output or Auto Parallax is present (4.9); offline checks
    only the loose diff JSON, and its refusal to run while VRAMr's output mod is enabled has no runtime counterpart.
20. GUI and settings: no conflict highlighting or viewer; `exclude facegens`, `disable dynamic cubemap` and `force
    always CM` are ini options instead of command line flags; Apply restores the records from a snapshot before
    re-planning (4.8); the log lives next to the other SKSE logs, so the `enabledebuglogging` and
    `enabletracelogging` keys of `settings.json` are ignored in favor of the ini's log level; `multithread` and
    `devmode` are kept from the file but not editable in the menu; a relative mod manager path in `settings.json` is
    resolved against the game folder, where offline resolves it against its own folder; a `settings.json` edited by
    hand runs as it is, with the patchers it names (the transform without its patchers is turned off with an error,
    where the offline launcher refuses to run); a shader that cannot be created turns its patcher off with an error,
    where offline refuses to run.

Explicitly the same: block order of the patched mesh (sorted as offline), the weighted `_0`/`_1` layout check, the
allowed record types filter including the ADDN rule, the mesh block list and allow list, `PG_IGNORE` extra data,
`PG_STOCK` restore, Fix Texture Slot Count, facegen patched for a dummy use with the TruePBR `nif_filter` disabled
and a facegen path with record uses left unpatched, skin tint shapes ignoring alternate textures, the use ordering
that picks which use keeps the original path, the `ignoreBaseMesh` rule, TruePBR config order (file map order), the
dynamic cubemap texture, the `vanillabsalist` rule, mod attribution and priority semantics (`hasMeshes` from the
mesh reading pass gives the same mod list), the complex material classification thresholds (same shader, classified before
the mod states as offline, cached across sessions), alternate texture handling (3D index matching, eight paths in
NIF slot order, eight empty strings for an unresolved set, the case rules of the dedupe), index corrections with -1
for deleted shapes, the duplicate mechanism and its use ordering, and every shader, flag, slot, field and geometry
edit the patchers make.

## 8. Implementation items to verify in the binary or in game

1. **Address Library IDs** for the two engine functions PGLite calls that CommonLib does not bind: the register-path
   function (AE `sub_140ECEE30`, ID 69845; SE 1.5.97 `sub_140C45280`, ID 68491) and the resolver (AE
   `sub_140ECE860`, ID 69839; SE `sub_140C44CB0`, ID 68484; the resolver call inside the bound
   `BSResourceNiBinaryStream::ctor`, so it can also be found by following that call), verified for 1.6.1170 and
   1.7.x (Address Library format 5) and in the 1.5.97 database (format 1, `version-1-5-97-0.bin`). The input
   dispatch call sits at offset 0x7B in 1.5.97 as well. Not yet run in game on 1.5.97.
2. **Stream contract in game.** A PGLite stream satisfies the loader (open, `totalSize`, read, seek, close), the
   per-entry single-open rule holds for queued and asynchronous loads as it does for synchronous ones, no model path
   requests an asynchronous stream (`DoCreateAsync` is unsupported), `DoClone` is what direct opens receive, and the
   `StreamBase` flags offset is read through CommonLib's accessor on both SE and AE.
3. **Registration timing.** Everything registered at data load wins over records registered at startup (3.1 insert
   semantics); the file cache validation that also registers paths (`sub_140ED9240`) runs only at startup; archive
   registration (`ArchiveRegistrationTaskData`) is complete by `kDataLoaded`; an entry created before data load
   keeps its old stream, so confirm that only meshes without uses are demanded before then.
4. **Record edits at runtime.** Creating a `BGSTextureSet` through the form factory on a loader thread touches no
   unsynchronized global state (otherwise the texture sets are created at data load for every alternate texture
   entry and the unchanged ones are pointed back at their original set at first open); index -1 is skipped by every
   swap path; `TESModel::model` writes at data load and in the main-thread retarget task are safe; the engine's
   weighted suffix derivation works on a variant name; the ignore flag bit survives loading for all 36 types.
5. **Uses map equality.** The dump-and-compare of 4.6, including engine defaults, Windows-1252 decoding, origin file
   and local form ID ordering, and the ADDN rule.
6. **Startup gating.** The main menu waits for the initialization (4.1): the file map, the mesh reading pass, the
   classification and the MO2 folder walk. Measured on the vanilla load order with a few mods: 2.8 s with a cold
   classification cache, 1.1 s with a warm one.
7. **Complex material classification in process:** device creation inside the game process next to ENB and ReShade
   proxies, the shader embedding, the disk cache identity, and the cost of the first session's full pass.
8. **Memory.** Plans not bytes, the merge of identical results, the file map with paths stored once
   and no archive objects, the per-path stream objects (about a hundred bytes each plus a 40-byte registry record),
   and the duplicates of section 7 keep PGLite's footprint small; measure on a large load order.
9. **Performance** is evaluated after the first in-game tests, not designed for up front; the first-open hitch on
   synchronous demands (4.3) is the known candidate.

## 9. Milestones

1. PGLib seam work (section 5, after `featalphaprop` is merged) with tests; offline output byte identical on a
   reference load order.
2. PGLite scaffold: configure, submodule, entry point, logging, ini, disable conditions with notices, the stream
   class and registration serving unchanged bytes, release packaging and CI job. Deploy to the MO2 test mod folder.
3. Runtime patching: uses map and record snapshot, texture map through the engine's resource layer, classification
   with its cache, TruePBR configs, mod manager, the data-load plan with retargets and registration, the full family
   patch with the permutation system (contexts, merge, record edits, TXST objects, index corrections, weighted
   pairs).
4. ImGui menu with all pages and apply with snapshot restore.
5. In-game testing, performance evaluation, and the differences list in the README.

## 10. Working assumptions

Defaults the design proceeds with unless overruled:

1. The offline classification (the mesh reading pass and the suffix rules), with the per-mesh vote cache of 4.5.
2. Complex material detection through `PGD3D` on PGLite's own device with embedded shaders, during initialization,
   with the disk cache of 4.5.
3. Apply affects the next load only; no forced scene refresh.
4. The GUI reads the offline translation files, with a language setting in the menu.
5. Supported runtimes 1.5.97, 1.6.x and 1.7.x, one build through the Address Library of the installed version; no
   VR.
6. No code hooks for file loading: registered streams only. The two engine functions of 8.1 are called, not hooked.
7. Texture sets are created on the requesting thread at first open and left unregistered; data-load pre-creation is
   the fallback of 8.4.

## Appendix A: reverse engineering notes (SkyrimSE_1.7.99)

Resource layer: `ID::GenerateFromPath` `sub_140ED3EE0` (wrapper `sub_140ED3C20(ID*, path)`); registry
`qword_14331B4C0` (shared_mutex at +0, tables at +16, level count at +44), lookup `sub_140ECADC0`, find record
`sub_140EC84A0`, record to stream `sub_140EC8570` (loose: shared pointer copy; archive: `ArchiveStream` or
`CompressedArchiveStream`, name copied from the record), ID to stream `sub_140EC88B0` and `sub_140EC8800`, insert or
replace `sub_140EC8170`, record constructor `sub_140EC9A60` (ID, zeroed flags and size, `DoGetName` into +24, stream
at +32), register path with stream `sub_140ECEE30`, path to stream resolver `sub_140ECE860` (registry, clone for
loose records, then `LocationTree::DoCreateStream` `sub_140ED1450`), lookup contexts with the registry lock
`sub_140EC7C10`/`sub_140EC7C90` (both `std::scoped_lock<std::shared_mutex>`). Startup registration `sub_14065DBE0`
(INI lists through `sub_140ECF2E0`, `Update.bsa` through `sub_140ECF080` to `sub_140EC5A90`, per plugin
`sub_140ECED50` and `sub_1401C4CB0`, loose files through `RegisterPrefix` `sub_140ECF570(prefix, root, Location*)`
or its list form `sub_140ECF670`, the directory list or all of `data\` depending on one INI switch
`byte_1420B6070`). `RegisterPrefix` builds a `LocationTree` over the given location and `GlobalPaths`
(`qword_14331B550`) and a 240-byte `Traverser` (vtable 0x141A44E28, `ProcessName` = `sub_140ED2AF0`: ID of the
name after the root string via `sub_140ECF790`, `location->DoCreateStream`, insert, then the registration event
dispatch `sub_140ED2860` over the listeners at `qword_14331B538 + 16`). `GlobalLocations` singleton
`qword_14331B558`, `RegisterLocation` 0x140ECE210 to `sub_140ED2130` (priority list, `DoMount`),
`GlobalLocations::DoCreateStream` `sub_140ED1290` (iterate locations). Loose files: `LooseFileLocation` vtable
0x1417FF6E0 (`DoCreateStream` `sub_140ED4850`, `DoTraversePrefix` `sub_140ED4A60` with the recursive walk
`sub_140ED6E90`, which passes names relative to the location root and registers files with the normal or archive
attribute). Stream vtables: `Stream` 0x141A43C00, `ArchiveStream` 0x141A43C68, `CompressedArchiveStream`
0x141A43CD8, `LooseFileStream` 0x141A453D8; `Location` 0x1417FF5B8 (12 slots, CommonLib order). Archive names are
kept only for archives with BSA flag 0x10 or those in `sResourceArchiveRetainFileNameList`.

Model loading: `BSModelDB::Demand` 0x140FC9060 -> `sub_140FC8D80` (prefixes `meshes\` through `sub_140ECDC50`, null
path into the entry database, entry database at `qword_14219DEE8 + 208`) -> entry lookup `sub_14020BD20(db, &id,
&entry, create)` (miss: registry `sub_140EC84A0`, entry `sub_14020B450`, stream `sub_140EC8570`) -> stream for the
load `sub_140152E60` (the entry's stream, else the registry by the entry's ID, else the location's `DoCreateStream`
with a path) -> loader `sub_140FC8480` (callers: synchronous `sub_14020B970`, asynchronous worker `sub_140152B50`,
`sub_14020BED0`). `sub_140F01090(this, BSTSmartPointer<Stream>*, bool, bool)` is the `BSResourceNiBinaryStream`
constructor from a stream (flags |= 8 or 14, `DoOpen`, buffer of `totalSize + 56`, read function `sub_140F016B0`
calling `DoRead` or the buffered `sub_140F01CB0`); the path constructor is `BSResourceNiBinaryStream__ctor`
0x140F00F80 (resolver, flags |= 0xA, `DoOpen`); the destructor 0x140F01260 clears the flags, calls `DoClose` and
releases. `sub_140EE3720` is the `NiStream` constructor (vtable 0x141A47168) followed by the `BSStream` vtable
(0x1417EEDC8), `sub_140FD12D0` is `BSStream::Load(this, path, stream)` (copies a non-null path to +0x108, calls
`sub_140EE40A0`: `good()` check, stream at +0x298, vfunc 15), the error model name lives in `qword_14331F590`,
`qword_1421A3780` is the `TESModelDB::TESProcessor` singleton (vtable 0x1417F75F0, `PostCreate` at slot 1).
`sub_1403C2EC0` is the NPC head loader (formats the FaceGeom path through `sub_1403C2E00`, demands `.p` and `.nif`
through `sub_140FC8D80`). `MistMenu::Show` 0x1409654B0 demands the loading screen model synchronously when no TES
exists. Atmosphere and clouds load through the model database (`sub_14040B740`, `sub_14040BFD0`).

Records: `TESModelTextureSwap::ApplySwap` `sub_1401ED800` -> `sub_1401EE0E0` skips entries with index -1, has an
interlocked reentrancy counter and is also reached from `QueuedAnimationObject` (vtable slot 20 -> `sub_140194B70`
-> `sub_1407D8340` -> `sub_14021C5F0`), so swaps are not main-thread only. `BGSTextureSet::GetTexturePath` and
`SetTexturePath` (`sub_14032D880`, `sub_14032DA00`, vfuncs 0x25 and 0x27) permute NIF slots 2 to 5 onto record
indices 3, 4, 5, 2; `GetTexture` `sub_14032D910` prepends `Data\Textures\`. `TESClimate::InitializeData`
`sub_1402A5C30` sets `Sky\Stars.nif`. `BSShaderProperty` flag word at offset 0x38 (CommonLib).

## Appendix B: offline facts that constrain the runtime (verified in the repository)

Alternate textures are matched by 3D index counted over `NiShape` and `NiParticleSystem` blocks in tree order
(`PGNIFUtil.cpp:516-542`); the MODS name is never used. For an alternate texture shape the NIF keeps the vanilla
texture set and gets the patched shader state, the patched paths go to a new TXST (`PGPatcher.cpp:773-779`,
`886-892`; `PGMutagen.cs:1142-1255`), which carries the eight paths and a `PG_` editor ID only, deduplicated across
the run by its eight strings (`PGMutagen.cs:1215-1255`). Record ignore is header flag 0x01000000
(`PGMutagen.cs:917-918`). The allowed record type filter is applied in C++ only and is always on
(`PGPatcher.cpp:564-575`, `main.cpp:829-835`); ADDN uses carry the `Unknown` type and are therefore always filtered.
"Meshes ignored" is decided per path from the mod that owns the NIF (`PGPatcher.cpp:493-503`). A facegen path with
record uses is left unpatched (`PGPatcher.cpp:551-555`). Model strings are keyed lower cased as written
(`PGMutagen.cs:388`). Single pass comes from the STAT material's MATO flags and only disables parallax and restricts
complex material (`PGMutagen.cs:901-906`, `PatcherMeshShaderVanillaParallax.cpp:58`,
`PatcherMeshShaderComplexMaterial.cpp:101-106`). Weighted ARMA models are registered under both `_0` and `_1`
(`PGMutagen.cs:398-446`), and the two outputs of a pair are compared by block types only
(`PGMeshPermutationTracker.cpp:297-352`). The offline save sorts blocks and reports index corrections to the plugin,
-1 for removed shapes (`PGMeshPermutationTracker.cpp:198-234`). TXST paths are handled through Mutagen's named
fields, which is why the offline code is unaffected by the engine's record order (`PGMutagen.cs:1232-1239`,
`1751-1758`); the NIF's ninth slot never reaches a plugin. The review reports that `SetModelUses` runs once per file
of a weighted pair against the same accumulated record copy with `altTexDict` keyed by the original slot IDs
(`PGMutagen.cs:1097-1171`), so a deleted shape can be re-mapped twice offline; that is a PGLib follow-up to confirm
and fix, not part of this design, and the runtime edits each record component once (4.4). An exception in a patch
stops the offline run through `ExceptionHandler`. `pbrtexturesets` appears only in the output cleanup list
(`PGPatcher.cpp:306`); nothing writes it.
