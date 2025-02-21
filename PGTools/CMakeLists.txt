set(EXE_NAME "pgtools")
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/meta.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
    @ONLY
)

# Add Files
set (HEADERS
)

set(SOURCES
    "src/main.cpp"
)

include_directories("include")
add_executable(pgtools
    ${SOURCES}
    ${HEADERS}
    ${CMAKE_CURRENT_BINARY_DIR}/resources/meta.rc
)

# Packages
find_package(CLI11 REQUIRED CONFIG)
find_package(cpptrace REQUIRED CONFIG)

target_link_libraries(pgtools PRIVATE
    PGLib
    CLI11::CLI11
    cpptrace::cpptrace
)

install(TARGETS pgtools DESTINATION . )
install(FILES $<TARGET_RUNTIME_DLLS:pgtools> DESTINATION . )
