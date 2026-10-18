#pragma once

#include "pgutil/PGTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TestUtil {

/**
 * @brief Writes small Skyrim SE plugins (ESM/ESP) for tests: statics, texture sets, material objects and armor addons.
 *
 * Form IDs are given in full, so the caller chooses the mod index (0x00 for the first master, the number of masters for
 * the plugin's own records). Strings are written as Windows-1252/ASCII zstrings.
 */
class PluginBuilder {
public:
    /// @brief An alternate texture of a model: the shape it applies to and the texture set record it points to.
    struct AlternateTexture {
        std::string shapeName;
        uint32_t textureSetFormID = 0;
        int32_t index3D = 0;
    };

    /// @brief The models of an armor addon. Empty strings leave the model out.
    struct ArmorAddonModels {
        std::string maleWorld;
        std::string femaleWorld;
        std::string maleFirstPerson;
        std::string femaleFirstPerson;
        bool isMaleWeightSliderEnabled = false;
        bool isFemaleWeightSliderEnabled = false;
        std::vector<AlternateTexture> maleWorldAlternateTextures;
    };

    /// @brief Record flag that marks a record as ignored by PGPatcher.
    static constexpr uint32_t ignoredRecordFlag = 0x01000000;

private:
    struct Record {
        std::string type;
        uint32_t formID = 0;
        uint32_t flags = 0;
        std::vector<std::byte> data;
    };

    std::string m_fileName;
    bool m_isMaster;
    std::vector<std::string> m_masters;
    std::vector<Record> m_records;

    void addRecord(const std::string& type,
                   uint32_t formID,
                   uint32_t flags,
                   std::vector<std::byte> data);

public:
    /**
     * @param fileName Plugin file name, written into nothing but used for write()
     * @param isMaster Whether the ESM header flag is set
     * @param masters Master plugin file names, in order
     */
    PluginBuilder(std::string fileName,
                  bool isMaster,
                  std::vector<std::string> masters = { });

    /// @brief Adds a STAT record. materialFormID links a MATO record (0 for none).
    void addStatic(uint32_t formID,
                   const std::string& editorID,
                   const std::string& model,
                   const std::vector<AlternateTexture>& alternateTextures = { },
                   uint32_t materialFormID = 0,
                   uint32_t recordFlags = 0);

    /**
     * @brief Adds a record that is made of an editor ID, object bounds and a model, such as a TREE, GRAS or ACTI.
     *
     * @param type Record type, for example "TREE"
     * @param formID Full form ID
     * @param editorID Editor ID
     * @param model Path of the model, relative to the meshes folder
     * @param bounds Value of the OBND subrecord
     */
    void addModelRecord(const std::string& type,
                        uint32_t formID,
                        const std::string& editorID,
                        const std::string& model,
                        const PGTypes::ObjectBounds& bounds = { });

    /// @brief Adds a TXST record with the given slot textures (paths relative to the textures folder).
    void addTextureSet(uint32_t formID,
                       const std::string& editorID,
                       const std::array<std::string,
                                        8>& textures);

    /// @brief Adds a MATO record.
    void addMaterialObject(uint32_t formID,
                           const std::string& editorID,
                           const std::string& model,
                           bool isSinglePass);

    /// @brief Adds an ARMA record.
    void addArmorAddon(uint32_t formID,
                       const std::string& editorID,
                       const ArmorAddonModels& models);

    /// @brief Serializes the plugin.
    [[nodiscard]] std::vector<std::byte> bytes() const;

    /// @brief Writes the plugin into a folder under its file name, creating the folder.
    void writeTo(const std::filesystem::path& folder) const;

    [[nodiscard]] const std::string& fileName() const;
};

}
