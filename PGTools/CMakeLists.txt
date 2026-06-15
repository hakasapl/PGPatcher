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
set(EXE_NAME "pgtools")
add_executable(${EXE_NAME}
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
)

# Delay load c# wrapper
if(MSVC)
    target_link_options(${EXE_NAME} PRIVATE "/DELAYLOAD:PGLib.dll")
    target_link_libraries(${EXE_NAME} PRIVATE delayimp)
endif()

#
# VCPKG Dependencies
#
find_package(CLI11 REQUIRED CONFIG)
find_package(cpptrace REQUIRED CONFIG)

target_link_libraries(${EXE_NAME} PRIVATE
    PGLib
    CLI11::CLI11
    cpptrace::cpptrace
)

add_custom_command(TARGET ${EXE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EXE_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${EXE_NAME}>
        "$<TARGET_FILE_DIR:${EXE_NAME}>"
    COMMAND_EXPAND_LISTS
)
