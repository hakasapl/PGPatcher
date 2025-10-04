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
    struct PatchedTextureSetsHash {
        auto operator()(const std::tuple<std::filesystem::path, uint32_t>& key) const -> std::size_t
        {
            return std::hash<std::filesystem::path>()(std::get<0>(key)) ^ std::hash<uint32_t>()(std::get<1>(key));
        }
    };

    struct PatchedTextureSet {
        NIFUtil::TextureSet original;
        std::unordered_map<uint32_t, NIFUtil::TextureSet> patchResults;
    };

    static std::shared_mutex s_patchedTextureSetsMutex;
    static std::unordered_map<std::filesystem::path, std::unordered_map<uint32_t, PatchedTextureSet>>
        s_patchedTextureSets;

public:
    static auto getTextureSet(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape)
        -> NIFUtil::TextureSet;
    static auto setTextureSet(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape,
        const NIFUtil::TextureSet& textures) -> bool;
    static void clearTextureSets(const std::filesystem::path& nifPath);

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
