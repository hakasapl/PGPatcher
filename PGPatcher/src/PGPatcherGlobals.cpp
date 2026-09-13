#include "PGPatcherGlobals.hpp"

#include "GUI/WXLoggerSink.hpp"
#include "PGConfig.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>

PGConfig* PGPatcherGlobals::s_pgc = nullptr;
auto PGPatcherGlobals::getPGC() -> PGConfig* { return s_pgc; }
void PGPatcherGlobals::setPGC(PGConfig* pgc) { s_pgc = pgc; }

std::filesystem::path PGPatcherGlobals::s_exePath = "";
auto PGPatcherGlobals::getEXEPath() -> std::filesystem::path { return s_exePath; }
void PGPatcherGlobals::setEXEPath(const std::filesystem::path& exePath) { s_exePath = exePath; }

std::shared_ptr<WXLoggerSink<std::mutex>> PGPatcherGlobals::s_wxLoggerSink = nullptr;
auto PGPatcherGlobals::getWXLoggerSink() -> std::shared_ptr<WXLoggerSink<std::mutex>> { return s_wxLoggerSink; }
void PGPatcherGlobals::setWXLoggerSink(std::shared_ptr<WXLoggerSink<std::mutex>> sink)
{
    s_wxLoggerSink = std::move(sink);
}

bool PGPatcherGlobals::s_isDarkMode = false;
auto PGPatcherGlobals::isDarkMode() -> bool { return s_isDarkMode; }
void PGPatcherGlobals::setIsDarkMode(bool isDarkMode) { s_isDarkMode = isDarkMode; }
