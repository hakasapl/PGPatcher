#pragma once

#include <filesystem>

#include "NifFile.hpp"

#include "Patcher.hpp"

/**
 * @class Patcher
 * @brief Base class for all patchers
 */
class PatcherMesh : public Patcher {
private:
    // Instance vars
    std::filesystem::path m_nifPath; /** Stores the path to the NIF file currently being patched */
    nifly::NifFile* m_nif; /** Stores the NIF object itself */

protected:
    /**
     * @brief Get the NIF path for the current patcher (used only within child patchers)
     *
     * @return std::filesystem::path Path to NIF
     */
    [[nodiscard]] auto getNIFPath() const -> std::filesystem::path;

    /**
     * @brief Get the NIF object for the current patcher (used only within child patchers)
     *
     * @return nifly::NifFile* pointer to NIF object
     */
    [[nodiscard]] auto getNIF() const -> nifly::NifFile*;

    void setNIF(nifly::NifFile* nif);

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object
     * @param patcherName Name of patcher
     */
    PatcherMesh(
        std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave = true);
};
