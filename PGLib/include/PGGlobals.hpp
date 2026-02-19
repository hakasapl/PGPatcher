#pragma once

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "common/BethesdaGame.hpp"
#include "util/TaskQueue.hpp"


#include <filesystem>
#include <unordered_set>

class PGGlobals {
private:
    static BethesdaGame* s_BG;
    static PGDirectory* s_PGD;
    static PGD3D* s_PGD3D;
    static PGModManager* s_PGMM;

public:
    static inline std::unordered_set<std::filesystem::path> s_foldersToMap
        = {"meshes", "textures", "pbrnifpatcher", "lightplacer"};

    static auto getBG() -> BethesdaGame*;
    static auto isBGSet() -> bool;
    static void setBG(BethesdaGame* bg);

    static auto getPGD() -> PGDirectory*;
    static auto isPGDSet() -> bool;
    static void setPGD(PGDirectory* pgd);

    static auto getPGD3D() -> PGD3D*;
    static auto isPGD3DSet() -> bool;
    static void setPGD3D(PGD3D* pgd3d);

    static auto getPGMM() -> PGModManager*;
    static auto isPGMMSet() -> bool;
    static void setPGMM(PGModManager* pgmm);

    static auto getFileSaver() -> TaskQueue&;
};
