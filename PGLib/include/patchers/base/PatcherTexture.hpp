#pragma once

#include "patchers/base/Patcher.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <string>

/**
 * @class Patcher
 * @brief Base class for all patchers
 */
class PatcherTexture : public Patcher {
private:
    // Instance vars
    std::filesystem::path m_ddsPath; /** Stores the path to the NIF file currently being patched */
    DirectX::ScratchImage* m_dds; /** Stores the NIF object itself */

protected:
    /**
     * @brief Get the NIF path for the current patcher (used only within child patchers)
     *
     * @return std::filesystem::path Path to NIF
     */
    [[nodiscard]] auto getDDSPath() const -> std::filesystem::path;

    /**
     * @brief Get the NIF object for the current patcher (used only within child patchers)
     *
     * @return nifly::NifFile* pointer to NIF object
     */
    [[nodiscard]] auto getDDS() const -> DirectX::ScratchImage*;

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object
     * @param patcherName Name of patcher
     */
    PatcherTexture(std::filesystem::path ddsPath, DirectX::ScratchImage* dds, std::string patcherName);
};
