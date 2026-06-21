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
find_package(Boost REQUIRED COMPONENTS locale)
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
    D3DCompiler.lib
)

if(WIN32)
    set(PLATFORM_LIBS ${D3D_LIBS} Shlwapi)
else()
    find_package(Vulkan REQUIRED)
    set(PLATFORM_LIBS Vulkan::Vulkan)
endif()

target_link_libraries(${LIBRARY_NAME} PUBLIC
    spdlog::spdlog
    bsa::bsa
    ${Boost_LIBRARIES}
    nifly
    miniz::miniz
    Microsoft::DirectXTex
    ${PLATFORM_LIBS}
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    cpptrace::cpptrace
    PGMutagenWrapper
)
target_include_directories(${LIBRARY_NAME} PUBLIC include)

#
# Shader Files (Windows only - D3D compute shaders)
#
if(WIN32)
add_custom_command(TARGET ${LIBRARY_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${LIBRARY_NAME}>/cshaders
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${LIBRARY_NAME}>/cshaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/cshaders
        $<TARGET_FILE_DIR:${LIBRARY_NAME}>/cshaders
)
endif()

if(NOT WIN32)
    find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

    set(GLSL_SHADERS
        cshaders/CountAlphaValues.comp
        cshaders/ParallaxToCM.comp
        cshaders/ConvertToHDR.comp
        cshaders/SSSFix.comp
    )

    set(SPV_OUTPUTS "")
    foreach(GLSL ${GLSL_SHADERS})
        get_filename_component(FILE_NAME ${GLSL} NAME_WE)
        set(SPV_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cshaders/${FILE_NAME}.spv)
        add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/cshaders
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/${GLSL} -o ${SPV_OUTPUT}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${GLSL}
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUTPUT})
    endforeach()

    add_custom_target(PGLibSPV DEPENDS ${SPV_OUTPUTS})
    add_dependencies(${LIBRARY_NAME} PGLibSPV)

    # Copy .spv files to runtime output directory
    add_custom_command(TARGET ${LIBRARY_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${LIBRARY_NAME}>/cshaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/cshaders
            $<TARGET_FILE_DIR:${LIBRARY_NAME}>/cshaders
    )
endif()

# include and link mutagen wrapper output
add_dependencies(${LIBRARY_NAME} PGMutagenWrapper)
