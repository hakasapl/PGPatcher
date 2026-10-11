# PGLite

PGLite is the SKSE plugin version of PGPatcher: it runs the same patchers on every mesh at the moment the game loads
it, and reproduces the plugin step (mesh duplicates and texture set records) in memory. Nothing is written to the data
folder, there is no output mod and no patching step before the game starts. The design is in [DESIGN.md](DESIGN.md).

## Installation

Install the `PGLite` archive like any SKSE plugin (`SKSE\Plugins\PGLite.dll`, `SKSE\Plugins\PGLite.ini` and the
dynamic cubemap texture). Requirements:

- Skyrim Special Edition 1.6.x or 1.7.x (the Anniversary Edition runtimes), SKSE and the
  [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for the installed game version.
  Skyrim SE 1.5.97 is not supported yet: the two engine functions PGLite calls are not known there, and PGLite stays
  disabled with a notice.
- No offline PGPatcher output in the load order (`ParallaxGen_Diff.json` or `PGPatcher.esp`) and no Auto Parallax.
  PGLite disables itself with an in-game notice when it finds one of them.
- Community Shaders for complex material and True PBR, as with the offline program.
- The Visual C++ 2015-2022 redistributable the game itself needs (`vcomp140.dll`) and `d3dcompiler_47.dll`,
  which is part of Windows; everything else is linked into `PGLite.dll`.

The menu opens with the key set in `PGLite.ini` (`iMenuKey`, a DirectInput scan code, F10 by default). Settings live
in `SKSE\Plugins\PGLite\settings.json` with the same keys as the offline `params`, so a settings file can be copied
between the two programs, and the mod rules in `SKSE\Plugins\PGLite\modrules.json`. The log is
`My Games\Skyrim Special Edition\SKSE\PGLite.log`.

## How it works

At data load PGLite reads every record model of the loaded game, plans the variants of meshes that several records
use with different alternate textures or material contexts, points those records at virtual paths under
`meshes\_pgpatcher_dups\`, and registers one resource stream per path with the engine. Opening such a stream patches
the mesh (every context of it at once), corrects the alternate texture indices of its records and creates the texture
set records the patched alternate textures need. The textures-only file map, the texture classification (complex
material detection runs on the GPU and is cached in `SKSE\Plugins\PGLite\cmcache.bin`), the True PBR configs and the
mod manager data are built on a background thread right after data load.

Apply in the menu saves the settings, restores every record, plans again and registers the new variant streams.
Meshes loaded from then on use the new settings; models the game already holds keep their state until the game
reloads them.

## Differences from the offline PGPatcher

Everything not listed here behaves as the offline program does (block order, the weighted `_0`/`_1` layout check,
the allowed record types with the ADDN rule, mesh block and allow lists, `PG_IGNORE`, `PG_STOCK`, facegen heads,
skin tint shapes, the use ordering and the `ignoreBaseMesh` rule, True PBR config order, the vanilla archive list,
mod attribution and priorities, alternate texture handling, index corrections and every shader, slot and geometry
edit of the patchers).

Patchers and textures:

1. No texture generation: the Parallax to Complex Material transform and Fix SSS do not exist at runtime. A shape
   whose parallax match cannot apply falls through to the next match; soft lighting shapes keep their diffuse in
   the glow slot.
2. Textures are classified by file name suffix, the texture rules and the vanilla archive list only. The offline
   vote of the shapes that use a texture does not run, so a texture whose name contradicts its use is classified
   differently.
3. No pgtools-only patchers (Particle Lights to Light Placer, Convert to HDR) and no `--consider-allmeshes`.

Records and the plugin step:

4. Records are the live game state: edits other tools make at data load are seen, and base object swappers make
   the engine load another record's model, with its own uses.
5. Light Placer and other mods that key on model paths lose their effect on records that PGLite points at a variant
   path; handler patchers are out of scope at runtime. Variant names use a session counter, not `1..N` per path.
6. Texture sets are in-memory objects with the eight paths only: no form ID, no `PG_` editor ID, no plugin record,
   created at the first load of a mesh and deduplicated per session.
7. Weighted pairs are planned as one family: a context merges only when both files agree, and each record
   component is edited once. A layout mismatch is logged like offline, but the record never points at meshes of two
   contexts.
8. Model strings are normalized as the engine resolves them (forward slashes, an embedded `meshes\` prefix),
   where offline keys on the string as written.
9. Facegen heads are enumerated from the NPC forms, not from the file map.
10. A climate night sky model equal to the engine default `Sky\Stars.nif` is not a use.

Files and archives:

11. Nothing is written: no duplicate files, plugins, texture set records, diff JSON, zip, update cache, BodySlide
    ShapeData handling or dynamic cubemap deployment (the package ships the texture).
12. The archive set and the loose file set are the engine's: the INI lists, `Update.bsa`, `<plugin>.bsa` and
    `<plugin> - Textures.bsa`, every loose file including hidden ones.
13. Every file read goes through the engine's resource layer, so it is what the game loads.

Lifecycle:

14. Settings changes apply to meshes loaded afterwards; resident models keep their state until the game reloads
    them.
15. Identical copies can exist in memory until the game evicts them: merged contexts until the main-thread
    retarget runs, models created from a merged-away name, and paths ignored by a mod rule (applied at first load).
16. A failure while patching a mesh leaves it unpatched with one log line; offline, an exception stops the run.
17. The first load of a mesh family patches on the thread that loads it, the main thread included.

Program behavior:

18. The offline DynDOLOD refusal has no runtime equivalent; `.bto` files have no uses.
19. PGLite disables itself when an offline output or Auto Parallax is present, with a stricter check than offline.
20. The menu is English only, without conflict highlighting or viewer; `exclude facegens` and `disable dynamic
    cubemap` are ini options; `vanillabsalist` and the default mesh block list are hidden defaults; the MO2
    instance is detected and confirmed instead of typed; the log lives next to the other SKSE logs.

## Development

`PGLite/PGLite.ini` has a `[Debug]` section: `sLoadSaveAtStart=<save name without extension>` loads that save as soon
as the main menu opens, so a session can be tested without clicking through the menu. `sLogLevel=debug` logs every
patched family.

PGLite is a standalone CMake configure (static vcpkg triplet, static CRT, C++23 for CommonLibSSE-NG) that compiles
the shared `PGLib` sources a second time, see `CMakeLists.txt`; `buildRelease.ps1` builds it after the offline
programs and produces `PGLite.zip`. The module has no unit tests of its own; the shared code is covered by `PGTests`.
