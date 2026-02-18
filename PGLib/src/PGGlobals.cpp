#include "PGGlobals.hpp"

#include "BethesdaGame.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "util/TaskQueue.hpp"
#include <stdexcept>

BethesdaGame* PGGlobals::s_BG = nullptr;
auto PGGlobals::getBG() -> BethesdaGame*
{
    if (!isBGSet()) {
        throw std::runtime_error("BG is not set");
    }
    return s_BG;
}
auto PGGlobals::isBGSet() -> bool { return s_BG != nullptr; }
void PGGlobals::setBG(BethesdaGame* bg) { s_BG = bg; }

ParallaxGenDirectory* PGGlobals::s_PGD = nullptr;
auto PGGlobals::getPGD() -> ParallaxGenDirectory*
{
    if (!isPGDSet()) {
        throw std::runtime_error("PGD is not set");
    }
    return s_PGD;
}
auto PGGlobals::isPGDSet() -> bool { return s_PGD != nullptr; }
void PGGlobals::setPGD(ParallaxGenDirectory* pgd) { s_PGD = pgd; }

ParallaxGenD3D* PGGlobals::s_PGD3D = nullptr;
auto PGGlobals::getPGD3D() -> ParallaxGenD3D*
{
    if (!isPGD3DSet()) {
        throw std::runtime_error("PGD3D is not set");
    }
    return s_PGD3D;
}
auto PGGlobals::isPGD3DSet() -> bool { return s_PGD3D != nullptr; }
void PGGlobals::setPGD3D(ParallaxGenD3D* pgd3d) { s_PGD3D = pgd3d; }

ModManagerDirectory* PGGlobals::s_MMD = nullptr;
auto PGGlobals::getMMD() -> ModManagerDirectory*
{
    if (!isMMDSet()) {
        throw std::runtime_error("MMD is not set");
    }
    return s_MMD;
}
auto PGGlobals::isMMDSet() -> bool { return s_MMD != nullptr; }
void PGGlobals::setMMD(ModManagerDirectory* mmd) { s_MMD = mmd; }

auto PGGlobals::getFileSaver() -> TaskQueue&
{
    static TaskQueue fileSaver;
    return fileSaver;
}
