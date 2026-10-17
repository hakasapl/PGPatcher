#include "PluginBuilder.hpp"

#include "TestUtil.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace TestUtil {

namespace {
/**
 * @brief Little endian byte writer for the plugin format.
 */
class Bytes {
private:
    std::vector<std::byte> m_data;

public:
    template<typename T> void write(const T& value)
    {
        std::array<std::byte, sizeof(T)> raw { };
        std::memcpy(raw.data(), &value, sizeof(T));
        m_data.insert(m_data.end(), raw.begin(), raw.end());
    }

    void writeRaw(const std::string& text)
    {
        for (const char ch : text)
            m_data.push_back(static_cast<std::byte>(ch));
    }

    void writeZString(const std::string& text)
    {
        writeRaw(text);
        m_data.push_back(std::byte { 0 });
    }

    void writeBytes(const std::vector<std::byte>& bytes) { m_data.insert(m_data.end(), bytes.begin(), bytes.end()); }

    /// @brief Writes a subrecord: type, 16 bit size, data.
    void writeSub(const std::string& type,
                  const std::vector<std::byte>& data)
    {
        writeRaw(type);
        write<uint16_t>(static_cast<uint16_t>(data.size()));
        writeBytes(data);
    }

    void writeSubZString(const std::string& type,
                         const std::string& text)
    {
        Bytes data;
        data.writeZString(text);
        writeSub(type, data.data());
    }

    [[nodiscard]] const std::vector<std::byte>& data() const { return m_data; }
};

constexpr uint16_t formVersion = 44; // Skyrim SE
constexpr uint32_t masterFlag = 0x1;

std::vector<std::byte> alternateTexturesSub(const std::vector<PluginBuilder::AlternateTexture>& textures)
{
    Bytes data;
    data.write<uint32_t>(static_cast<uint32_t>(textures.size()));
    for (const auto& texture : textures) {
        data.write<uint32_t>(static_cast<uint32_t>(texture.shapeName.size()));
        data.writeRaw(texture.shapeName);
        data.write<uint32_t>(texture.textureSetFormID);
        data.write<int32_t>(texture.index3D);
    }
    return data.data();
}

std::vector<std::byte> objectBoundsSub(const PGTypes::ObjectBounds& bounds = { })
{
    Bytes data;
    for (const auto value : bounds.min)
        data.write<int16_t>(value);
    for (const auto value : bounds.max)
        data.write<int16_t>(value);
    return data.data();
}
}

PluginBuilder::PluginBuilder(std::string fileName,
                             bool isMaster,
                             std::vector<std::string> masters)
    : m_fileName(std::move(fileName))
    , m_isMaster(isMaster)
    , m_masters(std::move(masters))
{
}

void PluginBuilder::addRecord(const std::string& type,
                              uint32_t formID,
                              uint32_t flags,
                              std::vector<std::byte> data)
{
    m_records.push_back({ .type = type, .formID = formID, .flags = flags, .data = std::move(data) });
}

void PluginBuilder::addStatic(uint32_t formID,
                              const std::string& editorID,
                              const std::string& model,
                              const std::vector<AlternateTexture>& alternateTextures,
                              uint32_t materialFormID,
                              uint32_t recordFlags)
{
    Bytes record;
    record.writeSubZString("EDID", editorID);
    record.writeSub("OBND", objectBoundsSub());
    record.writeSubZString("MODL", model);
    if (!alternateTextures.empty())
        record.writeSub("MODS", alternateTexturesSub(alternateTextures));

    Bytes dnam;
    dnam.write<float>(90);
    dnam.write<uint32_t>(materialFormID);
    record.writeSub("DNAM", dnam.data());

    addRecord("STAT", formID, recordFlags, record.data());
}

void PluginBuilder::addModelRecord(const std::string& type,
                                   uint32_t formID,
                                   const std::string& editorID,
                                   const std::string& model,
                                   const PGTypes::ObjectBounds& bounds)
{
    Bytes record;
    record.writeSubZString("EDID", editorID);
    record.writeSub("OBND", objectBoundsSub(bounds));
    record.writeSubZString("MODL", model);

    addRecord(type, formID, 0, record.data());
}

void PluginBuilder::addTextureSet(uint32_t formID,
                                  const std::string& editorID,
                                  const std::array<std::string,
                                                   8>& textures)
{
    Bytes record;
    record.writeSubZString("EDID", editorID);
    record.writeSub("OBND", objectBoundsSub());
    for (size_t slot = 0; slot < textures.size(); slot++) {
        if (textures.at(slot).empty())
            continue;

        record.writeSubZString("TX0" + std::to_string(slot), textures.at(slot));
    }

    Bytes dnam;
    dnam.write<uint16_t>(0);
    record.writeSub("DNAM", dnam.data());

    addRecord("TXST", formID, 0, record.data());
}

void PluginBuilder::addMaterialObject(uint32_t formID,
                                      const std::string& editorID,
                                      const std::string& model,
                                      bool isSinglePass)
{
    Bytes record;
    record.writeSubZString("EDID", editorID);
    record.writeSubZString("MODL", model);

    // Eleven floats (falloff, noise and material scales, projection vector, normal dampener, single pass color), then
    // the flags.
    Bytes data;
    for (int i = 0; i < 11; i++)
        data.write<float>(0);
    data.write<uint32_t>(isSinglePass ? 1U : 0U);
    record.writeSub("DATA", data.data());

    addRecord("MATO", formID, 0, record.data());
}

void PluginBuilder::addArmorAddon(uint32_t formID,
                                  const std::string& editorID,
                                  const ArmorAddonModels& models)
{
    Bytes record;
    record.writeSubZString("EDID", editorID);

    Bytes bod2;
    bod2.write<uint32_t>(0x4); // body slot flags
    bod2.write<uint32_t>(1); // armor type
    record.writeSub("BOD2", bod2.data());

    Bytes rnam;
    rnam.write<uint32_t>(0);
    record.writeSub("RNAM", rnam.data());

    // Priorities, weight slider flags (0x2 is "enabled"), unknown, detection sound value, unknown, weapon adjust.
    Bytes dnam;
    dnam.write<uint8_t>(0);
    dnam.write<uint8_t>(0);
    dnam.write<uint8_t>(models.isMaleWeightSliderEnabled ? 0x2 : 0);
    dnam.write<uint8_t>(models.isFemaleWeightSliderEnabled ? 0x2 : 0);
    dnam.write<uint16_t>(0);
    dnam.write<uint8_t>(0);
    dnam.write<uint8_t>(0);
    dnam.write<float>(0);
    record.writeSub("DNAM", dnam.data());

    if (!models.maleWorld.empty()) {
        record.writeSubZString("MOD2", models.maleWorld);
        if (!models.maleWorldAlternateTextures.empty())
            record.writeSub("MO2S", alternateTexturesSub(models.maleWorldAlternateTextures));
    }
    if (!models.femaleWorld.empty())
        record.writeSubZString("MOD3", models.femaleWorld);
    if (!models.maleFirstPerson.empty())
        record.writeSubZString("MOD4", models.maleFirstPerson);
    if (!models.femaleFirstPerson.empty())
        record.writeSubZString("MOD5", models.femaleFirstPerson);

    addRecord("ARMA", formID, 0, record.data());
}

std::vector<std::byte> PluginBuilder::bytes() const
{
    Bytes out;

    // TES4 header record.
    Bytes header;
    Bytes hedr;
    hedr.write<float>(1.7F);
    hedr.write<int32_t>(static_cast<int32_t>(m_records.size()));
    hedr.write<uint32_t>(0x800 + static_cast<uint32_t>(m_records.size()));
    header.writeSub("HEDR", hedr.data());
    header.writeSubZString("CNAM", "pgtests");
    for (const auto& master : m_masters) {
        header.writeSubZString("MAST", master);
        Bytes data;
        data.write<uint64_t>(0);
        header.writeSub("DATA", data.data());
    }

    out.writeRaw("TES4");
    out.write<uint32_t>(static_cast<uint32_t>(header.data().size()));
    out.write<uint32_t>(m_isMaster ? masterFlag : 0U);
    out.write<uint32_t>(0); // form ID
    out.write<uint32_t>(0); // version control
    out.write<uint16_t>(formVersion);
    out.write<uint16_t>(0);
    out.writeBytes(header.data());

    // One top level group per record type, in a fixed order.
    std::map<std::string, std::vector<const Record*>> groups;
    for (const auto& record : m_records)
        groups[record.type].push_back(&record);

    for (const auto& [type, records] : groups) {
        Bytes group;
        for (const auto* record : records) {
            group.writeRaw(record->type);
            group.write<uint32_t>(static_cast<uint32_t>(record->data.size()));
            group.write<uint32_t>(record->flags);
            group.write<uint32_t>(record->formID);
            group.write<uint32_t>(0); // version control
            group.write<uint16_t>(formVersion);
            group.write<uint16_t>(0);
            group.writeBytes(record->data);
        }

        out.writeRaw("GRUP");
        out.write<uint32_t>(static_cast<uint32_t>(group.data().size()) + 24);
        out.writeRaw(type); // label
        out.write<int32_t>(0); // top level group
        out.write<uint16_t>(0); // stamp
        out.write<uint16_t>(0);
        out.write<uint16_t>(0); // version
        out.write<uint16_t>(0);
        out.writeBytes(group.data());
    }

    return out.data();
}

void PluginBuilder::writeTo(const std::filesystem::path& folder) const
{
    writeFile(folder / std::filesystem::path(m_fileName), bytes());
}

const std::string& PluginBuilder::fileName() const { return m_fileName; }

}
