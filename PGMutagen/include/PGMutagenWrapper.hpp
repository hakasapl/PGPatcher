#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief C++ wrapper for the PGMutagen C# library that interfaces with Bethesda ES plugin files.
 *
 * Provides static methods for initializing the managed runtime, populating plugin object data,
 * retrieving and updating model uses, and finalizing the output plugin.
 */
class PGMutagenWrapper {
private:
    static constexpr int numPluginTextureSlots = 8;
    static constexpr int defaultBufferSize = 1024;

    static std::mutex s_libMutex;

    static void libLogMessageIfExists();
    static void libThrowExceptionIfExists();

public:
    /**
     * @brief Holds an alternate texture assignment for a single texture slot in a model record.
     */
    struct AlternateTexture {
        int slotID = 0; ///< Original texture slot index referenced by the plugin record.
        int slotIDNew = 0; ///< New texture slot index after patching (may differ from slotID).
        std::array<std::wstring, numPluginTextureSlots> slots; ///< Resolved texture paths for all slots.
    };

    /**
     * @brief Describes a single use of a mesh model within a plugin record.
     */
    struct ModelUse {
        std::wstring modName; ///< Name of the plugin (mod) that owns this record.
        unsigned formID = 0; ///< FormID of the record referencing this model.
        std::string subModel; ///< Sub-model identifier within the record.
        bool isWeighted = false; ///< Whether the model uses a weighted (skinned) mesh.
        std::wstring meshFile; ///< Path to the mesh file referenced by this record.
        bool isSinglepassMATO = false; ///< Whether this record uses single-pass MATO rendering.
        bool isIgnored = false; ///< Whether this model use should be skipped during patching.
        std::string type; ///< Record type string (e.g. "STAT", "ACTI").
        std::vector<AlternateTexture> alternateTextures; ///< List of alternate texture entries for this model.
    };

    /**
     * @brief Initializes the PGMutagen C# library and loads the specified game's plugin data.
     *
     * @param gameType  Integer identifier for the game type (maps to BethesdaGame::GameType).
     * @param exePath   Path to the game executable.
     * @param dataPath  Path to the game's Data directory.
     * @param loadOrder Ordered list of active plugin names; defaults to empty (auto-detected).
     * @param lang      Language code used for localised string resolution; defaults to 0 (English).
     */
    static void libInitialize(const int& gameType,
                              const std::wstring& exePath,
                              const std::wstring& dataPath,
                              const std::vector<std::wstring>& loadOrder = { },
                              const unsigned& lang = 0);

    /**
     * @brief Populates the internal plugin object graph, optionally merging an existing output mod.
     *
     * @param existingModPath Path to an existing output mod to merge into the session; empty to start fresh.
     */
    static void libPopulateObjs(const std::filesystem::path& existingModPath = { });

    /**
     * @brief Resets mutable plugin-patching state to the post-populate baseline.
     */
    static void libResetPatchingState();

    /**
     * @brief Writes all pending changes to the output plugin file and finalises the session.
     *
     * @param outputPath Path where the output plugin file should be written.
     * @param esmMode    0 = ESM flag only PGPatcher.esp, 1 = ESM flag all output plugins, 2 = no ESM flags.
     */
    static void libFinalize(const std::filesystem::path& outputPath,
                            int esmMode);

    /**
     * @brief Retrieves all plugin records that reference the given model path.
     *
     * @param modelPath Relative mesh path (e.g. "meshes/foo/bar.nif") to look up.
     * @return Vector of ModelUse structs describing each record that uses this model.
     */
    static std::vector<ModelUse> libGetModelUses(const std::wstring& modelPath);

    /**
     * @brief Pushes updated model-use records back to the C# library for serialisation into the output plugin.
     *
     * @param modelUses Updated list of ModelUse structs to write.
     */
    static void libSetModelUses(const std::vector<ModelUse>& modelUses);

    /**
     * @brief Describes an updated OBND (Object Bounds) value for a plugin record's primary model.
     */
    struct ObjectBoundsUpdate {
        std::wstring modName; ///< Name of the plugin (mod) that owns this record.
        unsigned formID = 0; ///< FormID of the record to update.
        std::array<int16_t, 3> min { }; ///< Minimum corner of the bounding box.
        std::array<int16_t, 3> max { }; ///< Maximum corner of the bounding box.
    };

    /**
     * @brief Pushes updated OBND (Object Bounds) values back to the C# library for the given records.
     *
     * @param updates Updated list of ObjectBoundsUpdate structs to write.
     */
    static void libSetObjectBounds(const std::vector<ObjectBoundsUpdate>& updates);

private:
    // Helpers.
    static std::wstring utf8toUTF16(const std::string& str);
    static std::string utf16toUTF8(const std::wstring& wStr);
};
