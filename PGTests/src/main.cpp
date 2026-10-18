#include "TestUtil.hpp"

#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <windows.h>

namespace {
std::filesystem::path executableDir()
{
    std::array<wchar_t, MAX_PATH> buffer { };
    if (!GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH)) {
        std::cerr << "Error getting executable path: " << GetLastError() << "\n";
        exit(1);
    }

    return std::filesystem::path(buffer.data()).parent_path();
}

// PGLib.dll and PGMutagenWrapper.dll are delay loaded because they need the .NET host in the dotnetlib folder, which is
// added to the DLL search path here before the first call into them (the same as the applications do).
void configureDotNetLibDirectory(const std::filesystem::path& exeDir)
{
    const auto libDir = exeDir / "dotnetlib";
    if (!std::filesystem::exists(libDir))
        return;

    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        std::cerr << "Failed to configure DLL search directories.\n";
        exit(1);
    }

    if (!AddDllDirectory(libDir.c_str())) {
        std::cerr << "Failed to add dotnetlib directory to DLL search path.\n";
        exit(1);
    }
}

/**
 * @brief Gives every test a log of its own, so the tests pass in any order and more than once in one process
 * (--gtest_shuffle, --gtest_repeat): the messages that earlier tests logged are forgotten, and Logger logs a warning or
 * an error again that an earlier test logged instead of suppressing it as a repeat.
 */
class FreshLogPerTest final : public ::testing::EmptyTestEventListener {
public:
    void OnTestStart([[maybe_unused]] const ::testing::TestInfo& testInfo) override
    {
        Logger::resetToRunStart();
        TestUtil::TestLog::clear();
    }
};
}

int main(int argc,
         char** argv)
{
    configureDotNetLibDirectory(executableDir());

    ::testing::InitGoogleTest(&argc, argv);

    // Everything PGLib logs is captured so tests can assert on it.
    TestUtil::TestLog::install();

    // Nothing has been logged yet, so this marks an empty suppression baseline for the tests to start from. Only
    // LoggerTest marks the run start again, which adds a message of its own that no other test logs.
    Logger::markRunStart();
    ::testing::UnitTest::GetInstance()->listeners().Append(
        std::make_unique<FreshLogPerTest>().release()); // gtest owns its listeners

    // The tests run on this thread, which is the main thread as far as ExceptionHandler is concerned.
    ExceptionHandler::setMainThread();
    ExceptionHandler::setTaskGuard(ExceptionHandler::cppTraceGuard);

    return RUN_ALL_TESTS();
}
