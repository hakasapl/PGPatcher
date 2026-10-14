#include "PCH.h"

#include "OfflineOutput.hpp"

#include "PGPlugin.hpp"
#include "RecordEdits.hpp"
#include "Resources.hpp"
#include "Uses.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

constexpr const char* diffFile = "parallaxgen_diff.json";
constexpr const char* outputPlugin = "PGPatcher.esp";
constexpr unsigned supportedVersion = 1;
/// @brief The folder of the output's duplicate meshes, followed by the duplicate number and the original path.
constexpr std::wstring_view duplicateFolder = L"meshes\\_pgpatcher_dups\\";
constexpr std::wstring_view meshesPrefix = L"meshes\\";

using AlternateTexture = RE::TESModelTextureSwap::AlternateTexture;

/// @brief An alternate texture entry of a record model: the texture set and the 3D index the engine compares, and the
/// 3D name the record carries.
struct Entry {
    RE::BGSTextureSet* textureSet = nullptr;
    std::uint32_t index3D = 0;
    std::string name3D;
};

/// @brief A record model's state: the model path as the engine resolves it and its entries.
struct ModelState {
    std::filesystem::path model;
    std::vector<Entry> entries;
    /// @brief Whether an entry names a texture set the loaded game does not have (a state of the file only).
    bool hasUnresolvedTextureSet = false;
};

/// @brief One record model of the stock records file.
struct StockRecord {
    std::string plugin;
    std::uint32_t formID = 0;
    std::string subModel;
    ModelState stock;
    ModelState patched;
};

std::string stockRecordsPath() { return StringUtil::utf16toWindows1252(PGPlugin::stockRecordsFile); }

/// @brief The engine's bytes of a string of the file, which the offline plugin reader decoded as UTF-8.
std::string engineString(const std::string& utf8)
{
    return StringUtil::utf16toWindows1252(StringUtil::utf8toUTF16(utf8));
}

/// @brief The file name of the plugin a form comes from, as the engine stores it; empty for a form without a file.
std::string pluginOf(const RE::TESForm& form)
{
    const auto* const file = form.GetFile(0);
    return file ? std::string(file->GetFilename()) : std::string();
}

/// @brief What a record model is looked up by: its plugin (without regard to case), form ID and sub model.
std::string keyOf(const std::string& plugin,
                  std::uint32_t formID,
                  const std::string& subModel)
{
    return std::format("{}:{:08X}:{}", StringUtil::toLowerASCIIFast(plugin), formID, subModel);
}

/// @brief What the state the output wrote and a loaded record model are matched by: the model path and the entries
/// by 3D index and texture set.
std::wstring signatureOf(const ModelState& state)
{
    auto signature = state.model.wstring();
    for (const auto& entry : state.entries)
        signature += std::format(L"|{}:{}", entry.index3D, static_cast<const void*>(entry.textureSet));

    return signature;
}

RE::BGSTextureSet* lookupTextureSet(RE::TESDataHandler& dataHandler,
                                    const nlohmann::json& json,
                                    const char* pluginKey,
                                    const char* formIDKey)
{
    const auto plugin = json.value(pluginKey, std::string());
    if (plugin.empty()) {
        // An entry without a texture set.
        return nullptr;
    }

    return dataHandler.LookupForm<RE::BGSTextureSet>(json.value(formIDKey, 0U), engineString(plugin));
}

/// @brief A model state of the file, its texture sets resolved.
ModelState readState(RE::TESDataHandler& dataHandler,
                     const nlohmann::json& json)
{
    ModelState state;
    state.model = Uses::normalizedModelPath(engineString(json.value("model", std::string())).c_str());
    for (const auto& entryJSON : json.value("alternatetextures", nlohmann::json::array())) {
        Entry entry;
        entry.textureSet = lookupTextureSet(dataHandler, entryJSON, "plugin", "formid");
        if (!entry.textureSet && !entryJSON.value("plugin", std::string()).empty())
            state.hasUnresolvedTextureSet = true;

        entry.index3D = entryJSON.value("index", 0U);
        entry.name3D = engineString(entryJSON.value("name", std::string()));
        state.entries.push_back(std::move(entry));
    }

    return state;
}

ModelState currentState(const ModelElement& element)
{
    ModelState state;
    state.model = Uses::normalizedModelPath(element.model->GetModel());
    if (element.swap && element.swap->alternateTextures) {
        for (std::uint32_t i = 0; i < element.swap->numAlternateTextures; i++) {
            const auto& entry = element.swap->alternateTextures[i];
            state.entries.push_back(
                { .textureSet = entry.textureSet, .index3D = entry.index3D, .name3D = entry.name3D.c_str() });
        }
    }

    return state;
}

bool isSameState(const ModelState& a,
                 const ModelState& b)
{
    if (a.model != b.model || a.entries.size() != b.entries.size())
        return false;

    for (std::size_t i = 0; i < a.entries.size(); i++)
        if (a.entries.at(i).textureSet != b.entries.at(i).textureSet
            || a.entries.at(i).index3D != b.entries.at(i).index3D)
            return false;

    return true;
}

/**
 * @brief Writes a state into a record model. The entries are written in place while their number is unchanged;
 * otherwise the swap gets an array from the engine's heap, and the record's own array stays with the record (a few
 * bytes, once per session).
 */
void applyState(const ModelElement& element,
                const ModelState& state)
{
    setModelPath(*element.model, state.model);
    auto* const swap = element.swap;
    if (!swap)
        return;

    const auto count = static_cast<std::uint32_t>(state.entries.size());
    if (swap->numAlternateTextures == count && (swap->alternateTextures || !count)) {
        for (std::uint32_t i = 0; i < count; i++) {
            auto& entry = swap->alternateTextures[i];
            entry.textureSet = state.entries.at(i).textureSet;
            entry.index3D = state.entries.at(i).index3D;
            entry.name3D = state.entries.at(i).name3D.c_str();
        }

        return;
    }

    AlternateTexture* entries = nullptr;
    if (count) {
        entries = RE::malloc<AlternateTexture>(sizeof(AlternateTexture) * count);
        if (!entries)
            throw std::runtime_error("the engine's heap refused the alternate textures");

        for (std::uint32_t i = 0; i < count; i++) {
            auto* const entry = new (&entries[i]) AlternateTexture { }; // NOLINT(cppcoreguidelines-owning-memory)
            entry->textureSet = state.entries.at(i).textureSet;
            entry->index3D = state.entries.at(i).index3D;
            entry->unk0C = 0;
            entry->name3D = state.entries.at(i).name3D.c_str();
        }
    }

    swap->alternateTextures = entries;
    swap->numAlternateTextures = count;
}

/// @brief The original path of a duplicate path: meshes\_pgpatcher_dups\<n>\rest is meshes\rest.
std::filesystem::path originalPathOf(const std::filesystem::path& duplicatePath)
{
    const auto rest = duplicatePath.wstring().substr(duplicateFolder.size());
    const auto separator = rest.find(L'\\');
    if (separator == std::wstring::npos)
        return duplicatePath;

    return std::wstring(meshesPrefix) + rest.substr(separator + 1);
}

/// @brief Whether a texture set is a record of the output's plugin.
bool isOutputTextureSet(const RE::BGSTextureSet* textureSet)
{
    return textureSet && StringUtil::asciiFastIEquals(pluginOf(*textureSet), std::string(outputPlugin));
}

std::optional<ModelElement> elementOf(RE::TESForm& form,
                                      const std::string& subModel)
{
    for (const auto& element : Uses::modelElements(form))
        if (element.subModel == subModel)
            return element;

    return std::nullopt;
}

/// @brief Reads and checks the stock records file, with the errors restore() documents.
nlohmann::json readStockRecords()
{
    std::vector<std::byte> bytes;
    if (!Resources::readFile(stockRecordsPath(), bytes))
        throw std::runtime_error("the stock records file of the offline output cannot be read");

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(bytes.begin(), bytes.end());
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::format("the stock records file of the offline output is not valid JSON: {}", e.what()));
    }
    if (!json.is_object())
        throw std::runtime_error("the stock records file of the offline output is not a JSON object");

    const auto version = json.value("version", 0U);
    if (version > supportedVersion) {
        throw std::runtime_error(std::format(
            "the stock records file of the offline output has version {}, newer than this PGLite supports", version));
    }

    return json;
}

}

bool OfflineOutput::isPresent()
{
    if (Resources::exists(diffFile))
        return true;

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    return dataHandler
        && (dataHandler->LookupLoadedModByName(outputPlugin) || dataHandler->LookupLoadedLightModByName(outputPlugin));
}

bool OfflineOutput::hasStockRecords() { return Resources::exists(stockRecordsPath()); }

std::unordered_set<std::filesystem::path> OfflineOutput::generatedTextures()
{
    std::unordered_set<std::filesystem::path> textures;
    if (!hasStockRecords())
        return textures;

    for (const auto& texture : readStockRecords().value("generatedtextures", nlohmann::json::array()))
        if (texture.is_string())
            textures.insert(StringUtil::utf8toUTF16(texture.get<std::string>()));

    return textures;
}

OfflineOutput::Result OfflineOutput::restore()
{
    const auto json = readStockRecords();

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        throw std::runtime_error("the data handler is not available");

    // The records the output changed, by key, and by the state the output wrote them in, which a copy of such a
    // record carries.
    std::vector<StockRecord> records;
    for (const auto& item : json.value("records", nlohmann::json::array())) {
        StockRecord record;
        record.plugin = engineString(item.value("plugin", std::string()));
        record.formID = item.value("formid", 0U);
        record.subModel = item.value("submodel", std::string());
        record.stock = readState(*dataHandler, item.value("stock", nlohmann::json::object()));
        record.patched = readState(*dataHandler, item.value("patched", nlohmann::json::object()));
        records.push_back(std::move(record));
    }
    std::unordered_map<std::string, const StockRecord*> byKey;
    std::unordered_map<std::wstring, const StockRecord*> bySignature;
    for (const auto& record : records) {
        byKey.emplace(keyOf(record.plugin, record.formID, record.subModel), &record);
        if (!record.patched.hasUnresolvedTextureSet)
            bySignature.emplace(signatureOf(record.patched), &record);
    }

    // The texture sets the output created, each with the stock set it replaced.
    std::unordered_map<const RE::BGSTextureSet*, RE::BGSTextureSet*> textureSetOrigins;
    for (const auto& item : json.value("texturesets", nlohmann::json::array())) {
        auto* const created = dataHandler->LookupForm<RE::BGSTextureSet>(item.value("formid", 0U), outputPlugin);
        auto* const stock = lookupTextureSet(*dataHandler, item, "stockplugin", "stockformid");
        if (created && stock)
            textureSetOrigins.emplace(created, stock);
    }

    Result result;
    for (const auto& texture : json.value("generatedtextures", nlohmann::json::array()))
        if (texture.is_string())
            result.generatedTextures.insert(StringUtil::utf8toUTF16(texture.get<std::string>()));

    // The records the output changed go back to stock while they hold what the output wrote. A value another plugin
    // set since stays, as every record edit of PGLite leaves another plugin's value alone.
    for (const auto& record : records) {
        auto* const form = dataHandler->LookupForm(record.formID, record.plugin);
        const auto element = form ? elementOf(*form, record.subModel) : std::nullopt;
        if (!element) {
            Logger::debug("The offline output's record {} / {:06X} / {} is not in the loaded game",
                          record.plugin,
                          record.formID,
                          record.subModel);
            result.unresolved++;
            continue;
        }

        const auto current = currentState(*element);
        if (isSameState(current, record.stock)) {
            // A later override carries the original already.
            continue;
        }

        if (!isSameState(current, record.patched)) {
            Logger::debug("The offline output's record {} / {:06X} / {} keeps the value another plugin set",
                          record.plugin,
                          record.formID,
                          record.subModel);
            result.kept++;
            continue;
        }

        if (record.stock.hasUnresolvedTextureSet) {
            Logger::warn("The offline output's record {} / {:06X} / {} keeps its patched alternate textures, the "
                         "loaded game lacks a stock texture set",
                         record.plugin,
                         record.formID,
                         record.subModel);
            result.unresolved++;
            continue;
        }

        applyState(*element, record.stock);
        result.restored++;
    }

    // Every other record model that carries the output's duplicate paths or texture sets is a copy made from its
    // records (DynDOLOD's, a patch's): the copy of a record of the file gets that record's stock values, any other
    // the original path and the stock texture sets.
    for (const auto formType : Uses::modelFormTypes()) {
        for (auto* const form : dataHandler->GetFormArray(formType)) {
            if (!form)
                continue;

            const auto plugin = pluginOf(*form);
            for (const auto& element : Uses::modelElements(*form)) {
                if (byKey.contains(keyOf(plugin, form->GetLocalFormID(), element.subModel)))
                    continue;

                auto current = currentState(element);
                const auto carriedPath = current.model.wstring();
                const bool hasDuplicatePath = carriedPath.starts_with(duplicateFolder);
                const bool hasOutputTextureSet
                    = std::ranges::any_of(current.entries, [&textureSetOrigins](const Entry& entry) {
                          return textureSetOrigins.contains(entry.textureSet) || isOutputTextureSet(entry.textureSet);
                      });
                if (!hasDuplicatePath && !hasOutputTextureSet)
                    continue;

                const auto copied = bySignature.find(signatureOf(current));
                if (copied != bySignature.end() && !copied->second->stock.hasUnresolvedTextureSet) {
                    applyState(element, copied->second->stock);
                } else {
                    if (hasDuplicatePath)
                        current.model = originalPathOf(current.model);
                    for (auto& entry : current.entries) {
                        const auto origin = textureSetOrigins.find(entry.textureSet);
                        if (origin != textureSetOrigins.end())
                            entry.textureSet = origin->second;
                    }
                    applyState(element, current);
                }

                Logger::debug(L"The record {} / {:06X} / {} carried the offline output's values ({}) and was mapped "
                              L"to stock ({})",
                              StringUtil::windows1252toUTF16(plugin),
                              form->GetLocalFormID(),
                              StringUtil::utf8toUTF16(element.subModel),
                              carriedPath,
                              Uses::normalizedModelPath(element.model->GetModel()).wstring());
                result.mapped++;
            }
        }
    }

    return result;
}

}
