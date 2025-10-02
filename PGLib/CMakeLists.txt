set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

include_directories("include")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Add the library
add_library(PGLib SHARED ${SOURCES} ${HEADERS})

# Add Packages
find_package(spdlog REQUIRED CONFIG)
find_package(bsa REQUIRED CONFIG)
find_package(Boost REQUIRED COMPONENTS locale)
find_package(directxtk REQUIRED)
find_package(directxtex REQUIRED CONFIG)
find_package(miniz REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(cpptrace REQUIRED CONFIG)

set(DirectXTK_LIBS
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
    D3DCompiler.lib
)
target_link_libraries(PGLib PUBLIC
    spdlog::spdlog
    bsa::bsa
    ${Boost_LIBRARIES}
    nifly
    miniz::miniz
    Microsoft::DirectXTex
    ${DirectXTK_LIBS}
    Microsoft::DirectXTK
    Shlwapi
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    cpptrace::cpptrace
    ${PGMUTAGEN_OUTPUT_DIR}/PGMutagenNE.lib
)
target_include_directories(PGLib PUBLIC include)

# Asset moving
add_custom_command(TARGET PGLib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:PGLib>/cshaders
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:PGLib>/cshaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/cshaders
        $<TARGET_FILE_DIR:PGLib>/cshaders
)

# include and link mutagen wrapper output
add_dependencies(PGLib PGMutagen)
include_directories(${PGMUTAGEN_OUTPUT_DIR})
