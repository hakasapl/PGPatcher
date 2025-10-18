#include "PGGlobals.hpp"

#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "util/TaskQueue.hpp"

ParallaxGenDirectory* PGGlobals::s_PGD = nullptr;
auto PGGlobals::getPGD() -> ParallaxGenDirectory* { return s_PGD; }
void PGGlobals::setPGD(ParallaxGenDirectory* pgd) { s_PGD = pgd; }

ParallaxGenD3D* PGGlobals::s_PGD3D = nullptr;
auto PGGlobals::getPGD3D() -> ParallaxGenD3D* { return s_PGD3D; }
void PGGlobals::setPGD3D(ParallaxGenD3D* pgd3d) { s_PGD3D = pgd3d; }

ModManagerDirectory* PGGlobals::s_MMD = nullptr;
auto PGGlobals::getMMD() -> ModManagerDirectory* { return s_MMD; }
void PGGlobals::setMMD(ModManagerDirectory* mmd) { s_MMD = mmd; }

auto PGGlobals::getFileSaver() -> TaskQueue&
{
    static TaskQueue fileSaver;
    return fileSaver;
}
