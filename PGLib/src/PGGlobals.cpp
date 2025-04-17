#include "PGGlobals.hpp"

ParallaxGenDirectory* PGGlobals::s_PGD = nullptr;
auto PGGlobals::getPGD() -> ParallaxGenDirectory* { return s_PGD; }
void PGGlobals::setPGD(ParallaxGenDirectory* pgd) { s_PGD = pgd; }

ParallaxGenD3D* PGGlobals::s_PGD3D = nullptr;
auto PGGlobals::getPGD3D() -> ParallaxGenD3D* { return s_PGD3D; }
void PGGlobals::setPGD3D(ParallaxGenD3D* pgd3d) { s_PGD3D = pgd3d; }

ModManagerDirectory* PGGlobals::s_MMD = nullptr;
auto PGGlobals::getMMD() -> ModManagerDirectory* { return s_MMD; }
void PGGlobals::setMMD(ModManagerDirectory* mmd) { s_MMD = mmd; }

bool PGGlobals::s_highMemMode = false;
auto PGGlobals::getHighMemMode() -> bool { return s_highMemMode; }
void PGGlobals::setHighMemMode(const bool& highMemMode) { s_highMemMode = highMemMode; }
