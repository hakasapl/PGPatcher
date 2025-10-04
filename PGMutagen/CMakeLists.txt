project(PGMutagenWrapper LANGUAGES CXX)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Include directories ---
include_directories("include")
include_directories(
    spdlog::spdlog
    ${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen
    ${CMAKE_CURRENT_SOURCE_DIR}/obj/flatbuffers
)

# --- Gather sources ---
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# --- Add the shared library ---
add_library(PGMutagenWrapper SHARED ${SOURCES} ${HEADERS})
target_compile_options(PGMutagenWrapper PRIVATE /utf-8)

# --- Find Packages ---
find_package(spdlog REQUIRED CONFIG)
find_package(flatbuffers REQUIRED)

target_link_libraries(PGMutagenWrapper
    PRIVATE
        flatbuffers::flatbuffers
        spdlog::spdlog
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/PGMutagenNE.lib"
)
target_include_directories(PGMutagenWrapper PUBLIC include)

# --- FlatBuffers generation ---
find_program(FLATC_EXECUTABLE flatc)
if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "flatc not found! Please install FlatBuffers compiler and add to your path.")
endif()

set(FLATBUFFERS_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/PGMutagenBuffers.fbs)
set(GENERATED_FLATBUFFERS_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/obj/flatbuffers/PGMutagenBuffers_generated.h)

add_custom_command(
    OUTPUT ${GENERATED_FLATBUFFERS_HEADER}
    COMMAND ${FLATC_EXECUTABLE} --cpp --csharp
            -o "${CMAKE_CURRENT_SOURCE_DIR}/obj/flatbuffers"
            ${FLATBUFFERS_SCHEMA}
    DEPENDS ${FLATBUFFERS_SCHEMA}
    COMMENT "Generating PGMutagenBuffers from FlatBuffers schema"
)

add_custom_target(PGMutagenBuffers ALL
    DEPENDS ${GENERATED_FLATBUFFERS_HEADER}
)

add_dependencies(PGMutagenWrapper PGMutagenBuffers)

# --- Build PGMutagenNE.lib via MSBuild ---
if (${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    set(MSBUILD_CONFIGURATION "Release")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(MSBUILD_CONFIGURATION "Debug")
else()
    set(MSBUILD_CONFIGURATION "Release")
endif()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/PGMutagenNE.lib
    COMMAND msbuild PGMutagen.csproj
        -p:Configuration=${MSBUILD_CONFIGURATION}
        -p:Platform=x64
        -p:OutputPath=${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building PGMutagenNE.lib"
)

add_custom_target(PGMutagenNE ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/PGMutagenNE.lib
)

add_dependencies(PGMutagenWrapper PGMutagenNE)

# --- Post-build: copy DLLs to runtime output ---
file(GLOB PG_MUTAGEN_DLLS "${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/*")
foreach(file_path IN LISTS PG_MUTAGEN_DLLS)
    add_custom_command(TARGET PGMutagenWrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${file_path}"
            "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
