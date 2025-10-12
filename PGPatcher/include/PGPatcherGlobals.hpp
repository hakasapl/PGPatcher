#pragma once

#include "ParallaxGenConfig.hpp"

#include <filesystem>

class PGPatcherGlobals {
private:
    static ParallaxGenConfig* s_PGC;

    static std::filesystem::path s_EXE_PATH;

public:
    PGPatcherGlobals() = delete;
    ~PGPatcherGlobals() = delete;
    PGPatcherGlobals(const PGPatcherGlobals&) = delete;
    auto operator=(const PGPatcherGlobals&) -> PGPatcherGlobals& = delete;
    PGPatcherGlobals(PGPatcherGlobals&&) = delete;
    auto operator=(PGPatcherGlobals&&) -> PGPatcherGlobals& = delete;

    static auto getPGC() -> ParallaxGenConfig*;
    static void setPGC(ParallaxGenConfig* pgc);

    static auto getEXEPath() -> std::filesystem::path;
    static void setEXEPath(const std::filesystem::path& exePath);
};
