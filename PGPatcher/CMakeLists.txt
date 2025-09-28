# Configure resource templates
set(EXE_NAME "PGPatcher")
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/meta.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
    @ONLY
)

# Add Files
set (HEADERS
    "include/ParallaxGenConfig.hpp"
    "include/ParallaxGenUI.hpp"
    "include/ParallaxGenHandlers.hpp"
    "include/GUI/LauncherWindow.hpp"
    "include/GUI/ModSortDialog.hpp"
    "include/GUI/components/CheckedColorDragListCtrl.hpp"
    "include/GUI/components/DragGhostWindow.hpp"
)

set(SOURCES
    "src/ParallaxGenConfig.cpp"
    "src/ParallaxGenUI.cpp"
    "src/GUI/LauncherWindow.cpp"
    "src/GUI/ModSortDialog.cpp"
    "src/GUI/components/CheckedColorDragListCtrl.cpp"
    "src/GUI/components/DragGhostWindow.cpp"
    "src/main.cpp"
)

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
