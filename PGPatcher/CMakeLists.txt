#
# Resources File
#
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/meta.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
    @ONLY
)

#
# Sources and Includes
#
include_directories("include")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

#
# Executable
#
set(EXE_NAME "PGPatcher")
add_executable(${EXE_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
)

#
# VCPKG Dependencies
#
find_package(CLI11 REQUIRED CONFIG)
find_package(wxWidgets REQUIRED COMPONENTS core base)
find_package(cpptrace REQUIRED CONFIG)

target_link_libraries(${EXE_NAME} PRIVATE
    PGLib
    CLI11::CLI11
    DbgHelp
    ${wxWidgets_LIBRARIES}
    cpptrace::cpptrace
)

# Delay load c# wrapper
if(MSVC)
    target_link_options(${EXE_NAME} PRIVATE "/DELAYLOAD:PGLib.dll")
    target_link_libraries(${EXE_NAME} PRIVATE delayimp)
endif()

#
# Assets
#
# Copy at build time (not configure time) so edits to these files land in the output
# directory on every build without needing a CMake reconfigure
file(GLOB RESOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/*.gif
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/*.svg)
add_custom_target(${EXE_NAME}_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_CURRENT_SOURCE_DIR}/translations ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/translations
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RESOURCE_FILES} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources
    VERBATIM
)
add_dependencies(${EXE_NAME} ${EXE_NAME}_data)
