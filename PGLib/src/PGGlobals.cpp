#include "PGGlobals.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "util/TaskQueue.hpp"
#include <stdexcept>

BethesdaGame* PGGlobals::s_bg = nullptr;
BethesdaGame* PGGlobals::bg()
{
    if (!isBGSet())
        throw std::runtime_error("BG is not set");
    return s_bg;
}
bool PGGlobals::isBGSet() { return s_bg != nullptr; }
void PGGlobals::setBG(BethesdaGame* bg) { s_bg = bg; }

PGDirectory* PGGlobals::s_pgd = nullptr;
PGDirectory* PGGlobals::pgd()
{
    if (!isPGDSet())
        throw std::runtime_error("PGD is not set");
    return s_pgd;
}
bool PGGlobals::isPGDSet() { return s_pgd != nullptr; }
void PGGlobals::setPGD(PGDirectory* pgd) { s_pgd = pgd; }

PGD3D* PGGlobals::s_pgD3D = nullptr;
PGD3D* PGGlobals::pGD3D()
{
    if (!isPGD3DSet())
        throw std::runtime_error("PGD3D is not set");
    return s_pgD3D;
}
bool PGGlobals::isPGD3DSet() { return s_pgD3D != nullptr; }
void PGGlobals::setPGD3D(PGD3D* pgd3d) { s_pgD3D = pgd3d; }

PGModManager* PGGlobals::s_pgmm = nullptr;
PGModManager* PGGlobals::pgmm()
{
    if (!isPGMMSet())
        throw std::runtime_error("PGMM is not set");
    return s_pgmm;
}
bool PGGlobals::isPGMMSet() { return s_pgmm != nullptr; }
void PGGlobals::setPGMM(PGModManager* pgmm) { s_pgmm = pgmm; }

TaskQueue& PGGlobals::fileSaver()
{
    static TaskQueue fileSaver;
    return fileSaver;
}
