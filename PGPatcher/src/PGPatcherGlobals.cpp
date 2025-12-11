#include "PGPatcherGlobals.hpp"

#include "GUI/WXLoggerSink.hpp"
#include "ParallaxGenConfig.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>

ParallaxGenConfig* PGPatcherGlobals::s_PGC = nullptr;
auto PGPatcherGlobals::getPGC() -> ParallaxGenConfig* { return s_PGC; }
void PGPatcherGlobals::setPGC(ParallaxGenConfig* pgc) { s_PGC = pgc; }

std::filesystem::path PGPatcherGlobals::s_EXE_PATH = "";
auto PGPatcherGlobals::getEXEPath() -> std::filesystem::path { return s_EXE_PATH; }
void PGPatcherGlobals::setEXEPath(const std::filesystem::path& exePath) { s_EXE_PATH = exePath; }

std::shared_ptr<WXLoggerSink<std::mutex>> PGPatcherGlobals::s_WXLoggerSink = nullptr;
auto PGPatcherGlobals::getWXLoggerSink() -> std::shared_ptr<WXLoggerSink<std::mutex>> { return s_WXLoggerSink; }
void PGPatcherGlobals::setWXLoggerSink(std::shared_ptr<WXLoggerSink<std::mutex>> sink)
{
    s_WXLoggerSink = std::move(sink);
}
