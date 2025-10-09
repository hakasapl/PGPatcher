# Configure resource templates
set(EXE_NAME "PGPatcher")
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/meta.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
    @ONLY
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

include_directories("include")
add_executable(PGPatcher
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.rc
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
)

# Packages
find_package(CLI11 REQUIRED CONFIG)
find_package(wxWidgets REQUIRED COMPONENTS core base)
find_package(cpptrace REQUIRED CONFIG)

target_link_libraries(PGPatcher PRIVATE
    PGLib
    CLI11::CLI11
    DbgHelp
    ${wxWidgets_LIBRARIES}
    cpptrace::cpptrace
)

# Copy Folders
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/)
