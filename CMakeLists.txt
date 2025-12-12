cmake_minimum_required (VERSION 3.8)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For MSVC
add_compile_options("/Zc:__cplusplus")

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

cmake_policy(SET CMP0167 NEW)

# Disables conflicting min/max macros in Windows.h
add_compile_definitions(NOMINMAX)
add_compile_definitions(WIN32_LEAN_AND_MEAN)
add_compile_definitions(NOSOCKETTYPE)
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
add_compile_definitions(_WIN32_WINNT=0x0A00)

# Project Vars
set(PROJECT_NAME "PGPatcher")

# Initialize Project
set(PG_VERSION_MAJOR 0)
set(PG_VERSION_MINOR 9)
set(PG_VERSION_PATCH 9)
set(PG_VERSION_TEST 2)

set(PG_VERSION "${PG_VERSION_MAJOR}.${PG_VERSION_MINOR}.${PG_VERSION_PATCH}.${PG_VERSION_TEST}")
project(${PROJECT_NAME} VERSION ${PG_VERSION})
add_compile_definitions(PG_VERSION="${PG_VERSION}")

# Set to 1 for test builds, 0 for prod builds
add_compile_definitions(PG_TEST_BUILD=1)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Externals
add_subdirectory(external/nifly)

# Submodules
add_subdirectory(PGMutagen)
add_subdirectory(PGPatcher)
add_subdirectory(PGLib)
add_subdirectory(PGTools)
