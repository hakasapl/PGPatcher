#include "PGGlobals.hpp"

ParallaxGenDirectory* PGGlobals::s_PGD = nullptr;

auto PGGlobals::getPGD() -> ParallaxGenDirectory* { return s_PGD; }
void PGGlobals::setPGD(ParallaxGenDirectory* pgd) { s_PGD = pgd; }
