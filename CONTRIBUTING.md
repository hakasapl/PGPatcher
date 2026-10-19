# Contributing

Contributors are welcome. Thank you in advance! Please use this page to learn about the project.

PGPatcher is a primarily C++ 20 application built on the MSVC 2022 toolchain, using VCPKG for dependency management. There is some C#.NET portions in one of the modules which this page will expand on. The project uses cmake for building, including the .NET portions.

## Guidelines

### Commits / PRs

All commits to `main` must be via a pull request. All pull requests must be named conforming to the [conventional commits specification](https://www.conventionalcommits.org/en/v1.0.0/). CI checks will fail if titles are not formatted in this way. All CI checks must pass prior to a PR being merged into main.

As every PR is squashed when merging automatically, the commit messages within the PR are not relevant. Only the title and description of the PR will end up in the final commit.

All PRs will be reviewed by GitHub copilot, alongside a human maintainer. Often copilot has erroneous results but it is good to read what it has to say regardless.

If your change is a user-facing change (ie. not a refactor, CI, or other project change), a changelog entry under `## UNRELEASED` in `CHANGELOG.md` is required. A CI check fails when an entry lands in a section that was already released, which happens when a branch made before a release appended its entry to the old `## UNRELEASED` section.

### Versioning

This project uses the [semantic versioning v2 specification](https://semver.org/). Versions are determined automatically based on the conventional commit titles used between tags. The manually triggered `Build and Publish Release` workflow will build the main branch, determine the next release version, and then create a draft release to be approved manually. Its optional `version` input releases the version given there instead of the calculated one, as long as that version's tag does not exist yet. For a stable release it also commits the dated changelog to main: the `## UNRELEASED` section becomes the section of that version, dated the day of the run, below a new empty `## UNRELEASED` section. If `CHANGELOG.md` changed on main while the release was building, that change comes as a pull request instead, to be merged before the draft release is published. Pre-releases are occasionally released for community feedback. These have the format `0.0.0-preX` where X is the number of commits since the last tag.

Tags are exclusively used to designate versions in this repository.

### Linting and Formatting

[Pre-commit](https://pre-commit.com/) is used for linting and formatting. Pre-commit calls [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to format the C++ code; clang-tidy runs separately, see below. Pre-commit has several general hooks for file endings, json formatting, and more. It is recommended to add the pre-commit hook to your local cloned repository so that it can run before any commit.

Both are enforced on every pull request. [pre-commit.ci](https://pre-commit.ci/) runs the hooks and pushes any fixes back to the branch, which covers clang-format; the version is pinned in `.pre-commit-config.yaml`, so it does not depend on what your editor happens to bundle.

clang-tidy is not a pre-commit hook, because it replays the real compile commands and so needs a build tree that has been configured *and* built at least once (PGMutagen generates flatbuffers headers into it). The `Build PGPatcher` workflow runs it after the build. To run it yourself:

```
python scripts/run_clang_tidy.py
```

It picks up `buildRelease/` or `build/` automatically; pass `--build-dir` for anything else. It needs the LLVM release pinned in `scripts/clang-tidy-version.txt` first on PATH (CI downloads it from llvm-project; locally, install the same release) and refuses any other version. The script's docstring explains why a plain clang-tidy invocation is not enough here.

The build itself has to stay warning free. The `Build PGPatcher` workflow keeps the output of its build and fails on any compiler, linker or C# warning in it (`scripts/check_build_warnings.py`; its docstring says what counts). To run the same check on your own build:

```
./buildRelease.ps1 -NoZip 2>&1 | Tee-Object build.log
python scripts/check_build_warnings.py build.log
```

A warning is only printed for a file that was compiled, so after a header change check a from-scratch build rather than an incremental one.

The [Webkit](https://webkit.org/code-style-guidelines/) style is used for all C++ code. Clang-format and clang-tidy will enforce this.

### Unit Tests

The `PGTests` module holds [googletest](https://github.com/google/googletest) unit tests for `PGLib` and `PGMutagen` (the GUI and the CLI are not unit tested). They build with the rest of the project (CMake's `BUILD_TESTING`, on by default) into `pgtests.exe` next to the application binaries, and run from that `bin` folder, either directly or through `ctest --test-dir buildRelease`. Tests that need a GPU or the vanilla Skyrim installation skip when those are not available; set `PG_TEST_SKYRIM_DIR` to point the vanilla tests at another installation.

New code in `PGLib` or `PGMutagen` should come with tests. The helpers in `PGTests/include` build NIFs, DDS textures, BSAs, plugins and a fake game folder on the fly, so tests need no checked in game assets.

## Environment Setup

1. Install Visual Studio 2022 or Build Tools for Visual Studio 2022 with the `Desktop Development for C++` enabled in the installer.
1. Install [flatbuffers compiler](https://github.com/google/flatbuffers/releases/tag/v25.2.10) version `25.2.10` and ensure `flatc.exe` is in your PATH <- The version is critically important
1. Install pre-commit from [here](https://pre-commit.com/)
1. Clone the repo and its submodules `git clone --recursive <YOUR FORK OF PGPATCHER>`
1. Change directory to the repo `cd PGPatcher`
1. Install pre-commit hook: `pre-commit install`
1. Configure CMake
    1. VS 2022 will do this out of the box
    1. VS Code requires some setup
        1. Install [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
        1. Install [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) for the debugger
        1. Install [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) for the language server
        1. Copy the `.vscode/cmake-kits.json.example` to `.vscode/cmake-kits.json` and change the `visualStudio` field to match your install (find using `Edit User-Local CMake Kits` from the VS Code runner). Set the cmake toolchain file according to your install location. This will ensure VCPKG is hooked.

## Modules

This section will dive deeper into the individual components of PG. PGPatcher is split into 5 CMake modules, all of which are included in the main `CMakeLists.txt`, plus `PGLite`, which is a standalone configure (see below).

### PGLite

`PGLite` is the SKSE plugin version of PGPatcher (`PGLite/DESIGN.md` has the design, `PGLite/README.md` the user facing differences). It is not part of the root configure: a configure has one vcpkg triplet, and the plugin is one statically linked DLL (x64-windows-static, static CRT, C++23 for CommonLibSSE-NG), so `PGLite/CMakeLists.txt` is configured on its own (`buildRelease.ps1` does it after the root build) and compiles the shared `PGLib` sources a second time as a static library without the offline-only units (the runners, the Mutagen plugin implementation, the update cache, the stack trace task guard, the pgtools-only particle lights and HDR patchers). `external/CommonLibSSE-NG` is its submodule. Every change to `PGLib` has to build there as well. The module has no unit tests of its own; the shared code it uses is tested through `PGTests`.

### PGLib

This is a shared library which contains all the library code associated with both PGPatcher and PGTools. This is where most of the functional code of the library lives. It was separated into its own library to avoid the size increase of static linking to both `pgtools.exe` and `PGPatcher.exe`.

#### PGLib/common

The common folder should only contain those classes which are sufficiently generic to be used outside of the PGPatcher project. As such, they should contain no references to anything prefixed `PG`.

#### PGLib/handlers

The handlers folder contains all of the handler patchers. Handler patchers are silent patchers that run on every patched mesh transparently. They do not have access to modify the patched NIF. For example, PGPatcher's functionality to patch light placer jsons is a handler patcher.

#### PGLib/patchers

The patchers folder contains the classes for all the individual patchers contained within the PG ecosystem (pgtools and pgpatcher). These patchers are set up with a focus on inheritance, to ensure all patchers of the same type can be used generically.

#### PGLib/pgutil

Utility code that is exclusively for the PG ecosystem.

#### PGLib/util

Generic utility code that can be used anywhere. This code cannot contain any reference to `PG`.

#### `PGD3D`

PGD3D is the class responsible for GPU hardware acceleration. It is initialized as an object from `PGPatcher` or `pgtools` and should be treated as a singleton. It makes use of the DirectX11 API and implements several helpers to assist in running compute shaders on the GPU. This is what allows PGPatcher to be very fast at texture processing and reading.

#### `PGDirectory`

`PGDirectory`, which inherits `BethesdaDirectory`, is the class which is responsible for keeping track of all files in the user's game folder. This is a singleton. Itself and its superclass can determine the BSA load order and scrub the game folder to build a file map. This file map is used when finding all meshes, textures, and any other relevant assets that need to be a candidate for patching. In addition, this class also is responsible for the texture mapping stage, where PG determines what type of texture each texture is.

#### `PGGlobals`

A static class, `PGGlobals` contains all of the singletons used throughout the execution. They are set from the main runners in `PGPatcher` or `pgtools` after objects are created.

#### `PGModManager`

`PGModManager` is responsible for gathering information from the user's mod manager (Vortex or MO2). This is a singleton. It builds a separate file map called a "Mod File Map", which is used to determine from which mod individual files originate from.

#### `PGPatcher`

This is the static class housing the patching code for meshes and textures. The main runners call these methods mainly for the patching process.

#### `PGPlugin`

This is a static class housing the patching code related to plugins. It hooks into `PGMutagen` directly.

### PGMutagen

PGMutagen implements a C++ wrapper around a .NET static class which interfaces with the [mutagen](https://github.com/Mutagen-Modding/Mutagen) library, which is the only one of its kind that is still maintained, hence the abnormal interop that had to be built. The [DNNE](https://github.com/AaronRobinsonMSFT/DNNE) library is used to provide native exports from the .NET side. Caution must be used with memory transfer here. If memory is created on the .NET side and passed to the C++ side, the C++ side **must release the memory** otherwise a leak can occur. The [flatbuffers](https://github.com/google/flatbuffers) library is used to wrap this memory in a convenient language-agnostic structure.

### PGPatcher

This is the main code for `PGPatcher.exe`. Caution must be used to avoid duplicating code between this and  `pgtools` - any duplicate code should instead be implemented in the shared library `PGLib`. As PGPatcher is the only application with a full GUI, you will find all the frontend code here as well. The frontend uses [wxWidgets](https://wxwidgets.org/) which implements winforms. `PGPatcher` also contains code relevant to loading/saving user config jsons, and of course the main runner of the application which hooks heavily into `PGLib`.

### PGTools

`PGTools` is the CLI only companion application to `PGPatcher`. It is far simpler in implementation and simply runs patchers on a set of files.
