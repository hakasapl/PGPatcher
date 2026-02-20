#pragma once

#include <array>
#include <atomic>
#include <ctime>
#include <excpt.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <windows.h>

#include <minidumpapiset.h>
#include <tlhelp32.h>

/**
 * @brief Provides static handlers for Windows exception handling and utility methods.
 *
 * All methods are static; the class is used as a pure utility namespace with additional
 * Windows-specific helpers for crash-dump generation and process introspection.
 */
class PGHandlers {
private:
    // Global variables for crash handling
    static inline std::atomic<bool> s_crashLogged {false};

    static constexpr const char* CRASHDUMP_DIR = "log";

    static inline _MINIDUMP_TYPE s_miniDumpType = MiniDumpWithPrivateWriteCopyMemory;

public:
    /**
     * @brief Structured-exception handler that writes a minidump and logs the crash.
     *
     * Creates a timestamped minidump file under the "log" directory, prints a message to
     * stderr, and returns EXCEPTION_EXECUTE_HANDLER to terminate the process. Only the
     * first call produces a dump; subsequent calls return EXCEPTION_CONTINUE_SEARCH.
     *
     * @param exceptionInfo Pointer to the Windows exception information provided by the OS.
     * @return EXCEPTION_EXECUTE_HANDLER after writing the dump, or EXCEPTION_CONTINUE_SEARCH
     *         if a dump has already been written.
     */
    static auto WINAPI customExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) -> LONG
    {
        if (s_crashLogged.exchange(true)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Get the current timestamp
        std::string timestamp;
        const time_t t = time(nullptr);
        tm tm {};
        if (localtime_s(&tm, &t) == 0) {
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            timestamp = "_" + oss.str();
        }

        // Create the dump file path
        const std::filesystem::path dumpFilePath
            = std::filesystem::path(CRASHDUMP_DIR) / ("pg_crash" + timestamp + ".dmp");
        std::filesystem::create_directories(CRASHDUMP_DIR);

        // Post message to standard console
        std::cerr << "Uh oh! Really bad things happened. PGPatcher has crashed. Please wait while the crash dump \""
                  << dumpFilePath << "\" is generated. Please include this dump in your bug report.\n";

        // Open the file for writing the dump
        HANDLE hFile = CreateFileA(
            dumpFilePath.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create dump file: " << GetLastError() << "\n";
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Write the dump
        MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo;
        dumpExceptionInfo.ThreadId = GetCurrentThreadId();
        dumpExceptionInfo.ExceptionPointers = exceptionInfo;
        dumpExceptionInfo.ClientPointers = TRUE;

        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), hFile, s_miniDumpType, &dumpExceptionInfo, nullptr, nullptr);

        CloseHandle(hFile);

        // Continue searching or terminate the process
        return EXCEPTION_EXECUTE_HANDLER;
    }

    /**
     * @brief Get the filesystem path of the running executable.
     *
     * @return Absolute path to the current executable, or an empty path if it cannot be
     *         determined or does not exist on disk.
     */
    static auto getExePath() -> std::filesystem::path
    {
        std::array<wchar_t, MAX_PATH> buffer {};
        if (GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH) == 0) {
            return {};
        }

        std::filesystem::path outPath = std::filesystem::path(buffer.data());

        if (std::filesystem::exists(outPath)) {
            return outPath;
        }

        return {};
    }

    /**
     * @brief Check whether the process is running under Mod Organizer 2's virtual filesystem.
     *
     * Enumerates loaded modules of the current process and looks for "usvfs_x64.dll", which
     * is injected by MO2's USVFS layer.
     *
     * @return true if usvfs_x64.dll is loaded in the process, false otherwise.
     */
    static auto isUnderUSVFS() -> bool
    {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        MODULEENTRY32W me32;
        me32.dwSize = sizeof(MODULEENTRY32W);

        if (Module32FirstW(hSnapshot, &me32) != 0) {
            do {
                const std::wstring moduleName(me32.szModule);
                if (moduleName == L"usvfs_x64.dll") {
                    CloseHandle(hSnapshot);
                    return true;
                }
            } while (Module32NextW(hSnapshot, &me32) != 0);
        }

        CloseHandle(hSnapshot);
        return false;
    }
};
