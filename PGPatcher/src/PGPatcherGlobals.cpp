#include "PGPatcherGlobals.hpp"

#include "GUI/WXLoggerSink.hpp"
#include "PGConfig.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>

PGConfig* PGPatcherGlobals::s_pgc = nullptr;
PGConfig* PGPatcherGlobals::pgc() { return s_pgc; }
void PGPatcherGlobals::setPGC(PGConfig* pgc) { s_pgc = pgc; }

std::filesystem::path PGPatcherGlobals::s_exePath = "";
std::filesystem::path PGPatcherGlobals::exePath() { return s_exePath; }
void PGPatcherGlobals::setEXEPath(const std::filesystem::path& exePath) { s_exePath = exePath; }

std::shared_ptr<WXLoggerSink<std::mutex>> PGPatcherGlobals::s_wxLoggerSink = nullptr;
std::shared_ptr<WXLoggerSink<std::mutex>> PGPatcherGlobals::wxLoggerSink() { return s_wxLoggerSink; }
void PGPatcherGlobals::setWXLoggerSink(std::shared_ptr<WXLoggerSink<std::mutex>> sink)
{
    s_wxLoggerSink = std::move(sink);
}

bool PGPatcherGlobals::s_isDarkMode = false;
bool PGPatcherGlobals::isDarkMode() { return s_isDarkMode; }
void PGPatcherGlobals::setIsDarkMode(bool isDarkMode) { s_isDarkMode = isDarkMode; }
