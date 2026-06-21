#
# Resources File (Windows only)
#
if(WIN32)
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/meta.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
    @ONLY
)
endif()

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
if(WIN32)
add_executable(${EXE_NAME} WIN32
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
)
else()
add_executable(${EXE_NAME}
    ${SOURCES}
    ${HEADERS}
)
endif()

#
# VCPKG Dependencies
#
find_package(CLI11 REQUIRED CONFIG)
find_package(wxWidgets REQUIRED COMPONENTS core base)
find_package(cpptrace REQUIRED CONFIG)

if(WIN32)
target_link_libraries(${EXE_NAME} PRIVATE
    PGLib
    CLI11::CLI11
    DbgHelp
    ${wxWidgets_LIBRARIES}
    cpptrace::cpptrace
)
else()
target_link_libraries(${EXE_NAME} PRIVATE
    PGLib
    CLI11::CLI11
    ${wxWidgets_LIBRARIES}
    cpptrace::cpptrace
)
endif()

# Delay load c# wrapper
if(MSVC)
    target_link_options(${EXE_NAME} PRIVATE "/DELAYLOAD:PGLib.dll")
    target_link_libraries(${EXE_NAME} PRIVATE delayimp)
endif()

#
# Assets
#
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources/runningparallaxgen.gif DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/)
