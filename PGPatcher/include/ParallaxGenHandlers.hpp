#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <DbgHelp.h>

#include <array>
#include <filesystem>
#include <iostream>

#include <spdlog/spdlog.h>

using namespace std;

class ParallaxGenHandlers {
private:
    // Global variables for crash handling
    static inline std::atomic<bool> s_crashLogged { false };
    static inline std::atomic<bool> s_exitBlocking { true };

    static constexpr const char* CRASHDUMP_DIR = "log";

    static inline _MINIDUMP_TYPE s_miniDumpType = MiniDumpWithPrivateWriteCopyMemory;

public:
    static auto WINAPI customExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) -> LONG
    {
        if (s_crashLogged.exchange(true)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Get the current timestamp
        string timestamp;
        const time_t t = time(nullptr);
        tm tm {};
        if (localtime_s(&tm, &t) == 0) {
            ostringstream oss;
            oss << put_time(&tm, "%Y-%m-%d_%H-%M-%S");
            timestamp = "_" + oss.str();
        }

        // Create the dump file path
        const filesystem::path dumpFilePath = filesystem::path(CRASHDUMP_DIR) / ("pg_crash" + timestamp + ".dmp");
        filesystem::create_directories(CRASHDUMP_DIR);

        // Post message to standard console
        cerr << "Uh oh! Really bad things happened. ParallaxGen has crashed. Please wait while the crash dump \""
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

    static void nonBlockingExit() { s_exitBlocking.store(false); }

    static void exitBlocking()
    {
        if (!s_exitBlocking) {
            return;
        }

        cout << "Press ENTER to exit...";
        cin.get();
    }

    static auto getExePath() -> filesystem::path
    {
        array<wchar_t, MAX_PATH> buffer {};
        if (GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH) == 0) {
            return {};
        }

        filesystem::path outPath = filesystem::path(buffer.data());

        if (filesystem::exists(outPath)) {
            return outPath;
        }

        return {};
    }

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
                const wstring moduleName(me32.szModule);
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
