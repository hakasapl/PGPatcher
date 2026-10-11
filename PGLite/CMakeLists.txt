cmake_minimum_required(VERSION 3.21)

#
# PGLite, the SKSE runtime patcher (see DESIGN.md).
#
# This is a standalone configure, not a module of the root project: the plugin is one statically linked DLL (static
# vcpkg triplet, static CRT, C++23 for CommonLibSSE-NG), while the root configure builds the offline programs with a
# dynamic CRT, and a configure has one triplet. The shared PGLib sources are compiled a second time here into a static
# library without the units only the offline programs use (the runners, the Mutagen plugin implementation, the update
# cache, the texture generating patchers). Configure it from a Visual Studio developer shell (fxc comes from the SDK):
#
#   cmake -S PGLite -B buildRelease/PGLite -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#         -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
#
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json, which clangd and scripts/run_clang_tidy.py both read
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(PG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PG_LIB_DIR "${PG_REPO_ROOT}/PGLib")

#
# Version, the same way the root configure derives it
#
if(DEFINED PG_VERSION AND NOT DEFINED PG_PRERELEASE)
  message(FATAL_ERROR "PG_VERSION is defined but PG_PRERELEASE is not defined. Please define both or neither.")
endif()
if(DEFINED PG_PRERELEASE AND NOT DEFINED PG_VERSION)
  message(FATAL_ERROR "PG_PRERELEASE is defined but PG_VERSION is not defined. Please define both or neither.")
endif()
if(NOT DEFINED PG_VERSION OR NOT DEFINED PG_PRERELEASE)
  execute_process(
    COMMAND git describe --tags --abbrev=0 --match "[0-9]*.[0-9]*.[0-9]*" --exclude "*-pre*"
    WORKING_DIRECTORY ${PG_REPO_ROOT}
    OUTPUT_VARIABLE PG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND git rev-list --count ${PG_VERSION}..HEAD
    WORKING_DIRECTORY ${PG_REPO_ROOT}
    OUTPUT_VARIABLE PG_PRERELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()
if(${PG_PRERELEASE} GREATER 0)
  set(PG_FULL_VERSION "${PG_VERSION}-pre${PG_PRERELEASE}")
else()
  set(PG_FULL_VERSION "${PG_VERSION}")
endif()

project(PGLite VERSION ${PG_VERSION} LANGUAGES CXX)

# Static CRT for every target, matching the x64-windows-static triplet
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options("/Zc:__cplusplus")
add_compile_definitions(NOMINMAX)
add_compile_definitions(WIN32_LEAN_AND_MEAN)
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
add_compile_definitions(UNICODE _UNICODE)
add_compile_definitions(PG_FULL_VERSION="${PG_FULL_VERSION}")
add_compile_definitions(PG_PRERELEASE=${PG_PRERELEASE})

#
# CommonLibSSE-NG (submodule). SE and AE are both enabled; AE covers the 1.6.x and 1.7.x runtimes (Address Library
# format 5). VR is off.
#
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory("${PG_REPO_ROOT}/external/CommonLibSSE-NG" commonlibsse)

#
# nifly (submodule)
#
add_subdirectory("${PG_REPO_ROOT}/external/nifly" nifly)

#
# vcpkg dependencies
#
find_package(spdlog REQUIRED CONFIG)
find_package(bsa REQUIRED CONFIG)
find_package(Boost REQUIRED)
find_package(directxtex REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(imgui REQUIRED CONFIG)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

#
# PGLib, compiled as a static library from the units the runtime shares with the offline programs
#
file(GLOB_RECURSE PGLIB_SOURCES CONFIGURE_DEPENDS "${PG_LIB_DIR}/src/*.cpp")
file(GLOB_RECURSE PGLIB_HEADERS CONFIGURE_DEPENDS "${PG_LIB_DIR}/include/*.hpp")
set(PGLIB_OFFLINE_SOURCES
  # The offline runners: output directory, diff JSON, zip, the update cache.
  "${PG_LIB_DIR}/src/PGPatcher.cpp"
  # The Mutagen implementation of PGPlugin; the runtime one is src/PGPluginRuntime.cpp.
  "${PG_LIB_DIR}/src/PGPlugin.cpp"
  # The update cache (PGRunObserver's default implementation records nothing).
  "${PG_LIB_DIR}/src/PGRunCache.cpp"
  # The stack trace task guard (cpptrace).
  "${PG_LIB_DIR}/src/util/ExceptionHandlerCppTrace.cpp"
  # Handler patchers are out of scope at runtime (DESIGN.md section 7).
  "${PG_LIB_DIR}/src/handlers/HandlerLightPlacerTracker.cpp"
  "${PG_LIB_DIR}/src/patchers/PatcherMeshGlobalParticleLightsToLP.cpp"
)
list(REMOVE_ITEM PGLIB_SOURCES ${PGLIB_OFFLINE_SOURCES})

add_library(PGLibRuntime STATIC ${PGLIB_SOURCES} ${PGLIB_HEADERS})
# The shared code is a C++20 code base and compiles as such here too (the plugin's own units need C++23 for
# CommonLibSSE-NG), so a change that builds in one tree builds in the other.
set_target_properties(PGLibRuntime PROPERTIES CXX_STANDARD 20)
target_include_directories(PGLibRuntime PUBLIC "${PG_LIB_DIR}/include")
target_link_libraries(PGLibRuntime PUBLIC
  spdlog::spdlog
  bsa::bsa
  Boost::headers
  nifly
  Microsoft::DirectXTex
  nlohmann_json::nlohmann_json
  d3d11.lib
  dxgi.lib
  dxguid.lib
  Shlwapi.lib
)

# The compute shaders, compiled at build time and embedded (see PGLib/cmake/PGShaders.cmake)
include("${PG_LIB_DIR}/cmake/PGShaders.cmake")
pg_add_compiled_shaders(PGLiteShaders "${PG_LIB_DIR}/cshaders" "${CMAKE_CURRENT_BINARY_DIR}/shaders")
add_dependencies(PGLibRuntime PGLiteShaders)
target_include_directories(PGLibRuntime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders")

#
# The plugin
#
file(GLOB_RECURSE PGLITE_HEADERS CONFIGURE_DEPENDS include/*.hpp include/*.h)
file(GLOB_RECURSE PGLITE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

# The plugin declaration (SKSEPluginInfo) is written in src/main.cpp, with the version of this configure.
string(REPLACE "." ";" PG_VERSION_PARTS "${PG_VERSION}")
list(LENGTH PG_VERSION_PARTS PG_VERSION_PART_COUNT)
if(NOT PG_VERSION_PART_COUNT EQUAL 3)
  message(FATAL_ERROR "PG_VERSION must be major.minor.patch, got ${PG_VERSION}")
endif()
list(GET PG_VERSION_PARTS 0 PG_VERSION_MAJOR)
list(GET PG_VERSION_PARTS 1 PG_VERSION_MINOR)
list(GET PG_VERSION_PARTS 2 PG_VERSION_PATCH)

add_library(PGLite SHARED ${PGLITE_SOURCES} ${PGLITE_HEADERS})
target_compile_features(PGLite PRIVATE cxx_std_23)
target_compile_definitions(PGLite PRIVATE
  PG_VERSION_MAJOR=${PG_VERSION_MAJOR}
  PG_VERSION_MINOR=${PG_VERSION_MINOR}
  PG_VERSION_PATCH=${PG_VERSION_PATCH}
)
target_precompile_headers(PGLite PRIVATE include/PCH.h)
target_include_directories(PGLite PRIVATE include ${SIMPLEINI_INCLUDE_DIRS})
target_link_libraries(PGLite PRIVATE CommonLibSSE::CommonLibSSE PGLibRuntime imgui::imgui)
