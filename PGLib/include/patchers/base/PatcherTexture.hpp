#pragma once

#include "patchers/base/Patcher.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <string>

/**
 * @class PatcherTexture
 * @brief Base class for all texture patchers: holds the DDS being patched
 */
class PatcherTexture : public Patcher {
private:
    // Instance vars.
    std::filesystem::path m_ddsPath; /** Stores the path to the DDS file currently being patched */
    DirectX::ScratchImage* m_dds; /** Stores the loaded DDS image itself */

protected:
    /**
     * @brief Get the DDS path for the current patcher (used only within child patchers)
     *
     * @return std::filesystem::path Path to the DDS relative to the data folder
     */
    [[nodiscard]] std::filesystem::path ddsPath() const;

    /**
     * @brief Get the DDS image for the current patcher (used only within child patchers)
     *
     * @return DirectX::ScratchImage* pointer to the loaded image, may be nullptr
     */
    [[nodiscard]] DirectX::ScratchImage* dds() const;

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param ddsPath Path to the DDS being patched
     * @param dds Loaded DDS image
     * @param patcherName Name of patcher
     */
    PatcherTexture(std::filesystem::path ddsPath,
                   DirectX::ScratchImage* dds,
                   std::string patcherName);
};
