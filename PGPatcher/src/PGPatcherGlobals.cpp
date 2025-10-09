#include "PGPatcherGlobals.hpp"

#include "ParallaxGenConfig.hpp"

#include <filesystem>

ParallaxGenConfig* PGPatcherGlobals::s_PGC = nullptr;
auto PGPatcherGlobals::getPGC() -> ParallaxGenConfig* { return s_PGC; }
void PGPatcherGlobals::setPGC(ParallaxGenConfig* pgc) { s_PGC = pgc; }

std::filesystem::path PGPatcherGlobals::s_EXE_PATH = "";
auto PGPatcherGlobals::getEXEPath() -> std::filesystem::path { return s_EXE_PATH; }
void PGPatcherGlobals::setEXEPath(const std::filesystem::path& exePath) { s_EXE_PATH = exePath; }
