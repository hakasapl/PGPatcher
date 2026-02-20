#pragma once

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "common/BethesdaGame.hpp"
#include "util/TaskQueue.hpp"

#include <filesystem>
#include <unordered_set>

/**
 * @brief Provides global singleton accessors for the core PGPatcher subsystem objects.
 *
 * Each subsystem (BethesdaGame, PGDirectory, PGD3D, PGModManager) is stored as a static
 * pointer and accessed via typed get/set/isSet methods. Throws std::runtime_error if a
 * getter is called before the corresponding pointer has been set.
 */
class PGGlobals {
private:
    static BethesdaGame* s_BG;
    static PGDirectory* s_PGD;
    static PGD3D* s_PGD3D;
    static PGModManager* s_PGMM;

public:
    const static inline std::unordered_set<std::filesystem::path> s_foldersToMap
        = {"meshes", "textures", "pbrnifpatcher", "lightplacer"};

    /**
     * @brief Returns the global BethesdaGame instance.
     *
     * @return Pointer to the BethesdaGame object.
     * @throws std::runtime_error if the BethesdaGame has not been set.
     */
    static auto getBG() -> BethesdaGame*;

    /**
     * @brief Returns whether the global BethesdaGame pointer has been set.
     *
     * @return true if set, false otherwise.
     */
    static auto isBGSet() -> bool;

    /**
     * @brief Sets the global BethesdaGame pointer.
     *
     * @param bg Pointer to the BethesdaGame instance to store.
     */
    static void setBG(BethesdaGame* bg);

    /**
     * @brief Returns the global PGDirectory instance.
     *
     * @return Pointer to the PGDirectory object.
     * @throws std::runtime_error if the PGDirectory has not been set.
     */
    static auto getPGD() -> PGDirectory*;

    /**
     * @brief Returns whether the global PGDirectory pointer has been set.
     *
     * @return true if set, false otherwise.
     */
    static auto isPGDSet() -> bool;

    /**
     * @brief Sets the global PGDirectory pointer.
     *
     * @param pgd Pointer to the PGDirectory instance to store.
     */
    static void setPGD(PGDirectory* pgd);

    /**
     * @brief Returns the global PGD3D instance.
     *
     * @return Pointer to the PGD3D object.
     * @throws std::runtime_error if PGD3D has not been set.
     */
    static auto getPGD3D() -> PGD3D*;

    /**
     * @brief Returns whether the global PGD3D pointer has been set.
     *
     * @return true if set, false otherwise.
     */
    static auto isPGD3DSet() -> bool;

    /**
     * @brief Sets the global PGD3D pointer.
     *
     * @param pgd3d Pointer to the PGD3D instance to store.
     */
    static void setPGD3D(PGD3D* pgd3d);

    /**
     * @brief Returns the global PGModManager instance.
     *
     * @return Pointer to the PGModManager object.
     * @throws std::runtime_error if PGModManager has not been set.
     */
    static auto getPGMM() -> PGModManager*;

    /**
     * @brief Returns whether the global PGModManager pointer has been set.
     *
     * @return true if set, false otherwise.
     */
    static auto isPGMMSet() -> bool;

    /**
     * @brief Sets the global PGModManager pointer.
     *
     * @param pgmm Pointer to the PGModManager instance to store.
     */
    static void setPGMM(PGModManager* pgmm);

    /**
     * @brief Returns the singleton TaskQueue used for serialized file-save operations.
     *
     * @return Reference to the global file-saver TaskQueue.
     */
    static auto getFileSaver() -> TaskQueue&;
};
