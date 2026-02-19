#pragma once

#include "GUI/WXLoggerSink.hpp"
#include "PGConfig.hpp"

#include <filesystem>
#include <memory>
#include <mutex>

class PGPatcherGlobals {
private:
    static PGConfig* s_PGC;
    static std::shared_ptr<WXLoggerSink<std::mutex>> s_WXLoggerSink;

    static std::filesystem::path s_EXE_PATH;

    static bool s_isDarkMode;

public:
    PGPatcherGlobals() = delete;
    ~PGPatcherGlobals() = delete;
    PGPatcherGlobals(const PGPatcherGlobals&) = delete;
    auto operator=(const PGPatcherGlobals&) -> PGPatcherGlobals& = delete;
    PGPatcherGlobals(PGPatcherGlobals&&) = delete;
    auto operator=(PGPatcherGlobals&&) -> PGPatcherGlobals& = delete;

    static auto getPGC() -> PGConfig*;
    static void setPGC(PGConfig* pgc);

    static auto getEXEPath() -> std::filesystem::path;
    static void setEXEPath(const std::filesystem::path& exePath);

    static auto getWXLoggerSink() -> std::shared_ptr<WXLoggerSink<std::mutex>>;
    static void setWXLoggerSink(std::shared_ptr<WXLoggerSink<std::mutex>> sink);

    static auto isDarkMode() -> bool;
    static void setIsDarkMode(bool isDarkMode);
};
