#pragma once

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "util/TaskQueue.hpp"

#include <filesystem>
#include <unordered_set>

class PGGlobals {
private:
    static BethesdaGame* s_BG;
    static ParallaxGenDirectory* s_PGD;
    static ParallaxGenD3D* s_PGD3D;
    static ModManagerDirectory* s_MMD;

public:
    static inline std::unordered_set<std::filesystem::path> s_foldersToMap
        = { "meshes", "textures", "pbrnifpatcher", "lightplacer" };

    static auto getBG() -> BethesdaGame*;
    static auto isBGSet() -> bool;
    static void setBG(BethesdaGame* bg);

    static auto getPGD() -> ParallaxGenDirectory*;
    static auto isPGDSet() -> bool;
    static void setPGD(ParallaxGenDirectory* pgd);

    static auto getPGD3D() -> ParallaxGenD3D*;
    static auto isPGD3DSet() -> bool;
    static void setPGD3D(ParallaxGenD3D* pgd3d);

    static auto getMMD() -> ModManagerDirectory*;
    static auto isMMDSet() -> bool;
    static void setMMD(ModManagerDirectory* mmd);

    static auto getFileSaver() -> TaskQueue&;
};
