# Export all symbols in the DLL so we don't have to use __declspec in the code
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

#
# Includes and Sources
#
include_directories("include")
include_directories(
    spdlog::spdlog
)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

#
# Library
#
set(LIBRARY_NAME "PGMutagenWrapper")
add_library(${LIBRARY_NAME} SHARED ${SOURCES} ${HEADERS})
target_compile_options(${LIBRARY_NAME} PRIVATE /utf-8)

#
# VCPKG Dependencies
#
find_package(spdlog REQUIRED CONFIG)
find_package(flatbuffers REQUIRED)

target_link_libraries(${LIBRARY_NAME}
    PRIVATE
        flatbuffers::flatbuffers
        spdlog::spdlog
)

# Define paths
set(PGMUTAGENNE_BUILD_DIR "${CMAKE_BINARY_DIR}/PGMutagenNE")
set(PGMUTAGENNE_BIN_DIR "${PGMUTAGENNE_BUILD_DIR}/bin")
set(PGMUTAGENNE_OBJ_DIR "${PGMUTAGENNE_BUILD_DIR}/obj")
set(PGMUTAGENNE_NUGET_DIR "${PGMUTAGENNE_BUILD_DIR}/.nuget/packages")

set(GENERATED_CSPROJ "${CMAKE_CURRENT_SOURCE_DIR}/PGMutagen.csproj")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PGMutagen.csproj.in"
    "${GENERATED_CSPROJ}"
    @ONLY
)

set(PGMUTAGEN_LIB
    ${PGMUTAGENNE_BIN_DIR}/PGMutagenNE.lib
)
target_link_libraries(${LIBRARY_NAME}
    PUBLIC
        "${PGMUTAGEN_LIB}"
)
target_include_directories(${LIBRARY_NAME} PUBLIC include)

#
# C# Project Build
#

# Flatbuffers first
find_program(FLATC_EXECUTABLE flatc)
if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "flatc not found! Please install FlatBuffers compiler and add to your path.")
endif()

set(FLATBUFFERS_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/PGMutagenBuffers.fbs)
set(GENERATED_FLATBUFFERS_HEADER ${PGMUTAGENNE_OBJ_DIR}/flatbuffers/PGMutagenBuffers_generated.h)

file(MAKE_DIRECTORY "${PGMUTAGENNE_OBJ_DIR}/flatbuffers")

execute_process(
    COMMAND ${FLATC_EXECUTABLE} --cpp --csharp
            -o "${PGMUTAGENNE_OBJ_DIR}/flatbuffers"
            ${FLATBUFFERS_SCHEMA}
    RESULT_VARIABLE _flatc_result
    OUTPUT_VARIABLE _flatc_out
    ERROR_VARIABLE _flatc_err
)

if(NOT _flatc_result EQUAL 0)
    message(FATAL_ERROR "FlatBuffers generation failed:\n${_flatc_err}")
endif()

include_directories(${PGMUTAGENNE_OBJ_DIR}/flatbuffers)

# Nuget restore during configure step
execute_process(
    COMMAND dotnet restore "${GENERATED_CSPROJ}"
        --packages "${PGMUTAGENNE_BUILD_DIR}/.nuget/packages"
        /p:BaseIntermediateOutputPath="${PGMUTAGENNE_BUILD_DIR}/obj/"
        /p:BaseOutputPath="${PGMUTAGENNE_BUILD_DIR}/bin/"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _nuget_restore_result
)

if(NOT _nuget_restore_result EQUAL 0)
    message(FATAL_ERROR "NuGet restore failed for PGMutagen.csproj")
endif()

if (${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    set(MSBUILD_CONFIGURATION "Release")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(MSBUILD_CONFIGURATION "Debug")
else()
    set(MSBUILD_CONFIGURATION "Release")
endif()

set(CSHARP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/PGMutagen.cs
    ${CMAKE_CURRENT_SOURCE_DIR}/PGMutagen.csproj
    ${FLATBUFFERS_SCHEMA}
)

add_custom_command(
    OUTPUT ${PGMUTAGEN_LIB}
    COMMAND msbuild PGMutagen.csproj
        -p:Configuration=${MSBUILD_CONFIGURATION}
        -p:Platform=x64
        -p:OutputPath=${PGMUTAGENNE_BIN_DIR}/
        -p:BaseIntermediateOutputPath=${PGMUTAGENNE_OBJ_DIR}/
        -p:BaseOutputPath=${PGMUTAGENNE_BIN_DIR}/
    DEPENDS
        ${CSHARP_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building PGMutagenNE.lib"
)

add_custom_target(PGMutagenNE
    DEPENDS ${PGMUTAGEN_LIB}
)

add_dependencies(${LIBRARY_NAME} PGMutagenNE)

include_directories(${PGMUTAGENNE_BIN_DIR}/)

#
# C# DLLs Copy
#
add_custom_target(CopyPGMutagenDLLs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PGMUTAGENNE_BIN_DIR}/" "${CMAKE_BINARY_DIR}/bin"
    COMMENT "Copying PGMutagen DLLs to build/bin"
)

add_dependencies(CopyPGMutagenDLLs PGMutagenNE)
add_dependencies(${LIBRARY_NAME} CopyPGMutagenDLLs)
