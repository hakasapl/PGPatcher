# PGLite

PGLite is the SKSE plugin version of PGPatcher: it runs the same patchers on every mesh at the moment the game loads
it, and reproduces the plugin step (mesh duplicates and texture set records) in memory. Nothing of the offline output
is written to the data folder, there is no output mod and no patching step before the game starts; PGLite keeps its
own files under `SKSE\Plugins\PGLite`. The design is in [DESIGN.md](DESIGN.md).

## Installation

Install the `PGLite` archive like any SKSE plugin (`SKSE\Plugins\PGLite.dll` with its `PGLite.pdb` for crash logs,
`SKSE\Plugins\PGLite.ini`, the translation files under `SKSE\Plugins\PGLite\translations` and the dynamic cubemap
texture). Requirements:

- Skyrim Special Edition 1.6.x or 1.7.x (the Anniversary Edition runtimes), SKSE and the
  [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for the installed game version.
  Skyrim SE 1.5.97 is not supported yet: the two engine functions PGLite calls are not known there, and PGLite stays
  disabled with a notice.
- No offline PGPatcher output in the load order (`ParallaxGen_Diff.json` or `PGPatcher.esp`) and no Auto Parallax.
  PGLite disables itself with an in-game notice when it finds one of them, and installs nothing else then (no menu,
  no hooks).
- Community Shaders for complex material and True PBR, as with the offline program.
- The Visual C++ 2015-2022 redistributable the game itself needs (`vcomp140.dll`) and `d3dcompiler_47.dll`,
  which is part of Windows; everything else is linked into `PGLite.dll`.

The menu opens with the key set in `PGLite.ini` (`iMenuKey`, a DirectInput scan code, F8 by default). Its status page
has a toggle that disables PGLite (meshes loaded from then on are the originals) and enables it again, kept across
game starts. After the game data loaded, the game waits until PGLite is ready before the main menu opens; "Loading
PGLite", the current step and the meshes read so far show in the top right corner meanwhile
(`bShowLoadingMessage=false` in the ini hides the line, not the wait), and after the first start a welcome line with
the menu key shows there until the menu was opened once (`bShowWelcomeMessage=false` hides it for good). The menu's strings come from the translation
files of the offline launcher, and its Settings page picks the language. Settings live
in `SKSE\Plugins\PGLite\settings.json` with the same keys as the offline `params`, so a settings file can be copied
between the two programs, and the mod rules in `SKSE\Plugins\PGLite\modrules.json`. The log is
`My Games\Skyrim Special Edition\SKSE\PGLite.log`.

## How it works

At data load PGLite reads every record model of the loaded game, plans the variants of meshes that several records
use with different alternate textures or material contexts, points those records at virtual paths under
`meshes\_pgpatcher_dups\`, and registers one resource stream per path with the engine. Opening such a stream patches
the mesh (every context of it at once), corrects the alternate texture indices of its records and creates the texture
set records the patched alternate textures need. The file map, the mesh reading pass that classifies the textures by
the shapes that use them (every mesh of the load order is read, and the result is kept per mesh in
`SKSE\Plugins\PGLite\classification.bin`, so a later start reads only the meshes that changed), the complex material
detection (on the GPU, cached in the same file), the True PBR configs and the mod manager data are built on a
background thread right after data load; the loading line in the top right corner counts the meshes read.

The Parallax to Complex Material transform and Fix SSS generate their textures the first time a patched mesh needs
them, from the texture the game itself would load, and keep them in `SKSE\Plugins\PGLite\cache`. A cached texture is
used again only while its source texture, the shader and the PGLite version are unchanged; the folder can be deleted at
any time. `bForceAlwaysCM=true` in the ini upgrades every parallax match (the `--force-always-cm` flag of the offline
program).

Apply in the menu saves the settings, restores every record, plans again and registers the new variant streams.
The game is paused meanwhile, and the menu shows the elapsed time and the step it is at. Apply refuses the settings
the offline launcher refuses (the transform without its patchers, empty or duplicate rule entries) and says why.
Meshes loaded from then on use the new settings; models the game already holds keep their state until the game
reloads them. A model string another plugin sets after PGLite planned is never overwritten, at data load or by
Apply.

## Differences from the offline PGPatcher

Everything not listed here behaves as the offline program does (block order, the weighted `_0`/`_1` layout check,
the allowed record types with the ADDN rule, mesh block and allow lists, `PG_IGNORE`, `PG_STOCK`, facegen heads,
skin tint shapes, the use ordering and the `ignoreBaseMesh` rule, True PBR config order, the vanilla archive list,
mod attribution and priorities, alternate texture handling, index corrections and every shader, slot and geometry
edit of the patchers).

Patchers and textures:

1. The Parallax to Complex Material transform and Fix SSS generate their textures the first time a patched mesh
   needs them, from the texture the game would load, into `SKSE\Plugins\PGLite\cache` instead of the output folder;
   the engine reads them from there. The images are the ones the offline program writes.
2. The texture votes of the meshes and the complex material classifications are cached across game starts in
   `SKSE\Plugins\PGLite\classification.bin`; offline keeps them in the update cache of the output folder. The
   classification itself is the same.
3. No pgtools-only patchers (Particle Lights to Light Placer, Convert to HDR) and no `--consider-allmeshes`.

Records and the plugin step:

4. Records are the live game state: edits made by plugins whose data-load handler runs before PGLite's (SKSE runs
   them in plugin load order) are seen, a model string changed after PGLite planned keeps its value (that record
   loads what the string names, patched as that path is), and base object swappers make the engine load another
   record's model, with its own uses.
5. Light Placer lights follow the records PGLite points at a variant path: PGLite writes
   `LightPlacer\PGLite_Variants.json` with the lights of every entry that lists a retargeted model and asks Light
   Placer to reload its configs (its `ReloadLP` console command) after data load and after Apply. Other mods that key
   on model paths lose their effect on those records. Variant names use a session counter, not `1..N` per path.
6. Texture sets are in-memory objects with the eight paths only: no form ID, no `PG_` editor ID, no plugin record,
   created at the first load of a mesh and deduplicated per session.
7. Weighted pairs are planned as one family: a context merges only when both files agree, and each record
   component is edited once. A layout mismatch is logged like offline, but the record never points at meshes of two
   contexts. A use that names one file of a pair is patched on the other file too, and a pair with one file the game
   cannot find is patched on the other.
8. Model strings are normalized as the engine resolves them (forward slashes, an embedded `meshes\` prefix),
   where offline keys on the string as written.
9. Facegen heads are enumerated from the NPC forms, not from the file map.
10. A climate night sky model equal to the engine default `Sky\Stars.nif` is not a use.

Files and archives:

11. Nothing of the offline output is written to the data folder: no duplicate files, plugins, texture set records,
    diff JSON, zip, update cache, BodySlide ShapeData handling or dynamic cubemap deployment (the package ships the
    texture). PGLite's own files (the settings, the mod rules, the classification cache and the generated textures)
    live under `SKSE\Plugins\PGLite`, and the Light Placer config of item 5 under `LightPlacer`.
12. The archive set and the loose file set are the engine's, in the engine's own order: the INI lists, `Update.bsa`,
    `<plugin>.bsa` and `<plugin> - Textures.bsa`, every loose file including hidden ones.
13. The bytes of every mesh and texture a patch reads come from the engine's resource layer, so they are what the
    game loads; the mesh reading pass reads the data folder and the archives natively before the first patch, with
    the same archive set and order.

Lifecycle:

14. Settings changes apply to meshes loaded afterwards; resident models keep their state until the game reloads
    them, and a new instance of such a model can show an alternate texture on the wrong shape until then. A generated
    texture stays registered for the session, whatever a later Apply enables.
15. Identical copies can exist in memory until the game evicts them: merged contexts until the main-thread
    retarget runs, models created from a merged-away name, and paths ignored by a mod rule (applied at first load).
16. A failure while patching a mesh leaves it unpatched with one log line; offline, an exception stops the run.
17. The first load of a mesh family patches on the thread that loads it, the main thread included.

Program behavior:

18. The offline DynDOLOD refusal has no runtime equivalent; `.bto` files have no uses.
19. PGLite disables itself when an offline output or Auto Parallax is present; offline checks only the loose diff
    JSON, and its refusal to run next to VRAMr's output has no runtime counterpart.
20. The menu has no conflict highlighting or viewer; `exclude facegens`, `disable dynamic cubemap` and `force
    always CM` are ini options; Apply restores the records from a snapshot before it plans again; the log lives next
    to the other SKSE logs, so the `enabledebuglogging` and `enabletracelogging` keys of `settings.json` are ignored
    in favor of the ini's log level; `multithread` and `devmode` are kept from the file but not editable in the menu;
    a relative mod manager path is resolved against the game folder; a `settings.json` edited by hand runs as it is
    (the transform without its patchers is turned off with an error), and a shader that cannot be created turns its
    patcher off with an error, where the offline program refuses to run.

## Development

`sLogLevel=debug` in `PGLite.ini` logs every patched family, every generated texture and the Light Placer config.

PGLite is a standalone CMake configure (static vcpkg triplet, static CRT, C++23 for CommonLibSSE-NG) that compiles
the shared `PGLib` sources a second time, see `CMakeLists.txt`; `buildRelease.ps1` builds it after the offline
programs and produces `PGLite.zip`. The module has no unit tests of its own; the shared code is covered by `PGTests`.
