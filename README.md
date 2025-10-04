# PGPatcher

Dynamic patcher to patch meshes or textures for various things in your load order.

See the [Nexus Page](https://www.nexusmods.com/skyrimspecialedition/mods/120946) for full description.

A wiki is available [here](https://github.com/hakasapl/ParallaxGen/wiki)

## Contributing

Contributors are welcome. Thank you in advance!

This is a CMake project with VCPKG for packages. Supported IDEs are Visual Studio 2022 or Visual Studio Code. Personally I use Visual Studio Code with the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd). It tends to be much faster than VS2022.

### Requirements

* FlatBuffers `flatc` compiler version 25.2.10 in your path (if this is ever updated the nuget package also needs an update)

### Visual Studio Code

* Visual Studio 2022 with desktop development for C++ is still required as this project uses the `MSVC` toolchain.
* Install [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) extension.
* Install [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) extension for a debugger.
* Install [clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) extension for the language server.
* Create a project-level `.vscode/cmake-kits.json`. You can get the name by editing the user-level kit file. The command in VS code is `CMake: Edit User-Local CMake Kits` to do so.

  ```json
  [
    {
      "name": "VS2022-VCPKG",
      "visualStudio": "6f297109",  // << Change this to whatever is on your system
      "visualStudioArchitecture": "x64",
      "isTrusted": true,
      "cmakeSettings": {
        "CMAKE_TOOLCHAIN_FILE": "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"  // << Change this to where it is on your system
      }
    }
  ]
  ```

### Visual Studio 2022

You should be able to just open the directory in Visual Studio and everything should automatically work.

### Initializing Project

1. Install pre-commit from [here](https://pre-commit.com/)
1. Install pre-commit hook: `pre-commit install`
1. Get git submodules: `git submodule init` and `git submodule update`
1. Configure CMake
