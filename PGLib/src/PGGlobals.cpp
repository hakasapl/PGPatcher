#include "PGGlobals.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "util/TaskQueue.hpp"
#include <stdexcept>

BethesdaGame* PGGlobals::s_bg = nullptr;
auto PGGlobals::getBG() -> BethesdaGame*
{
    if (!isBGSet())
        throw std::runtime_error("BG is not set");
    return s_bg;
}
auto PGGlobals::isBGSet() -> bool { return s_bg != nullptr; }
void PGGlobals::setBG(BethesdaGame* bg) { s_bg = bg; }

PGDirectory* PGGlobals::s_pgd = nullptr;
auto PGGlobals::getPGD() -> PGDirectory*
{
    if (!isPGDSet())
        throw std::runtime_error("PGD is not set");
    return s_pgd;
}
auto PGGlobals::isPGDSet() -> bool { return s_pgd != nullptr; }
void PGGlobals::setPGD(PGDirectory* pgd) { s_pgd = pgd; }

PGD3D* PGGlobals::s_pgD3D = nullptr;
auto PGGlobals::getPGD3D() -> PGD3D*
{
    if (!isPGD3DSet())
        throw std::runtime_error("PGD3D is not set");
    return s_pgD3D;
}
auto PGGlobals::isPGD3DSet() -> bool { return s_pgD3D != nullptr; }
void PGGlobals::setPGD3D(PGD3D* pgd3d) { s_pgD3D = pgd3d; }

PGModManager* PGGlobals::s_pgmm = nullptr;
auto PGGlobals::getPGMM() -> PGModManager*
{
    if (!isPGMMSet())
        throw std::runtime_error("PGMM is not set");
    return s_pgmm;
}
auto PGGlobals::isPGMMSet() -> bool { return s_pgmm != nullptr; }
void PGGlobals::setPGMM(PGModManager* pgmm) { s_pgmm = pgmm; }

auto PGGlobals::getFileSaver() -> TaskQueue&
{
    static TaskQueue fileSaver;
    return fileSaver;
}
