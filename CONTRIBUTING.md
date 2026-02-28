# Contributing

Contributors are welcome. Thank you in advance! Please use this page to learn about the project.

PGPatcher is a primarily C++ 20 application built on the MSVC 2022 toolchain, using VCPKG for dependency management. There is some C#.NET portions in one of the modules which this page will expand on. The project uses cmake for building, including the .NET portions.

## Guidelines

### Commits / PRs

All commits to `main` must be via a pull request. All pull requests must be named conforming to the [conventional commits specification](https://www.conventionalcommits.org/en/v1.0.0/). CI checks will fail if titles are not formatted in this way. All CI checks must pass prior to a PR being merged into main.

As every PR is squashed when merging automatically, the commit messages within the PR are not relevant. Only the title and description of the PR will end up in the final commit.

All PRs will be reviewed by GitHub copilot, alongside a human maintainer. Often copilot has erroneous results but it is good to read what it has to say regardless.

### Versioning

This project uses the [semantic versioning v2 specification](https://semver.org/). Versions are determined automatically based on the conventional commit titles used between tags. The manually triggered `Build and Publish Release` workflow will build the main branch, determine the next release version, and then create a draft release to be approved manually. Pre-releases are occasionally released for community feedback. These have the format `0.0.0-preX` where X is the number of commits since the last tag.

Tags are exclusively used to designate versions in this repository.

### Linting and Formatting

[Pre-commit](https://pre-commit.com/) is used for linting and formatting. Pre-commit calls [clang-format](https://clang.llvm.org/docs/ClangFormat.html) and [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) for formatting and linting, respectively, for the C++ code. Pre-commit has several general hooks for file endings, json formatting, and more. It is recommended to add the pre-commit hook to your local cloned repository so that it can run before any commit.

The [Webkit](https://webkit.org/code-style-guidelines/) style is used for all C++ code. Clang-format and clang-tidy will enforce this.

### Unit Tests

There are none! As I work on this project for fun, I have no intention of writing unit tests. I am aware of the drawbacks of not doing so and have decided it is worth it to avoid the turmoil. If you would like to make and maintain unit tests for this project, I am happy to merge them.

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

This section will dive deeper into the individual components of PG. PGPatcher is split into 4 CMake modules, all of which are included in the main `CMakeLists.txt`.

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
