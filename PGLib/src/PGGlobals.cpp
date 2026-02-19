#include "PGGlobals.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "common/BethesdaGame.hpp"
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

PGDirectory* PGGlobals::s_PGD = nullptr;
auto PGGlobals::getPGD() -> PGDirectory*
{
    if (!isPGDSet()) {
        throw std::runtime_error("PGD is not set");
    }
    return s_PGD;
}
auto PGGlobals::isPGDSet() -> bool { return s_PGD != nullptr; }
void PGGlobals::setPGD(PGDirectory* pgd) { s_PGD = pgd; }

PGD3D* PGGlobals::s_PGD3D = nullptr;
auto PGGlobals::getPGD3D() -> PGD3D*
{
    if (!isPGD3DSet()) {
        throw std::runtime_error("PGD3D is not set");
    }
    return s_PGD3D;
}
auto PGGlobals::isPGD3DSet() -> bool { return s_PGD3D != nullptr; }
void PGGlobals::setPGD3D(PGD3D* pgd3d) { s_PGD3D = pgd3d; }

PGModManager* PGGlobals::s_PGMM = nullptr;
auto PGGlobals::getPGMM() -> PGModManager*
{
    if (!isPGMMSet()) {
        throw std::runtime_error("PGMM is not set");
    }
    return s_PGMM;
}
auto PGGlobals::isPGMMSet() -> bool { return s_PGMM != nullptr; }
void PGGlobals::setPGMM(PGModManager* pgmm) { s_PGMM = pgmm; }

auto PGGlobals::getFileSaver() -> TaskQueue&
{
    static TaskQueue fileSaver;
    return fileSaver;
}
