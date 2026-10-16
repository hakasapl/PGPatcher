#
# Compiles the compute shaders of a folder with fxc at build time and generates one header per shader that holds the
# bytecode as a byte array named g_<shader name>, so the programs embed the shaders instead of shipping the HLSL sources
# and compiling them at run time.
#
# pg_add_compiled_shaders(<target> <shader folder> <output folder>)
#   Creates <target>, which depends on the generated headers in <output folder>. Make the targets that include the
#   headers depend on it and put <output folder> on their include path.
#
function(pg_add_compiled_shaders TARGET_NAME SHADER_DIR OUTPUT_DIR)
    # fxc ships with the Windows SDK. The developer environment names the SDK's bin folder; otherwise the newest SDK in
    # the default install folder is used.
    find_program(PG_FXC_EXECUTABLE NAMES fxc HINTS "$ENV{WindowsSdkVerBinPath}x64" "$ENV{WindowsSdkVerBinPath}")
    if(NOT PG_FXC_EXECUTABLE)
        file(GLOB PG_FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
        if(PG_FXC_CANDIDATES)
            list(SORT PG_FXC_CANDIDATES)
            list(GET PG_FXC_CANDIDATES -1 PG_FXC_NEWEST)
            set(PG_FXC_EXECUTABLE "${PG_FXC_NEWEST}" CACHE FILEPATH "fxc shader compiler" FORCE)
        endif()
    endif()
    if(NOT PG_FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe (the shader compiler of the Windows SDK) was not found")
    endif()

    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    file(GLOB PG_SHADER_SOURCES CONFIGURE_DEPENDS "${SHADER_DIR}/*.hlsl")
    set(PG_SHADER_HEADERS "")
    foreach(SHADER_SOURCE ${PG_SHADER_SOURCES})
        get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME_WE)
        set(SHADER_HEADER "${OUTPUT_DIR}/${SHADER_NAME}.h")
        add_custom_command(
            OUTPUT "${SHADER_HEADER}"
            # fxc's default optimization level: the level D3DCompile used when the programs compiled the shaders at
            # runtime, so the generated textures keep their bytes.
            COMMAND "${PG_FXC_EXECUTABLE}" /nologo /T cs_5_0 /E main /Ges /Fh "${SHADER_HEADER}"
                    /Vn "g_${SHADER_NAME}" "${SHADER_SOURCE}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling shader ${SHADER_NAME}.hlsl"
            VERBATIM
        )
        list(APPEND PG_SHADER_HEADERS "${SHADER_HEADER}")
    endforeach()

    add_custom_target(${TARGET_NAME} DEPENDS ${PG_SHADER_HEADERS})
endfunction()
