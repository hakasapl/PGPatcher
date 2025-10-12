#pragma once

#include "ModManagerDirectory.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"

#include <filesystem>
#include <unordered_set>

class PGGlobals {
private:
    static ParallaxGenDirectory* s_PGD;
    static ParallaxGenD3D* s_PGD3D;
    static ModManagerDirectory* s_MMD;

public:
    static inline std::unordered_set<std::filesystem::path> s_foldersToMap
        = { "meshes", "textures", "pbrnifpatcher", "lightplacer" };

    static auto getPGD() -> ParallaxGenDirectory*;
    static void setPGD(ParallaxGenDirectory* pgd);

    static auto getPGD3D() -> ParallaxGenD3D*;
    static void setPGD3D(ParallaxGenD3D* pgd3d);

    static auto getMMD() -> ModManagerDirectory*;
    static void setMMD(ModManagerDirectory* mmd);
};
