# Export all symbols in the DLL so we don't have to use __declspec in the code
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

#
# Includes and Sources
#
include_directories("include")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

#
# Library
#
set(LIBRARY_NAME "PGLib")
add_library(${LIBRARY_NAME} SHARED ${SOURCES} ${HEADERS})

#
# VCPKG Dependencies
#
find_package(spdlog REQUIRED CONFIG)
find_package(bsa REQUIRED CONFIG)
find_package(Boost REQUIRED)
find_package(directxtex REQUIRED CONFIG)
find_package(miniz REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(cpptrace REQUIRED CONFIG)

set(D3D_LIBS
    d3d11.lib
    dxgi.lib
    dxguid.lib
    uuid.lib
    kernel32.lib
    user32.lib
    comdlg32.lib
    advapi32.lib
    shell32.lib
    ole32.lib
    oleaut32.lib
)
target_link_libraries(${LIBRARY_NAME} PUBLIC
    spdlog::spdlog
    bsa::bsa
    Boost::headers
    nifly
    miniz::miniz
    Microsoft::DirectXTex
    ${D3D_LIBS}
    Shlwapi
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    cpptrace::cpptrace
    PGMutagenWrapper
)
target_include_directories(${LIBRARY_NAME} PUBLIC include)

#
# Shaders, compiled at build time and embedded in the binary (see cmake/PGShaders.cmake)
#
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PGShaders.cmake)
pg_add_compiled_shaders(PGLibShaders ${CMAKE_CURRENT_SOURCE_DIR}/cshaders ${CMAKE_CURRENT_BINARY_DIR}/shaders)
add_dependencies(${LIBRARY_NAME} PGLibShaders)
target_include_directories(${LIBRARY_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# include and link mutagen wrapper output
add_dependencies(${LIBRARY_NAME} PGMutagenWrapper)
