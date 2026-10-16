#
# Unit tests for PGLib and PGMutagen (googletest)
#
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

#
# Sources and Includes
#
include_directories("include")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

#
# Executable
#
set(TEST_EXE_NAME "pgtests")
add_executable(${TEST_EXE_NAME}
    ${SOURCES}
    ${HEADERS}
)

# PGLib.dll and PGMutagenWrapper.dll depend on the .NET host in bin/dotnetlib, so both are delay loaded (as the
# applications do) and main() adds that folder to the DLL search path before the first call into them
if(MSVC)
    target_link_options(${TEST_EXE_NAME} PRIVATE "/DELAYLOAD:PGLib.dll" "/DELAYLOAD:PGMutagenWrapper.dll")
    target_link_libraries(${TEST_EXE_NAME} PRIVATE delayimp)
endif()

#
# Test shaders, compiled at build time like the shaders of PGLib
#
include(${CMAKE_SOURCE_DIR}/PGLib/cmake/PGShaders.cmake)
pg_add_compiled_shaders(PGTestsShaders ${CMAKE_CURRENT_SOURCE_DIR}/cshaders ${CMAKE_CURRENT_BINARY_DIR}/shaders)
add_dependencies(${TEST_EXE_NAME} PGTestsShaders)
target_include_directories(${TEST_EXE_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)

target_link_libraries(${TEST_EXE_NAME} PRIVATE
    PGLib
    GTest::gtest
    GTest::gmock
)

# The tests run from the bin folder, next to the DLLs and the dotnetlib folder they need
gtest_discover_tests(${TEST_EXE_NAME}
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
)
