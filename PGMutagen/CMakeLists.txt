# PGMutagen is currently built in-source via msbuild
if (${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
	set(MSBUILD_CONFIGURATION "Release")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	set(MSBUILD_CONFIGURATION "Debug")
endif()

# PGMutagen configure step
execute_process(
    COMMAND msbuild -t:restore PGMutagen.csproj
    -p:Configuration=${MSBUILD_CONFIGURATION}
    -p:Platform=x64
    -p:OutputPath=${PGMUTAGEN_OUTPUT_DIR}
    WORKING_DIRECTORY ${PGMUTAGEN_DIR}
    RESULT_VARIABLE RESTORE_RESULT
)

if(NOT RESTORE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to restore PGMutagen project")
endif()

# PGMutagen build step
add_custom_target(PGMutagen
				   msbuild -t:build PGMutagen.csproj
                   -p:Configuration=${MSBUILD_CONFIGURATION}
                   -p:Platform=x64
                   -p:OutputPath=${PGMUTAGEN_OUTPUT_DIR}
				   COMMENT "Building PGMutagen ${MSBUILD_CONFIGURATION},x64"
                   WORKING_DIRECTORY ${PGMUTAGEN_DIR}
                   BYPRODUCTS ${PGMUTAGEN_OUTPUT_DIR}/PGMutagenNE.lib)
