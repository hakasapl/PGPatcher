#pragma once

#include "ParallaxGenConfig.hpp"
#include <sys/stat.h>

class PGPatcherGlobals {
private:
    static ParallaxGenConfig* s_PGC;

public:
    static auto getPGC() -> ParallaxGenConfig*;
    static void setPGC(ParallaxGenConfig* pgc);
};
