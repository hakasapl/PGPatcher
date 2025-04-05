#pragma once

#include "ParallaxGenDirectory.hpp"
class PGGlobals {
private:
    static ParallaxGenDirectory* s_PGD;

public:
    static auto getPGD() -> ParallaxGenDirectory*;
    static void setPGD(ParallaxGenDirectory* pgd);
};
