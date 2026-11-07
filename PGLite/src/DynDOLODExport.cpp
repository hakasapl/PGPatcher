#include "PCH.h"

#include "DynDOLODExport.hpp"

#include "PGPlugin.hpp"
#include "Plan.hpp"
#include "RecordEdits.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Uses.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include "NifFile.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

constexpr const wchar_t* fileName = L"dyndolod_pglite_patches.json";
constexpr unsigned formatVersion = 1;
/// @brief The sub model of a record's full model; destruction stage models are not drawn as LOD.
constexpr const char* fullModel = "MODL";

/// @brief The load order index in the top byte of a form ID: a plugin's, or the light plugins' index.
constexpr unsigned pluginIndexShift = 24;
constexpr std::uint32_t lightPluginIndex = 0xFE;
/// @brief A light plugin's index in the 12 bits below the top byte of its form IDs, and its local form IDs.
constexpr unsigned lightPluginIndexShift = 12;
constexpr std::uint32_t lightPluginIndexMask = 0xFFF;
constexpr std::uint32_t lightLocalFormIDMask = 0xFFF;
constexpr std::uint32_t localFormIDMask = 0xFFFFFF;

/**
 * @brief A record's key in the file: the plugin of its form ID's master and its form ID within that plugin, as
 * DynDOLOD and the offline program key records. A record a plugin injected into a master's range is the master's,
 * although the injecting plugin defines it. Nothing for a form ID of no loaded plugin.
 */
std::optional<std::pair<std::wstring,
                        unsigned>>
recordKey(const RE::TESForm& form)
{
    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        return std::nullopt;

    const auto formID = form.GetFormID();
    const RE::TESFile* file = nullptr;
    unsigned localFormID = 0;
    if (formID >> pluginIndexShift == lightPluginIndex) {
        file = dataHandler->LookupLoadedLightModByIndex(
            static_cast<std::uint16_t>((formID >> lightPluginIndexShift) & lightPluginIndexMask));
        localFormID = formID & lightLocalFormIDMask;
    } else {
        file = dataHandler->LookupLoadedModByIndex(static_cast<std::uint8_t>(formID >> pluginIndexShift));
        localFormID = formID & localFormIDMask;
    }

    if (!file)
        return std::nullopt;

    return std::pair { StringUtil::windows1252toUTF16(std::string(file->GetFilename())), localFormID };
}

/// @brief Calls a function with every index on as many threads as the machine has. The function must not throw.
template<typename Function>
void forEachInParallel(const std::vector<std::size_t>& indices,
                       const Function& function)
{
    std::atomic<std::size_t> next { 0 };
    const auto work = [&] {
        for (auto i = next.fetch_add(1); i < indices.size(); i = next.fetch_add(1))
            function(indices.at(i));
    };

    // Joined when they go out of scope, also when starting one of them throws.
    std::vector<std::jthread> workers;
    const auto workerCount = std::min<std::size_t>(std::max(1U, std::thread::hardware_concurrency()), indices.size());
    workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; i++)
        workers.emplace_back(work);
}

/// @brief The members of one family whose records the file can list, with the file of the family each describes.
struct Item {
    Family* family = nullptr;
    std::vector<std::pair<std::size_t, std::size_t>> memberPaths;
};

/// @brief A record the file lists: the shapes PBR entries apply to and the deleted shapes of its model by 3D index.
struct RecordEntry {
    std::wstring plugin;
    unsigned formID = 0;
    std::map<unsigned, nlohmann::json> shapes;
};

/**
 * @brief Whether a use is the full model of a record DynDOLOD draws as LOD, and the record still loads the mesh PGLite
 * planned for it: one whose model string another plugin replaced loads another mesh, and PGLite leaves its edits
 * alone (Plan::retargetRecords, Family::editRecords).
 */
bool isListedUse(const RecordUse& use,
                 const RecordSnapshot& snapshot)
{
    static const auto recordTypes = PGPlugin::dynDOLODRecTypeSet();
    return use.element.subModel == fullModel && recordTypes.contains(use.attributes.recType)
        && use.element.model != nullptr && snapshot.isModelOwned(*use.element.model);
}

/**
 * @brief What the file patches, as the offline program's DynDOLOD mode does: the members with a use DynDOLOD reads
 * (isListedUse()), at the files those uses name. A member of the other record types alone, a facegen head and a family
 * without such a use are not patched at all.
 */
std::vector<Item> itemsOf(const Plan& plan,
                          const RecordSnapshot& snapshot)
{
    std::vector<Item> items;
    for (const auto& family : plan.families()) {
        Item item { .family = family.get() };
        for (std::size_t member = 0; member < family->members.size(); member++) {
            std::vector<std::size_t> pathIndices;
            for (const auto& ref : family->members.at(member).uses) {
                if (isListedUse(*ref.use, snapshot)
                    && std::ranges::find(pathIndices, ref.pathIndex) == pathIndices.end()) {
                    pathIndices.push_back(ref.pathIndex);
                }
            }

            for (const auto pathIndex : pathIndices)
                item.memberPaths.emplace_back(member, pathIndex);
        }

        if (!item.memberPaths.empty())
            items.push_back(std::move(item));
    }

    return items;
}

/**
 * @brief The records of one family the file lists: those of every member whose patch applies a PBR entry to a shape.
 * The texture hooks the patches register are left for the opens that serve the meshes to generate (each open registers
 * them again); the registrations stay with the calling thread, which ends with the write.
 *
 * @return The members that could not be described (logged).
 */
std::size_t describeFamily(const Item& item,
                           const RecordSnapshot& snapshot,
                           std::vector<RecordEntry>& entries)
{
    std::size_t failed = 0;
    for (const auto& [member, pathIndex] : item.memberPaths) {
        try {
            // Only the shapes the patch applies an entry to have a material: a shape that is PBR on disk without one
            // is drawn as the mesh says.
            const PatcherMeshShaderTruePBR::AppliedAttributes applied;
            const auto descriptions = item.family->describe(
                member,
                pathIndex,
                [&applied](const nifly::NifFile& nif, nifly::NiShape& shape, const PGTypes::TextureSet& slots) {
                    const auto* const attributes = applied.of(shape);
                    return attributes ? PatcherMeshShaderTruePBR::material(nif, shape, slots, *attributes)
                                      : nlohmann::json();
                });
            if (!descriptions)
                continue;

            // A shape the patch deleted is described by that alone.
            auto patched = descriptions->patched;
            for (const auto index3D : descriptions->deleted)
                patched[index3D] = nlohmann::json { { "delete", true } };
            if (patched.empty())
                continue;

            for (const auto& ref : item.family->members.at(member).uses) {
                if (ref.pathIndex != pathIndex || !isListedUse(*ref.use, snapshot) || !ref.use->form)
                    continue;

                const auto key = recordKey(*ref.use->form);
                if (!key)
                    continue;

                entries.push_back({
                    .plugin = key->first,
                    .formID = key->second,
                    .shapes = patched,
                });
            }
        } catch (const std::exception& e) {
            Logger::error(L"Unable to describe the PBR materials of {} for DynDOLOD, its records are not listed: {}",
                          item.family->basePaths.at(pathIndex).wstring(),
                          StringUtil::utf8toUTF16(e.what()));
            failed++;
        } catch (...) {
            Logger::error(L"Unable to describe the PBR materials of {} for DynDOLOD, its records are not listed",
                          item.family->basePaths.at(pathIndex).wstring());
            failed++;
        }
    }

    return failed;
}

}

std::filesystem::path DynDOLODExport::file() { return Settings::configFolder() / fileName; }

DynDOLODExport::Result DynDOLODExport::write(const Plan& plan,
                                             const RecordSnapshot& snapshot,
                                             TextureSetFactory& textureSets,
                                             const StageCallback& reportStage,
                                             const ProgressCallback& reportProgress)
{
    Result result;
    const auto started = std::chrono::steady_clock::now();
    try {
        reportStage("pglite.stages.describingMeshes");
        const auto items = itemsOf(plan, snapshot);
        std::vector<std::vector<RecordEntry>> entriesPerItem(items.size());
        std::atomic<std::size_t> failed { 0 };
        std::size_t finished = 0;
        std::mutex mutex;

        // The families patch in parallel, as they do for the engine's loader threads; each one fills its own slot of
        // the results, so the file comes out in the same order whatever the threads did. A family no open patched yet
        // gets its first patch first, which decides what the game keeps of it for the session. One that could not
        // read an input is attempted again after the retry interval, as the next open would, until it is settled.
        std::vector<std::size_t> pending(items.size());
        std::ranges::iota(pending, 0);
        for (unsigned attempt = 0; !pending.empty(); attempt++) {
            if (attempt)
                std::this_thread::sleep_for(readFailureRetryInterval);

            const bool isLastAttempt = attempt >= Family::maxPatchReadFailures;
            std::vector<std::size_t> unsettled;
            forEachInParallel(pending, [&](std::size_t i) {
                const auto& item = items.at(i);
                if (!item.family->settle(textureSets, plan.generation()) && !isLastAttempt) {
                    const std::scoped_lock lock(mutex);
                    unsettled.push_back(i);
                    return;
                }

                failed += describeFamily(item, snapshot, entriesPerItem.at(i));
                const std::scoped_lock lock(mutex);
                reportProgress(++finished, items.size());
            });
            pending = std::move(unsettled);
        }
        result.failed = failed;

        // The records by plugin and form ID, each distinct material once, numbered in the order the records name them.
        reportStage("pglite.stages.writingFile");
        std::map<std::wstring, std::map<unsigned, const RecordEntry*>> records;
        for (const auto& entries : entriesPerItem)
            for (const auto& entry : entries)
                records[entry.plugin][entry.formID] = &entry;

        nlohmann::json materials = nlohmann::json::array();
        std::unordered_map<std::string, std::size_t> materialIndices;
        nlohmann::json recordsJSON = nlohmann::json::object();
        for (const auto& [plugin, formIDs] : records) {
            auto& pluginJSON = recordsJSON[StringUtil::utf16toUTF8(plugin)];
            for (const auto& [formID, entry] : formIDs) {
                auto& recordJSON = pluginJSON[std::format("{:06X}", formID)];
                recordJSON = nlohmann::json::object();
                for (const auto& [index3D, material] : entry->shapes) {
                    const auto key = material.dump();
                    const auto [it, isNew] = materialIndices.try_emplace(key, materialIndices.size());
                    if (isNew)
                        materials.push_back(material);
                    recordJSON[std::to_string(index3D)] = it->second;
                }
                result.records++;
            }
        }
        result.materials = materials.size();

        nlohmann::json output = nlohmann::json::object();
        output["version"] = formatVersion;
        output["materials"] = std::move(materials);
        output["records"] = std::move(recordsJSON);

        // Compact, serialized first and then written whole or not at all, so DynDOLOD never reads half a file.
        const auto text = output.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!Settings::writeFileAtomically(file(), text.data(), text.size())) {
            Logger::error(L"Unable to write the DynDOLOD file {}", file().wstring());
            return result;
        }

        const auto elapsed
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        Logger::info(L"Wrote the DynDOLOD file {} in {} ms: {} records, {} materials, {} bytes ({} mesh families "
                     L"described, {} members failed)",
                     file().wstring(),
                     elapsed.count(),
                     result.records,
                     result.materials,
                     text.size(),
                     items.size(),
                     result.failed);
        result.didSucceed = true;
    } catch (const std::exception& e) {
        Logger::error("The DynDOLOD file could not be written: {}", e.what());
    }

    return result;
}

}
