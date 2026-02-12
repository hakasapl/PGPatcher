# PGPatcher - Copilot Coding Agent Instructions

## Project Overview

**PGPatcher** is a dynamic patcher for Skyrim Special Edition meshes and textures. It patches various game assets in your load order using parallel processing and advanced texture/mesh manipulation.

- **Primary Language**: C++20
- **Build System**: CMake 3.31+
- **Package Manager**: VCPKG
- **Target Platform**: Windows (x64) only
- **IDE Support**: Visual Studio 2022, Visual Studio Code

## Project Structure

```
PGPatcher/
├── PGPatcher/          # Main GUI application (wxWidgets-based)
├── PGLib/              # Shared core library (shader/texture patchers, NIF utilities)
├── PGMutagen/          # C# wrapper for ES file format handling with FlatBuffers
├── PGTools/            # Command-line interface tool (CLI11)
├── external/nifly/     # Git submodule for NIF format library
├── .github/workflows/  # CI/CD workflows
├── buildRelease.ps1    # PowerShell release build script
└── package/            # Files to include in distribution package
```

### Key Modules

1. **PGPatcher** (C++ Executable): GUI application with launcher window, progress dialogs, and configuration management
2. **PGLib** (C++ Shared Library): Core patching engine with texture processors, mesh handlers, BSA utilities
3. **PGMutagen** (C# + C++ Wrapper): .NET wrapper for ES file format via FlatBuffers serialization
4. **PGTools** (C++ CLI Tool): Command-line interface for batch operations and automation

## Technology Stack

### Core Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| **Boost** | 1.86.0+ | Algorithm, ASIO, CRC, GIL, IOStreams, Locale |
| **FlatBuffers** | 25.2.10 | Serialization format for C#/C++ interop |
| **nlohmann/json** | 3.11.3+ | JSON parsing with schema validation |
| **spdlog** | 1.15.0+ | Logging framework (wchar support enabled) |
| **wxWidgets** | 3.3.1+ | GUI framework for launcher/dialogs |
| **DirectXTex** | 2024-06-04 | Texture processing with DX11 |
| **CLI11** | 2.4.2+ | Command-line argument parsing |
| **rsm-bsa** | 4.1.0+ | Bethesda BSA archive handling |
| **cpptrace** | 0.7.3+ | Stack tracing for error reporting |
| **miniz** | 3.0.2+ | Compression utilities |

### Build Tools

- **MSVC Toolchain**: Required (Visual Studio 2022)
- **CMake**: 3.31+ with Ninja generator preferred
- **VCPKG**: For dependency management
- **FlatBuffers Compiler (`flatc`)**: Version 25.2.10 (must be in PATH)
- **MSBuild**: For C# project compilation
- **.NET SDK**: Required for PGMutagen C# compilation

## Development Setup

### Prerequisites

1. **Visual Studio 2022** with "Desktop development with C++" workload
2. **FlatBuffers compiler** (`flatc`) version 25.2.10 in your PATH
   - Download from: https://github.com/google/flatbuffers/releases/tag/v25.2.10
   - Extract and add to PATH
3. **.NET SDK** (for PGMutagen C# compilation)
4. **Pre-commit** framework: https://pre-commit.com/

### Initial Setup

```bash
# 1. Install pre-commit hooks
pre-commit install

# 2. Initialize git submodules
git submodule init
git submodule update

# 3. Configure CMake
# For VS Code: Use CMake Tools extension and configure kit
# For VS 2022: Open directory directly
```

### Visual Studio Code Setup

1. Install required extensions:
   - **CMake Tools** (`ms-vscode.cmake-tools`)
   - **C/C++** (`ms-vscode.cpptools`) - for debugging
   - **clangd** (`llvm-vs-code-extensions.vscode-clangd`) - language server (recommended)

2. Create `.vscode/cmake-kits.json` (not tracked in git):
   ```json
   [
     {
       "name": "VS2022-VCPKG",
       "visualStudio": "6f297109",  // Change to your VS instance ID
       "visualStudioArchitecture": "x64",
       "isTrusted": true,
       "cmakeSettings": {
         "CMAKE_TOOLCHAIN_FILE": "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
       }
     }
   ]
   ```

3. Get VS instance ID: Run `CMake: Edit User-Local CMake Kits` command in VS Code

## Building the Project

### Development Build (VS Code)

```bash
# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="<vcpkg-root>/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build
```

### Release Build

Use the provided PowerShell script:

```powershell
# Create release build with zip package
.\buildRelease.ps1

# Create release build without zip (for CI)
.\buildRelease.ps1 -NoZip
```

**Output locations:**
- Build artifacts: `buildRelease/bin/`
- Distribution: `dist/PGPatcher/`
- Zip package: `dist/PGPatcher.zip`

### Build Process Details

1. **FlatBuffers Code Generation**: Generates C++ and C# code from `PGMutagenBuffers.fbs`
2. **NuGet Restore**: Restores .NET dependencies for PGMutagen
3. **C# Compilation**: Builds PGMutagen.dll using MSBuild
4. **C++ Compilation**: Builds all C++ modules (PGLib, PGPatcher, PGTools, PGMutagenWrapper)
5. **Asset Copying**: Copies shaders, resources, and assets to output directory

## Code Standards

### Formatting

- **Style**: WebKit-based (`.clang-format`)
- **Line Length**: 120 characters
- **Auto-format**: Enabled on save in VS Code

### Linting

Configuration in `.clang-tidy`:

**Enabled Checks:**
- `cppcoreguidelines-*`
- `llvm-include-order`
- `misc-*` (except `include-cleaner`, `no-recursion`)
- `modernize-*`
- `performance-*`
- `readability-*` (except `function-cognitive-complexity`, `identifier-length`, `magic-numbers`)

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes/Enums | `CamelCase` | `TexturePatcher`, `ShaderType` |
| Functions/Variables | `camelBack` | `processTexture()`, `meshCount` |
| Parameters | `camelBack` | `filePath`, `shaderType` |
| Private Members | `m_` prefix + `camelBack` | `m_textureCache` |
| Protected Members | `m_` prefix + `camelBack` | `m_baseConfig` |
| Global Variables | `s_` prefix + `camelBack` | `s_instanceCount` |
| Constants/Macros | `UPPER_CASE` | `MAX_TEXTURE_SIZE` |

### Pre-commit Hooks

Automatically enforced:
- Trailing whitespace removal
- Merge conflict detection
- Line ending normalization (LF)
- JSON/YAML validation and formatting
- File case conflict detection

Run manually: `pre-commit run --all-files`

## Testing

**Note**: This project currently has **no automated test suite**. Testing is done manually:

1. **GUI Testing**: Launch PGPatcher.exe and test workflows
2. **CLI Testing**: Run `pgtools.exe` with various arguments
3. **Integration Testing**: Test with actual Skyrim mod load orders

### Manual Testing Workflow

1. Build the project
2. Run from `build/bin/` or `buildRelease/bin/`
3. Test with sample mods/meshes
4. Verify output in completion dialog

## CI/CD Workflow

### GitHub Actions (`.github/workflows/build.yml`)

**Triggers**: Push/PR to `main` branch

**Steps:**
1. Setup MSVC dev environment (amd64)
2. Install `flatc` 25.2.10
3. Checkout with submodules
4. Run `buildRelease.ps1 -NoZip`
5. Upload artifacts

**Platform**: `windows-2022` runner

## Common Issues and Workarounds

### 1. FlatBuffers Compiler Not Found

**Error**: `flatc not found! Please install FlatBuffers compiler and add to your path.`

**Solution**:
- Download FlatBuffers 25.2.10 from https://github.com/google/flatbuffers/releases/tag/v25.2.10
- Extract and add to system PATH
- Verify: `flatc --version` should output `25.2.10`

### 2. VCPKG Dependencies Not Found

**Error**: `Could not find package <name>`

**Solution**:
- Ensure `CMAKE_TOOLCHAIN_FILE` points to vcpkg's CMake toolchain
- Run `vcpkg integrate install` (if using user-wide vcpkg)
- For VS2022 built-in vcpkg: Use path from VS installation

### 3. Git Submodule Not Initialized

**Error**: `external/nifly` directory is empty or CMake fails to find nifly

**Solution**:
```bash
git submodule init
git submodule update
```

### 4. C# Build Fails (PGMutagen)

**Error**: MSBuild errors or missing .NET SDK

**Solution**:
- Install .NET SDK (6.0 or later)
- Verify MSBuild is available: `msbuild -version`
- Check `PGMutagen.csproj` is generated correctly

### 5. DirectX/D3D11 Compilation Errors

**Error**: Missing DirectX headers or libraries

**Solution**:
- Install Windows SDK via Visual Studio Installer
- Ensure "Windows 10 SDK" is selected in VS2022 components

### 6. Pre-commit Hooks Fail

**Error**: Pre-commit checks fail on commit

**Solution**:
- Install pre-commit: `pip install pre-commit` or download from https://pre-commit.com/
- Run `pre-commit install` after first clone
- Fix issues or run `pre-commit run --all-files` to see all problems

### 7. VCPKG Baseline Mismatch

**Error**: VCPKG complains about baseline or registry issues

**Solution**:
- Project uses pinned baseline in `vcpkg-configuration.json`
- Use the specified VCPKG version/commit
- Check `overlay-triplets` configuration is applied

### 8. Build Directory Conflicts

**Error**: CMake configuration fails with existing build directory

**Solution**:
- Delete `build/` or `buildRelease/` directory
- Reconfigure from scratch
- Build directories are git-ignored

## Important Notes for Agents

### File Locations
- **Source code**: `<module>/src/` and `<module>/include/`
- **Resources**: `PGPatcher/resources/`, `PGPatcher/assets/`
- **Shaders**: `PGLib/cshaders/` (copied to output at build time)
- **Output**: `build/bin/` or `buildRelease/bin/`

### Platform-Specific Code
- This is a **Windows-only** project
- Uses Windows-specific APIs (DirectX, Windows SDK)
- MSVC compiler required (not GCC/Clang)
- File paths use Windows conventions

### VCPKG Custom Configuration
- Custom triplet: `triplets/x64-windows.cmake`
- Forces dynamic linkage for all libraries
- Special spdlog configuration: `SPDLOG_WCHAR_CONSOLE` and `SPDLOG_WCHAR_FILENAMES` enabled
- Pinned VCPKG baseline for reproducibility

### Version Information
- Version defined in root `CMakeLists.txt`:
  - `PG_VERSION_MAJOR`, `PG_VERSION_MINOR`, `PG_VERSION_PATCH`, `PG_VERSION_TEST`
- Compiled into binaries via `PG_VERSION` macro
- Test/production builds controlled by `PG_TEST_BUILD` flag (0 = prod, 1 = test)

### Working with FlatBuffers
- Schema: `PGMutagen/PGMutagenBuffers.fbs`
- Generated headers: Auto-generated during CMake configure
- Used for C++/C# interop in PGMutagen module
- **Critical**: flatc version must match (25.2.10)

### Debugging
- VS Code: Use `launch.json` configurations (Attach/Launch)
- Debugger type: `cppvsdbg` (Visual Studio debugger)
- Executable path: `${command:cmake.buildDirectory}/ParallaxGen.exe` (note: older name)

### Distribution Package
- `package/` directory contents are included in release zip
- Includes dummy INI file for mod manager compatibility
- Assets, resources, and shaders copied automatically

## Making Code Changes

### Before Making Changes
1. Ensure pre-commit hooks are installed
2. Verify build succeeds before modifications
3. Check `.clang-format` and `.clang-tidy` are applied

### Adding New Features
1. Follow existing code structure and patterns
2. Use appropriate module (PGLib for core, PGPatcher for GUI, PGTools for CLI)
3. Respect naming conventions
4. Add error handling with cpptrace for stack traces
5. Use spdlog for logging (wchar support enabled)

### Modifying Dependencies
1. Update `vcpkg.json` with new dependencies
2. Add `find_package()` in appropriate `CMakeLists.txt`
3. Link library in `target_link_libraries()`
4. Test build with clean VCPKG cache if issues arise

### GUI Changes (PGPatcher)
- Uses wxWidgets 3.3.1+
- Custom controls in `PGPatcher/include/GUI/components/`
- Event handling with wxWidgets event system
- Resources in `PGPatcher/resources/` and `PGPatcher/assets/`

### CLI Changes (PGTools)
- Uses CLI11 for argument parsing
- Single source file: `PGTools/src/main.cpp`
- Links against PGLib for core functionality

## Repository Maintenance

### Changelog
- Update `CHANGELOG.md` with user-facing changes
- Follow existing format: version, date, bullet points
- Distinguish features, fixes, and removals

### Pre-commit CI
- Uses `precommit.ci` service (mentioned in git history)
- Automatic formatting and validation
- May auto-commit fixes on PRs

### Submodule Updates
```bash
cd external/nifly
git pull origin main  # or specific version
cd ../..
git add external/nifly
git commit -m "Update nifly submodule"
```

## Resources

- **Nexus Page**: https://www.nexusmods.com/skyrimspecialedition/mods/120946
- **Wiki**: https://github.com/hakasapl/ParallaxGen/wiki
- **Issue Tracker**: GitHub Issues
- **FlatBuffers**: https://google.github.io/flatbuffers/
- **VCPKG**: https://vcpkg.io/

## Quick Reference Commands

```bash
# Setup
git submodule update --init
pre-commit install

# Build (PowerShell)
.\buildRelease.ps1

# Format code
clang-format -i <file>

# Run pre-commit checks
pre-commit run --all-files

# Clean build
rm -rf build buildRelease dist
```

---

## Notes on Instructions Creation

This file was created by exploring the repository structure, build system, and existing documentation. During the creation process:

### Challenges Encountered

1. **Platform-Specific Project**: This is a Windows-only project requiring MSVC, DirectX, and Windows SDK. Full build testing could not be performed in a Linux environment.

2. **FlatBuffers Dependency**: The project requires `flatc` version 25.2.10 in PATH, which is not commonly available in standard development environments. This is documented in the "Common Issues" section.

3. **No Automated Tests**: The project lacks automated unit or integration tests, making it harder to validate changes programmatically. Manual testing workflows are documented instead.

4. **Multiple Build Systems**: The project uses both CMake (for C++) and MSBuild (for C# components), requiring coordination between them via custom CMake commands.

### Verification Performed

- ✅ Reviewed all CMakeLists.txt files to understand build process
- ✅ Analyzed `.clang-format` and `.clang-tidy` for code standards
- ✅ Examined `.pre-commit-config.yaml` for automated checks
- ✅ Reviewed `vcpkg.json` and `vcpkg-configuration.json` for dependencies
- ✅ Analyzed `buildRelease.ps1` for release build process
- ✅ Examined GitHub Actions workflow for CI/CD
- ✅ Reviewed README.md and CHANGELOG.md for context
- ✅ Explored source code structure across all modules

### Recommendations for Future Updates

1. Consider adding automated unit tests for core PGLib functionality
2. Document examples of common patching operations in the wiki
3. Add troubleshooting section to README.md for common setup issues
4. Consider containerizing the build environment for reproducibility
5. Add architecture diagrams to help new contributors understand data flow
