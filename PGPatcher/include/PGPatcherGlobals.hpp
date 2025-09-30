#pragma once

#include "ParallaxGenConfig.hpp"
#include <sys/stat.h>

class PGPatcherGlobals {
private:
    static ParallaxGenConfig* s_PGC;

public:
    PGPatcherGlobals() = delete;
    ~PGPatcherGlobals() = delete;
    PGPatcherGlobals(const PGPatcherGlobals&) = delete;
    auto operator=(const PGPatcherGlobals&) -> PGPatcherGlobals& = delete;
    PGPatcherGlobals(PGPatcherGlobals&&) = delete;
    auto operator=(PGPatcherGlobals&&) -> PGPatcherGlobals& = delete;

    static auto getPGC() -> ParallaxGenConfig*;
    static void setPGC(ParallaxGenConfig* pgc);
};
