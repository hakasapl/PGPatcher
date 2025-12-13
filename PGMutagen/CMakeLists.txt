# Export all symbols in the DLL so we don't have to use __declspec in the code
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

#
# Includes and Sources
#
include_directories("include")
include_directories(
    spdlog::spdlog
    ${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen
    ${CMAKE_CURRENT_SOURCE_DIR}/obj/flatbuffers
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
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/PGMutagenNE.lib"
)
target_include_directories(${LIBRARY_NAME} PUBLIC include)

#
# FlatBuffers Code Generation
#
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

add_dependencies(${LIBRARY_NAME} PGMutagenBuffers)

#
# C# Project Build
#
if (${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    set(MSBUILD_CONFIGURATION "Release")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(MSBUILD_CONFIGURATION "Debug")
else()
    set(MSBUILD_CONFIGURATION "Release")
endif()

add_custom_target(PGMutagenNE ALL
    COMMAND msbuild PGMutagen.csproj
        -p:Configuration=${MSBUILD_CONFIGURATION}
        -p:Platform=x64
        -p:OutputPath=${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building PGMutagenNE.lib"
)

add_dependencies(${LIBRARY_NAME} PGMutagenNE)

#
# C# DLLs Copy
#
file(GLOB PG_MUTAGEN_DLLS "${CMAKE_CURRENT_SOURCE_DIR}/bin/PGMutagen/*")
foreach(file_path IN LISTS PG_MUTAGEN_DLLS)
    add_custom_command(TARGET ${LIBRARY_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${file_path}"
            "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
