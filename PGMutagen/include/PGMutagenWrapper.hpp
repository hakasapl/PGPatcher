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
 * retrieving and updating model uses, and finalizing the output plugin. Every call is serialized by one mutex, forwards
 * the C# side's log messages to spdlog and throws std::runtime_error with the C# exception message if it failed.
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
        int slotID = 0; ///< 3D index of the shape the alternate texture applies to.
        int slotIDNew = 0; ///< 3D index of that shape in the output mesh; -1 deletes the alternate texture.
        std::array<std::wstring, numPluginTextureSlots> slots; ///< Texture paths of the alternate texture set.
    };

    /**
     * @brief Describes a single use of a mesh model within a plugin record.
     */
    struct ModelUse {
        std::wstring modName; ///< Name of the plugin (mod) that owns this record.
        unsigned formID = 0; ///< FormID of the record referencing this model.
        std::string subModel; ///< Sub-model identifier within the record.
        bool isWeighted = false; ///< Whether the model is an armor addon model with the weight slider enabled.
        std::wstring meshFile; ///< Path to the mesh file referenced by this record.
        bool isSinglepassMATO = false; ///< Whether this record uses single-pass MATO rendering.
        bool isIgnored = false; ///< Whether this model use should be skipped during patching.
        std::string type; ///< Record type string (e.g. "STAT", "ACTI").
        std::vector<AlternateTexture> alternateTextures; ///< List of alternate texture entries for this model.
    };

    /**
     * @brief Loads the .NET runtime, then initializes the PGMutagen C# library with the game's plugins.
     *
     * Plugins that are not in the data folder, cannot be read or have missing masters are skipped (and logged).
     *
     * @param gameType  Mutagen SkyrimRelease value (2 SkyrimSE, 3 SkyrimVR, 6 EnderalSE, 7 SkyrimSEGog).
     * @param exePath   Folder of the executable, used as the base directory the .NET runtime resolves assemblies from.
     * @param dataPath  Path to the game's Data directory.
     * @param loadOrder Active plugin file names in load order.
     * @param lang      Mutagen Language value for localized strings; 0 is English.
     * @throws std::runtime_error if the .NET runtime fails to load or the plugin environment cannot be built.
     */
    static void libInitialize(const int& gameType,
                              const std::wstring& exePath,
                              const std::wstring& dataPath,
                              const std::vector<std::wstring>& loadOrder = { },
                              const unsigned& lang = 0);

    /**
     * @brief Reads every model record of the load order (winning overrides) and captures the baseline that
     * libResetPatchingState() returns to.
     *
     * @param existingModPath PGPatcher.esp of a previous run, whose texture sets are carried over so texture sets
     * created again keep their form IDs; empty to start fresh.
     * @throws std::runtime_error if libInitialize() was not called or a plugin cannot be read.
     */
    static void libPopulateObjs(const std::filesystem::path& existingModPath = { });

    /**
     * @brief Drops every record change and new texture set made since libPopulateObjs().
     */
    static void libResetPatchingState();

    /**
     * @brief Writes PGPatcher.esp (new texture sets) and the PG_<n>.esp plugins holding the changed records to the
     * output directory. Nothing is written when no record changed.
     *
     * @param outputPath Directory the plugins are written to.
     * @param esmMode    0 = ESM flag only PGPatcher.esp, 1 = ESM flag all output plugins, 2 = no ESM flags.
     */
    static void libFinalize(const std::filesystem::path& outputPath,
                            int esmMode);

    /**
     * @brief Writes the stock records file: the stock and the written model path and alternate textures of every
     * record model libSetModelUses() changed, and the stock texture set every created texture set replaced. Always
     * written, with no records when nothing changed.
     *
     * @param filePath The file to write.
     */
    static void libSaveStockRecords(const std::filesystem::path& filePath);

    /**
     * @brief Retrieves all plugin records that reference the given model path.
     *
     * @param modelPath Mesh path relative to the data folder (e.g. L"meshes\\foo\\bar.nif"), compared without
     * regard to case.
     * @return One ModelUse per record model that uses the mesh, with its alternate textures; empty if none does.
     */
    static std::vector<ModelUse> libGetModelUses(const std::wstring& modelPath);

    /**
     * @brief Points the given record models at their output meshes and alternate texture sets. Only modName, formID,
     * subModel, meshFile and alternateTextures are used; records that nothing changes stay untouched.
     *
     * @param modelUses Model uses to update.
     * @throws std::runtime_error if a record or sub model cannot be found.
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

    /**
     * @brief Whether any record has been overridden by the updates so far, which is when libFinalize() writes a
     * plugin.
     *
     * @return true if at least one record was modified.
     */
    static bool libHasModifiedRecords();

private:
    // Helpers.
    static std::wstring utf8toUTF16(const std::string& str);
    static std::string utf16toUTF8(const std::wstring& wStr);
};
