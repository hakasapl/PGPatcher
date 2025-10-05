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
    "include/PGPatcherGlobals.hpp"
    "include/GUI/CompletionDialog.hpp"
    "include/GUI/LauncherWindow.hpp"
    "include/GUI/ModSortDialog.hpp"
    "include/GUI/WXLoggerSink.hpp"
    "include/GUI/components/PGCheckedDragListCtrl.hpp"
    "include/GUI/components/PGCheckedDragListCtrlGhostWindow.hpp"
    "include/GUI/components/PGCheckedDragListCtrlEvtItemDragged.hpp"
    "include/GUI/components/PGCheckedDragListCtrlEvtItemChecked.hpp"
)

set(SOURCES
    "src/ParallaxGenConfig.cpp"
    "src/ParallaxGenUI.cpp"
    "src/PGPatcherGlobals.cpp"
    "src/GUI/CompletionDialog.cpp"
    "src/GUI/LauncherWindow.cpp"
    "src/GUI/ModSortDialog.cpp"
    "src/GUI/components/PGCheckedDragListCtrl.cpp"
    "src/GUI/components/PGCheckedDragListCtrlGhostWindow.cpp"
    "src/GUI/components/PGCheckedDragListCtrlEvtItemDragged.cpp"
    "src/GUI/components/PGCheckedDragListCtrlEvtItemChecked.cpp"
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
