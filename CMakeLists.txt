cmake_minimum_required (VERSION 3.31)

#
# Project
#
set(PROJECT_NAME "PGPatcher")

#
# Using C++20 Standard for all modules
#
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#
# MSVC / Windows SDK
#
add_compile_options("/Zc:__cplusplus")
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
add_compile_definitions(UNICODE _UNICODE)

# Version

# if PG_VERSION is defined PG_PRERELEASE must also be defined, otherwise fail
if(DEFINED PG_VERSION AND NOT DEFINED PG_PRERELEASE)
  message(FATAL_ERROR "PG_VERSION is defined but PG_PRERELEASE is not defined. Please define both or neither.")
endif()

if (DEFINED PG_PRERELEASE AND NOT DEFINED PG_VERSION)
  message(FATAL_ERROR "PG_PRERELEASE is defined but PG_VERSION is not defined. Please define both or neither.")
endif()

# Check if PG_VERSION is set, if not set, use the latest commit hash as version
if(NOT DEFINED PG_VERSION OR NOT DEFINED PG_PRERELEASE)
  execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  execute_process(
    COMMAND git rev-list --count ${PG_VERSION}..HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PG_PRERELEASE
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

# Create res friendly version strings
string(REPLACE "." "," PG_VERSION_RES ${PG_VERSION})

# if prerelease is greater than 0, append -prerelease to version string
if (${PG_PRERELEASE} GREATER 0)
  set(PG_FULL_VERSION "${PG_VERSION}-pre${PG_PRERELEASE}")
  set(PG_VERSION_RES "${PG_VERSION_RES},${PG_PRERELEASE}")
else()
  set(PG_FULL_VERSION "${PG_VERSION}")
endif()

project(${PROJECT_NAME} VERSION ${PG_VERSION})

add_compile_definitions(PG_FULL_VERSION="${PG_FULL_VERSION}")
add_compile_definitions(PG_PRERELEASE=${PG_PRERELEASE})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#
# Externals
#
add_subdirectory(external/nifly)

#
# Submodules
#
add_subdirectory(PGMutagen)
add_subdirectory(PGPatcher)
add_subdirectory(PGLib)
add_subdirectory(PGTools)
