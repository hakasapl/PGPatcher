#include "PGPatcherGlobals.hpp"
#include "PGGlobals.hpp"

ParallaxGenConfig* PGPatcherGlobals::s_PGC = nullptr;
auto PGPatcherGlobals::getPGC() -> ParallaxGenConfig* { return s_PGC; }
void PGPatcherGlobals::setPGC(ParallaxGenConfig* pgc) { s_PGC = pgc; }
