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

# Disables conflicting min/max macros in Windows.h
add_compile_definitions(NOMINMAX)
add_compile_definitions(WIN32_LEAN_AND_MEAN)
add_compile_definitions(NOSOCKETTYPE)
add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)

# Project Vars
set(PROJECT_NAME "PGPatcher")

# Initialize Project
set(PG_VERSION_MAJOR 0)
set(PG_VERSION_MINOR 8)
set(PG_VERSION_PATCH 10)
set(PG_VERSION_TEST 0)

set(PG_VERSION "${PG_VERSION_MAJOR}.${PG_VERSION_MINOR}.${PG_VERSION_PATCH}.${PG_VERSION_TEST}")
project(${PROJECT_NAME} VERSION ${PG_VERSION})
add_compile_definitions(PG_VERSION="${PG_VERSION}")

# Set test version (set this to 0 for prod releases)
add_compile_definitions(PG_TEST_VERSION=${PG_VERSION_TEST})

# Setup Folders
set(EXTRN_BUILD_DIR ${CMAKE_BINARY_DIR}/external/blds)
set(EXTRN_INSTALL_DIR ${CMAKE_BINARY_DIR}/external/installed)
file(MAKE_DIRECTORY ${EXTRN_BUILD_DIR})
file(MAKE_DIRECTORY ${EXTRN_INSTALL_DIR})
# Set CMAKE module path to include external install dir
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${EXTRN_INSTALL_DIR})


# PGMutagen is currently built in-source via msbuild
set(PGMutagen_DIR ${CMAKE_SOURCE_DIR}/PGMutagen)

if (${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
	set(MSBUILD_CONFIGURATION "Release")
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	set(MSBUILD_CONFIGURATION "Debug")
endif()

set(PGMutagen_BINARY_DIR ${PGMutagen_DIR}/bin/x64/${MSBUILD_CONFIGURATION}/net8.0)

add_custom_target( PGMutagen
				   msbuild -t:restore,build PGMutagen.csproj -p:Configuration=${MSBUILD_CONFIGURATION} -p:Platform=x64
				   COMMENT "Building PGMutagen ${MSBUILD_CONFIGURATION},x64"
                   WORKING_DIRECTORY ${PGMutagen_DIR}
				   BYPRODUCTS ${PGMutagen_BINARY_DIR}/PGMutagenNE.lib )

set_target_properties( PGMutagen
					   PROPERTIES ADDITIONAL_CLEAN_FILES ${PGMutagen_BINARY_DIR})

# NIFLY
message(STATUS "Install NIFLY external...")
execute_process(COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/external/nifly -B ${EXTRN_BUILD_DIR}/nifly -DCMAKE_INSTALL_PREFIX=${EXTRN_INSTALL_DIR} -DBUILD_TESTING=OFF)
execute_process(COMMAND ${CMAKE_COMMAND} --build ${EXTRN_BUILD_DIR}/nifly --target install --config ${CMAKE_BUILD_TYPE})

# Submodules
enable_testing()
add_subdirectory(PGPatcher)
add_subdirectory(PGLib)
add_subdirectory(PGTools)

# Install Global Things
# Copy Folders
file(COPY ${CMAKE_SOURCE_DIR}/PGPatcher/assets DESTINATION ${CMAKE_BINARY_DIR}/)
file(COPY ${CMAKE_SOURCE_DIR}/PGLib/shaders DESTINATION ${CMAKE_BINARY_DIR}/)

# Copy assets to install
install(DIRECTORY ${CMAKE_BINARY_DIR}/assets DESTINATION . )
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders DESTINATION . )
