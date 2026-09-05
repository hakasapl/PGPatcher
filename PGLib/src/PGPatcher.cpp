#include "PGPatcher.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "handlers/HandlerLightPlacerTracker.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherMesh.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"
#include "util/TaskTracker.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "util/TaskQueue.hpp"
#include <DirectXTex.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/thread.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <d3d11.h>
#include <exception>
#include <filesystem>
#include <fmt/xchar.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <winerror.h>
#include <winnt.h>

using namespace std;
using namespace StringUtil;
using namespace nifly;

// statics
PatcherUtil::PatcherMeshSet PGPatcher::s_meshPatchers;
PatcherUtil::PatcherTextureSet PGPatcher::s_texPatchers;

std::shared_mutex PGPatcher::s_diffJSONMutex;
nlohmann::json PGPatcher::s_diffJSON;

void PGPatcher::loadPatchers(const PatcherUtil::PatcherMeshSet& meshPatchers,
                             const PatcherUtil::PatcherTextureSet& texPatchers)
{
    s_meshPatchers = meshPatchers;
    s_texPatchers = texPatchers;
}

void PGPatcher::patchMeshes(const bool& multiThread,
                            const bool& forceBasePatch,
                            const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                            const bool& checkAllowedRecTypes,
                            const bool& excludeFacegens,
                            const std::function<void(size_t,
                                                     size_t)>& progressCallback)
{
    auto* const pgd = PGGlobals::getPGD();
    pgd->waitForMeshMapping();
    pgd->waitForCMClassification();

    // Init Handlers
    HandlerLightPlacerTracker::init(pgd->getLightPlacerJSONs());
    PatcherTextureHookConvertToCM::reset();
    PatcherTextureHookFixSSS::reset();

    //
    // MESH PATCHING
    //

    // Create task queue for setting model uses
    TaskQueue setModelUsesQueue;

    // Add tasks
    auto meshes = pgd->getMeshes();

    // Incremental runs: find the meshes whose previous output is still valid and remove outputs that are not
    std::unordered_set<std::filesystem::path> skippable;
    if (PGRunCache::hasPreviousRun()) {
        skippable = PGRunCache::evaluateMeshes(meshes, multiThread, progressCallback);
        PGRunCache::pruneStaleOutputs(skippable);
    }

    // Create task tracker
    TaskTracker taskTracker("Mesh Patcher", meshes.size());

    // Create runner
    TaskPoolRunner meshRunner(multiThread);
    if (progressCallback) {
        taskTracker.setCallbackFunc(progressCallback);
    }

    // Model uses of replayed meshes are applied in one batch: thousands of individual plugin calls would otherwise
    // dominate the runtime of an incremental run
    std::vector<PGMeshPermutationTracker::MeshResult> replayedMeshResults;
    std::mutex replayedMeshResultsMutex;

    for (auto& [mesh, nifCache] : meshes) {
        if (skippable.contains(mesh)) {
            meshRunner.addTask([&taskTracker, &mesh, &replayedMeshResults, &replayedMeshResultsMutex] {
                taskTracker.completeJob(replayNIF(mesh, replayedMeshResults, replayedMeshResultsMutex));
            });
            continue;
        }

        meshRunner.addTask([&taskTracker,
                            &mesh,
                            &setModelUsesQueue,
                            &forceBasePatch,
                            &allowedModelRecTypes,
                            &checkAllowedRecTypes,
                            &excludeFacegens] {
            taskTracker.completeJob(patchNIF(
                mesh, setModelUsesQueue, forceBasePatch, allowedModelRecTypes, checkAllowedRecTypes, excludeFacegens));
        });
    }

    // Blocks until all tasks are done
    meshRunner.runTasks();

    // Apply the plugin model uses of all replayed meshes at once
    if (!replayedMeshResults.empty()) {
        setModelUsesQueue.queueTask(
            [results = std::move(replayedMeshResults)]() -> void { PGPlugin::setModelUses(results); });
    }

    // final validation for weight variants
    const auto weightVariantErrors = PGMeshPermutationTracker::validateWeightedVariants();
    for (const auto& [meshPath, message] : weightVariantErrors) {
        // these errors belong to the mesh so they are replayed when the mesh is skipped in a later run
        PGRunCache::appendDeferredMessage(meshPath, spdlog::level::err, message);
    }

    // Finalize handlers
    HandlerLightPlacerTracker::finalize();

    // Wait for model uses to complete
    // TODO this wait does not need to wait here, just needs to end before finalize
    if (setModelUsesQueue.isWorking()) {
        Logger::info("Waiting for setting plugin model uses to complete...");
        setModelUsesQueue.waitForCompletion();
    }
}

void PGPatcher::patchTextures(const bool& multiThread,
                              const std::function<void(size_t,
                                                       size_t)>& progressCallback)
{
    auto* const pgd = PGGlobals::getPGD();
    pgd->waitForMeshMapping();
    pgd->waitForCMClassification();

    // Init Handlers
    HandlerLightPlacerTracker::init(pgd->getLightPlacerJSONs());

    // Incremental runs: replay reused generated textures and delete generated textures nobody needs anymore
    PGRunCache::finalizeHooks();

    //
    // TEXTURE PATCHING
    //

    // texture runner
    auto textures = pgd->getTextures();

    // Create task tracker
    TaskTracker textureTaskTracker("Texture Patcher", textures.size());

    // Create runner
    TaskPoolRunner textureRunner(multiThread);
    if (progressCallback) {
        textureTaskTracker.setCallbackFunc(progressCallback);
    }

    // Add tasks
    for (const auto& texture : textures) {
        textureRunner.addTask([&textureTaskTracker, &texture] { textureTaskTracker.completeJob(patchDDS(texture)); });
    }

    // Blocks until all tasks are done
    textureRunner.runTasks();
}

auto PGPatcher::getPatchMeta() -> std::map<std::filesystem::path,
                                           MeshMeta>
{
    const shared_lock lock(s_meshPatchInfoMutex);
    return s_meshPatchInfo;
}

void PGPatcher::sortMatches(std::vector<PatcherUtil::ShaderPatcherMatch>& matches)
{
    std::ranges::sort(
        matches, [&](const PatcherUtil::ShaderPatcherMatch& a, const PatcherUtil::ShaderPatcherMatch& b) -> bool {
            if (a.mod != nullptr && b.mod == nullptr) {
                return true;
            }

            if (a.mod == nullptr && b.mod != nullptr) {
                return false;
            }

            if (a.mod != nullptr && b.mod != nullptr) {
                if (a.mod->priority != b.mod->priority) {
                    return a.mod->priority > b.mod->priority;
                }
            }

            const auto aShader = a.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN ? a.shaderTransformTo : a.shader;
            const auto bShader = b.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN ? b.shaderTransformTo : b.shader;
            if (aShader != bShader) {
                return static_cast<int>(aShader) > static_cast<int>(bShader);
            }

            return a.match.matchedPath < b.match.matchedPath;
        });
}

void PGPatcher::sortMatches(std::vector<MatchMeta>& matches,
                            const std::vector<std::shared_ptr<PGModManager::Mod>>& modPriorityList)
{
    // Build a map of mod pointers to their priority rank (index in modPriorityList)
    std::unordered_map<const PGModManager::Mod*, size_t> modPriorityOrder;
    modPriorityOrder.reserve(modPriorityList.size());
    size_t priorityRank = 0;
    for (const auto& mod : modPriorityList) {
        modPriorityOrder.emplace(mod.get(), priorityRank++);
    }

    const size_t fallbackRank = modPriorityList.size();

    std::ranges::sort(matches, [&](const MatchMeta& a, const MatchMeta& b) -> bool {
        // Get priority ranks for each match's mod
        size_t aRank = fallbackRank;
        if (a.mod != nullptr) {
            const auto aIt = modPriorityOrder.find(a.mod.get());
            if (aIt != modPriorityOrder.end()) {
                aRank = aIt->second;
            }
        }

        size_t bRank = fallbackRank;
        if (b.mod != nullptr) {
            const auto bIt = modPriorityOrder.find(b.mod.get());
            if (bIt != modPriorityOrder.end()) {
                bRank = bIt->second;
            }
        }

        // First sort by mod priority (lower rank = higher priority)
        if (aRank != bRank) {
            return aRank < bRank;
        }

        // Then sort by effective shader type (higher enum value wins; prefer transform target if set)
        const auto aShader = a.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN ? a.shaderTransformTo : a.shader;
        const auto bShader = b.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN ? b.shaderTransformTo : b.shader;
        if (aShader != bShader) {
            return static_cast<int>(aShader) > static_cast<int>(bShader);
        }

        // Finally sort by matched path filename A-Z
        return a.matchedPath.native() < b.matchedPath.native();
    });
}

auto PGPatcher::hasConflictData() -> bool
{
    const shared_lock lock(s_meshPatchInfoMutex);
    return !s_meshPatchInfo.empty();
}

void PGPatcher::resetRunState()
{
    {
        const unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo.clear();
    }

    {
        const unique_lock lock(s_diffJSONMutex);
        s_diffJSON.clear();
    }
}

void PGPatcher::deleteOutputDir(const bool& preOutput,
                                const bool& keepIncrementalOutput)
{
    static const unordered_set<filesystem::path> foldersToDelete
        = {"meshes", "textures", "pbrnifpatcher", "lightplacer", "pbrtexturesets"};
    static const filesystem::path updateCacheFile = boost::to_lower_copy(PGRunCache::CACHE_FILENAME.wstring());
    static const filesystem::path updateCacheTempFile = updateCacheFile.wstring() + L".tmp";
    static const unordered_set<filesystem::path> filesToDelete
        = {"pgpatcher.esp", "parallaxgen_diff.json", updateCacheFile, updateCacheTempFile};
    static const vector<pair<wstring, wstring>> filesToDeleteParseRules = {{L"pg_", L".esp"}};
    static const unordered_set<filesystem::path> filesToIgnore = {"meta.ini"};
    static const unordered_set<filesystem::path> filesToDeletePreOutput = {"pgpatcher_output.zip"};

    // Kept when updating a previous output incrementally
    static const unordered_set<filesystem::path> foldersToKeepIncremental = {"meshes", "textures"};
    static const unordered_set<filesystem::path> filesToKeepIncremental = {updateCacheFile};

    const auto outputDir = PGGlobals::getPGD()->getGeneratedPath();
    if (!filesystem::exists(outputDir) || !filesystem::is_directory(outputDir)) {
        return;
    }

    vector<filesystem::path> filesToDeleteParsed;
    for (const auto& entry : filesystem::directory_iterator(outputDir)) {
        const filesystem::path entryFilename = boost::to_lower_copy(entry.path().filename().wstring());

        bool outerContinue = false;
        for (const auto& [filesToDeleteStartsWith, filesToDeleteEndsWith] : filesToDeleteParseRules) {
            if (boost::starts_with(entryFilename.wstring(), filesToDeleteStartsWith)
                && boost::ends_with(entryFilename.wstring(), filesToDeleteEndsWith)) {
                filesToDeleteParsed.push_back(entry.path().filename());
                outerContinue = true;
                break;
            }
        }

        if (outerContinue) {
            continue;
        }

        if (entry.is_regular_file()
            && (filesToDelete.contains(entryFilename) || filesToIgnore.contains(entryFilename)
                || filesToDeletePreOutput.contains(entryFilename))) {
            continue;
        }

        if (entry.is_directory() && (foldersToDelete.contains(entryFilename))) {
            continue;
        }

        Logger::critical("Output directory has non-PGPatcher related files. The output directory should only contain "
                         "files generated by PGPatcher or empty. Exiting.");
        return;
    }

    if (keepIncrementalOutput) {
        Logger::info("Deleting old plugin and metadata files from output directory (meshes and textures are kept "
                     "for updating)...");
    } else {
        Logger::info("Deleting old output files from output directory...");
    }

    // Delete old output
    try {
        filesToDeleteParsed.insert(filesToDeleteParsed.end(), filesToDelete.begin(), filesToDelete.end());
        for (const auto& fileToDelete : filesToDeleteParsed) {
            if (keepIncrementalOutput && filesToKeepIncremental.contains(fileToDelete)) {
                continue;
            }

            const auto file = outputDir / fileToDelete;
            if (filesystem::exists(file)) {
                filesystem::remove(file);
            }
        }

        for (const auto& folderToDelete : foldersToDelete) {
            if (keepIncrementalOutput && foldersToKeepIncremental.contains(folderToDelete)) {
                continue;
            }

            const auto folder = outputDir / folderToDelete;
            if (filesystem::exists(folder)) {
                filesystem::remove_all(folder);
            }
        }

        if (preOutput) {
            for (const auto& fileToDelete : filesToDeletePreOutput) {
                const auto file = outputDir / fileToDelete;
                if (filesystem::exists(file)) {
                    filesystem::remove(file);
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::critical("Failed to delete old output files: {}", e.what());
        return;
    }
}

auto PGPatcher::isOutputEmpty() -> bool
{
    static const unordered_set<filesystem::path> filesToIgnore
        = {"meta.ini", boost::to_lower_copy(PGRunCache::CACHE_FILENAME.wstring())};

    // recursive output dir
    const auto outputDir = PGGlobals::getPGD()->getGeneratedPath();
    if (!filesystem::exists(outputDir) || !filesystem::is_directory(outputDir)) {
        return true;
    }

    // check if output dir is empty
    for (const auto& entry : // NOLINT(readability-use-anyofallof)
         filesystem::recursive_directory_iterator(outputDir)) {
        const filesystem::path entryFilename = boost::to_lower_copy(entry.path().filename().wstring());
        if (entry.is_regular_file() && !filesToIgnore.contains(entryFilename)) {
            return false;
        }
    }

    return true;
}

auto PGPatcher::replayNIF(const std::filesystem::path& nifPath,
                          std::vector<PGMeshPermutationTracker::MeshResult>& replayedMeshResults,
                          std::mutex& replayedMeshResultsMutex) -> TaskTracker::Result
{
    const Logger::Prefix nifPrefix(nifPath.wstring());

    const auto* record = PGRunCache::getPreviousRecord(nifPath);
    if (record == nullptr) {
        throw runtime_error("Update cache record not found for mesh: " + nifPath.string());
    }

    Logger::trace("Mesh is unchanged since the previous run, reusing previous output");

    // Warnings and errors that patching this mesh produced
    PGRunCache::replayMessages(*record);

    // Previous output files are still in the output directory
    auto* const pgd = PGGlobals::getPGD();
    for (const auto& [output, size] : record->outputFiles) {
        pgd->addGeneratedFile(output);
    }

    // Generated textures this mesh depends on
    for (const auto& [kind, texPath] : record->hookRegistrations) {
        PGRunCache::replayHookRegistration(kind, texPath);
    }

    // Plugin model uses (applied in one batch by the caller)
    {
        const lock_guard<mutex> lock(replayedMeshResultsMutex);
        replayedMeshResults.insert(replayedMeshResults.end(), record->meshResults.begin(), record->meshResults.end());
    }

    // Light placer handler
    for (const auto& meshResult : record->meshResults) {
        HandlerLightPlacerTracker::handleNIFCreated(nifPath, meshResult.meshPath);
    }

    // Diff JSON
    if (record->hasDiff) {
        const auto diffJSONKey = utf16toUTF8(nifPath.wstring());
        const unique_lock lock(s_diffJSONMutex);
        s_diffJSON[diffJSONKey]["crc32original"] = record->crc32Original;
        s_diffJSON[diffJSONKey]["crc32patched"] = record->crc32Patched;
    }

    // Conflict viewer metadata
    {
        auto meshMeta = PGRunCache::buildMeshMeta(record->meta);
        const unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo[nifPath] = std::move(meshMeta);
    }

    // The record stays valid for the next run
    PGRunCache::carryOverRecord(nifPath);

    return TaskTracker::Result::SUCCESS;
}

auto PGPatcher::getDiffJSON() -> nlohmann::json
{
    const shared_lock lock(s_diffJSONMutex);
    return s_diffJSON;
}

auto PGPatcher::patchNIF(const std::filesystem::path& nifPath,
                         TaskQueue& setModelUsesQueue,
                         const bool& forceBasePatch,
                         const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                         const bool& checkAllowedRecTypes,
                         const bool& excludeFacegens) -> TaskTracker::Result
{
    const Logger::Prefix nifPrefix(nifPath.wstring());

    // Record everything this mesh depends on so a later run can skip it if nothing changed
    std::unique_ptr<PGRunCache::MeshRecorder> recorder;
    if (PGRunCache::isEnabled()) {
        recorder = std::make_unique<PGRunCache::MeshRecorder>(nifPath);
    }

    // Get mod of nif
    if (PGGlobals::isPGMMSet()) {
        const auto mod = PGGlobals::getPGMM()->getModByFileSmart(nifPath);
        if (mod != nullptr) {
            const std::shared_lock<std::shared_mutex> modLock(mod->mutex);
            PGRunCache::recordModState(mod->name, mod->isEnabled, mod->areMeshesIgnored);
            if (mod != nullptr && mod->areMeshesIgnored) {
                Logger::trace(L"Skipping NIF patching for mod with ignored meshes: {}", mod->name);
                return TaskTracker::Result::SUCCESS;
            }
        }
    }

    // Create mesh tracker for this NIF
    auto meshTracker = PGMeshPermutationTracker(nifPath);

    // check if we have the nif in cache
    auto* const pgd = PGGlobals::getPGD();
    const auto& meshes = pgd->getMeshes();
    if (!meshes.contains(nifPath)) {
        throw runtime_error("NIF not found in cache: " + nifPath.string());
    }

    auto nifCache = meshes.at(nifPath);
    const auto originalMeshUses = nifCache.meshUses;
    const bool isFacegen = PGNIFUtil::isFacegenMesh(nifPath);
    if (isFacegen && excludeFacegens) {
        Logger::trace(L"Skipping NIF patching for facegen mesh (facegens excluded): {}", nifPath.wstring());
        return TaskTracker::Result::SUCCESS;
    }

    if (nifCache.meshUses.empty() && !forceBasePatch && !isFacegen) {
        Logger::trace(L"Skipping NIF patching for mesh with no plugin uses: {}", nifPath.wstring());
        return TaskTracker::Result::SUCCESS;
    }

    meshTracker.load();

    // Prepare meta
    MeshMeta meshMeta;

    if (nifCache.meshUses.empty() && (forceBasePatch || isFacegen)) {
        // add a dummy mesh use to trigger base patching (pgtools uses this since no plugins)
        // always trigger dummy for facegen meshes since they never appear in plugins
        Logger::debug(L"Forcing non-plugin patching context for mesh: {}", nifPath.wstring());
        const PGMeshPermutationTracker::FormKey dummyFormKey = {.modKey = L"", .formID = 0, .subMODL = ""};
        const PGPlugin::MeshUseAttributes dummyUse = {.isWeighted = false,
                                                      .singlepassMATO = false,
                                                      .isFacegen = isFacegen,
                                                      .isIgnored = false,
                                                      .isDummyUse = true,
                                                      .recType = PGPlugin::ModelRecordType::UNKNOWN,
                                                      .alternateTextures = {}};
        nifCache.meshUses.insert(nifCache.meshUses.begin(), make_pair(dummyFormKey, dummyUse));
    } else if (isFacegen) {
        // if this is true then there are mesh uses but this is a facegen mesh so we should throw a warning
        Logger::warn(L"NIF has mesh uses but is detected as a facegen mesh: {}", nifPath.wstring());
        return TaskTracker::Result::FAILURE;
    }

    // loop through each use
    for (auto use : nifCache.meshUses) {
        // process mesh patch for each and every occurance of the mesh in plugins
        if (use.second.isIgnored) {
            // This record is ignored, trigger tracker to ignore the base mesh and skip this patch
            meshTracker.ignoreBaseMesh();
            continue;
        }

        if (checkAllowedRecTypes && !use.second.isDummyUse && !allowedModelRecTypes.contains(use.second.recType)) {
            // This record is not in the allowed record types, trigger tracker to ignore the base mesh and skip this
            // patch
            meshTracker.ignoreBaseMesh();
            continue;
        }

        const auto formKey = use.first;

        const Logger::Prefix dupPrefix(
            fmt::format(L"{}:{:06X}:{}", formKey.modKey, formKey.formID, StringUtil::utf8toUTF16(formKey.subMODL)));

        // alternate textures do exist so we need to do some processing
        // stage a new mesh
        auto* stagedNIF = meshTracker.stageMesh();
        unordered_set<unsigned int> enforceCheckBlocks;
        if (!processNIF(nifPath,
                        stagedNIF,
                        meshMeta,
                        use.second.singlepassMATO,
                        formKey,
                        use.second.recType,
                        use.second.alternateTextures,
                        enforceCheckBlocks)) {
            return TaskTracker::Result::FAILURE;
        }
        if (meshTracker.commitMesh(formKey, use.second.isWeighted, use.second.alternateTextures, enforceCheckBlocks)) {
            Logger::trace("Mesh committed");
        } else {
            Logger::trace("Mesh not committed (already exists or no changes)");
        }
    }

    // Save meshes
    const auto saveResults = meshTracker.saveMeshes();
    setModelUsesQueue.queueTask([saveResults]() -> void { PGPlugin::setModelUses(saveResults.first); });

    // run handlers
    for (const auto& meshResult : saveResults.first) {
        Logger::Prefix(L"Handler: " + meshResult.meshPath.wstring());
        HandlerLightPlacerTracker::handleNIFCreated(nifPath, meshResult.meshPath);
    }
    // Add to diff JSON
    const auto diffJSONKey = utf16toUTF8(nifPath.wstring());
    if (saveResults.second.second != 0) {
        // only add to diff if the base mesh actually saved, which is indicated by a non-zero patched crc32
        Logger::trace(
            "Base mesh was updated, saving diff CRC32: {} -> {}", saveResults.second.first, saveResults.second.second);

        const unique_lock lock(s_diffJSONMutex);
        s_diffJSON[diffJSONKey]["crc32original"] = saveResults.second.first;
        s_diffJSON[diffJSONKey]["crc32patched"] = saveResults.second.second;
    }

    // Save mesh meta
    {
        const unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo[nifPath] = meshMeta;
    }

    // Store the record for incremental runs
    if (recorder != nullptr) {
        recorder->setUses(originalMeshUses);
        recorder->setMeshResults(saveResults.first);
        if (saveResults.second.second != 0) {
            recorder->setDiff(saveResults.second.first, saveResults.second.second);
        }
        recorder->setMeta(meshMeta);
        recorder->commit();
    }

    return TaskTracker::Result::SUCCESS;
}

auto PGPatcher::processNIF(const std::filesystem::path& nifPath,
                           nifly::NifFile* nif,
                           MeshMeta& meshMeta,
                           bool singlepassMATO,
                           const PGMeshPermutationTracker::FormKey& formKey,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           std::unordered_map<unsigned int,
                                              PGTypes::TextureSet>& alternateTextures,
                           std::unordered_set<unsigned int>& nonAltTexShapes) -> bool
{
    // Create patcher objects
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nif);

    // Get shapes and index 3ds (this is in the order as they would show up as 3d indices in plugins)
    const auto shapes = PGNIFUtil::getShapesWith3DIdx(nif);

    meshMeta.formKeys.push_back(formKey);

    size_t shapeMetaIdx = 0;
    for (const auto& [nifShape, oldIndex3D] : shapes) {
        const auto shapeBlockID = nif->GetBlockID(nifShape);
        const auto shapeName = nifShape->name.get();
        const Logger::Prefix shapePrefix(to_string(shapeBlockID) + "/" + shapeName + "/" + to_string(oldIndex3D));

        if (nifShape == nullptr) {
            // Skip if shape is null (invalid shapes)
            Logger::trace(L"Skipping: Shape is null");
            continue;
        }

        if (!PGNIFUtil::isPatchableShape(*nif, *nifShape)) {
            // Skip if not patchable shape
            Logger::trace(L"Skipping: Shape is not patchable");
            continue;
        }

        auto& curMeshShapeMeta = meshMeta.shapeMeta[shapeMetaIdx++];
        curMeshShapeMeta.blockID = shapeBlockID;
        curMeshShapeMeta.shapeName = shapeName;

        PGTypes::TextureSet* ptrAltTex = nullptr;
        if (alternateTextures.contains(oldIndex3D)) {
            ptrAltTex = &alternateTextures.at(oldIndex3D);
        } else {
            // we want to include any texture sets that do not have alternate textures defined to be compared
            nonAltTexShapes.insert(oldIndex3D);
        }
        if (!processNIFShape(nifPath,
                             nif,
                             nifShape,
                             curMeshShapeMeta,
                             patcherObjects,
                             singlepassMATO,
                             formKey,
                             modelRecordType,
                             ptrAltTex)) {
            return false;
        }
    }

    // Run global patchers
    for (const auto& globalPatcher : patcherObjects.globalPatchers) {
        const Logger::Prefix prefixPatches(utf8toUTF16(globalPatcher->getPatcherName()));
        if (globalPatcher->applyPatch()) {
            meshMeta.globalPatchersApplied.push_back(globalPatcher->getPatcherName());
        }
    }

    // Clear texture sets cache for this NIF
    PatcherMeshShader::clearTextureSets(nifPath);

    return true;
}

auto PGPatcher::processNIFShape(const std::filesystem::path& nifPath,
                                nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                MeshShapeMeta& meshShapeMeta,
                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                bool singlepassMATO,
                                const PGMeshPermutationTracker::FormKey& formKey,
                                const PGPlugin::ModelRecordType& modelRecordType,
                                PGTypes::TextureSet* alternateTexture) -> bool
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    if (nifShape == nullptr) {
        throw runtime_error("NIFShape is null");
    }

    // Prep
    PGTypes::TextureSet slots;
    if (alternateTexture == nullptr) {
        slots = PatcherMesh::getTextureSet(nifPath, *nif, *nifShape);
    } else {
        Logger::trace("Alternate texture exist for this shape");
        slots = *alternateTexture;
    }

    // log slots
    Logger::trace("Texture Slots: {}", PGTypes::getStrFromTextureSlots(slots));

    // apply prepatchers
    for (const auto& prePatcher : patchers.prePatchers) {
        const Logger::Prefix prefixPatches(prePatcher->getPatcherName());
        if (prePatcher->applyPatch(slots, *nifShape)) {
            meshShapeMeta.prePatchersApplied.push_back(prePatcher->getPatcherName());

            if (nif->GetBlockID(nifShape) == NIF_NPOS) {
                // shape was deleted, nothing else to do
                return true;
            }
        }
    }

    if (PGNIFUtil::isShaderPatchableShape(*nif, *nifShape)) {
        // Allowed shaders from result of patchers
        const auto matches = getMatches(slots, patchers, singlepassMATO, modelRecordType, &patchers, nifShape);
        std::vector<PatcherUtil::ShaderPatcherMatch> enabledMatches;

        // Add matches to mesh shape meta
        for (const auto& match : matches) {
            MatchMeta matchMeta;
            matchMeta.mod = match.mod;
            matchMeta.shader = match.shader;
            matchMeta.shaderTransformTo = match.shaderTransformTo;
            matchMeta.matchedPath = match.match.matchedPath;

            if (PGGlobals::isPGMMSet()) {
                // Record which mod supplies each result texture so the conflict viewer can flag
                // matches whose result textures come from different mods. Transformed matches get
                // their slots from the transform's target shader (running the actual transform here
                // would schedule texture generation); the source match path is kept for attribution
                // since transform-generated files belong to no mod anyway.
                auto resultShader = match.shader;
                if (match.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN
                    && patchers.shaderPatchers.contains(match.shaderTransformTo)) {
                    resultShader = match.shaderTransformTo;
                }

                PGTypes::TextureSet resultSlots = slots;
                patchers.shaderPatchers.at(resultShader)->applyPatchSlots(resultSlots, match.match);
                for (size_t slot = 0; slot < resultSlots.size(); slot++) {
                    if (resultSlots.at(slot).empty()) {
                        continue;
                    }

                    auto slotMod = PGGlobals::getPGMM()->getModByFileSmart(resultSlots.at(slot));
                    if (slotMod != nullptr) {
                        matchMeta.resultTextureMods.emplace_back(static_cast<PGEnums::TextureSlots>(slot),
                                                                 std::move(slotMod));
                    }
                }
            }

            meshShapeMeta.matches[formKey].push_back(matchMeta);

            if (match.mod != nullptr) {
                const std::shared_lock lk(match.mod->mutex);
                if (!match.mod->isEnabled) {
                    continue;
                }
            }

            enabledMatches.push_back(match);
        }

        PatcherUtil::ShaderPatcherMatch winningShaderMatch;
        // Get winning match
        if (!enabledMatches.empty()) {
            winningShaderMatch = enabledMatches.at(0);

            // Apply transforms
            if (applyTransformIfNeeded(winningShaderMatch, patchers)) {
                Logger::trace("Shader transform was applied");
            }

            Logger::trace(L"Winning Match: {} / {} / {}",
                          utf8toUTF16(PGEnums::getStrFromShader(winningShaderMatch.shader)),
                          winningShaderMatch.mod == nullptr ? L"" : winningShaderMatch.mod->name,
                          winningShaderMatch.match.matchedPath);

            // loop through patchers
            patchers.shaderPatchers.at(winningShaderMatch.shader)
                ->applyPatch(slots, *nifShape, winningShaderMatch.match);

            if (nif->GetBlockID(nifShape) == NIF_NPOS) {
                // shape was deleted, nothing else to do
                return true;
            }
        }
    }

    // apply postpatchers
    for (const auto& postPatcher : patchers.postPatchers) {
        const Logger::Prefix prefixPatches(postPatcher->getPatcherName());
        if (postPatcher->applyPatch(slots, *nifShape)) {
            meshShapeMeta.postPatchersApplied.push_back(postPatcher->getPatcherName());

            if (nif->GetBlockID(nifShape) == NIF_NPOS) {
                // shape was deleted, nothing else to do
                return true;
            }
        }
    }

    if (alternateTexture == nullptr) {
        // assign texture set to nif
        PatcherMesh::setTextureSet(nifPath, *nif, *nifShape, slots);
    } else {
        // assign new slots to propogate upstream for alternate textures
        *alternateTexture = slots;
    }

    Logger::trace("Texture Slots Modified: {}", PGTypes::getStrFromTextureSlots(slots));

    return true;
}

auto PGPatcher::digestMatches(const std::vector<PatcherUtil::ShaderPatcherMatch>& matches,
                              const PatcherUtil::PatcherMeshObjectSet& patchers) -> uint64_t
{
    // Everything about the ordered match list that influences how the winning match is chosen and applied. Mod
    // priorities are deliberately not part of the digest: only their effect (the order of the list) matters, so
    // renumbering priorities when a mod is added does not invalidate meshes whose matches did not change.
    HashUtil::Fnv1a64 hasher;
    hasher.add(static_cast<uint64_t>(matches.size()));
    for (const auto& match : matches) {
        hasher.add(match.shader);
        hasher.add(StringUtil::toLowerASCIIFast(match.match.matchedPath));

        if (match.mod != nullptr) {
            const shared_lock modLock(match.mod->mutex);
            hasher.add(match.mod->name);
            hasher.add(match.mod->isEnabled);
        } else {
            hasher.add(wstring());
            hasher.add(false);
        }

        const auto patcherIt = patchers.shaderPatchers.find(match.shader);
        if (patcherIt != patchers.shaderPatchers.end()) {
            hasher.add(patcherIt->second->getMatchExtraDataHash(match.match));
        }
    }

    return hasher.value();
}

auto PGPatcher::computeMatchesDigest(const std::filesystem::path& nifPath,
                                     const PGTypes::TextureSet& slots,
                                     bool singlepassMATO,
                                     const PGPlugin::ModelRecordType& modelRecordType) -> uint64_t
{
    // Shader patchers only need a NIF for canApply, which is not part of the digest
    PatcherUtil::PatcherMeshObjectSet patchers;
    for (const auto& [shader, factory] : s_meshPatchers.shaderPatchers) {
        patchers.shaderPatchers.emplace(shader, factory(nifPath, nullptr));
    }

    const auto matches = getMatches(slots, patchers, singlepassMATO, modelRecordType);
    return digestMatches(matches, patchers);
}

auto PGPatcher::getMatches(const PGTypes::TextureSet& slots,
                           const PatcherUtil::PatcherMeshObjectSet& patchers,
                           bool singlepassMATO,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           const PatcherUtil::PatcherMeshObjectSet* patcherObjects,
                           nifly::NiShape* shape) -> std::vector<PatcherUtil::ShaderPatcherMatch>
{
    vector<PatcherUtil::ShaderPatcherMatch> matches;
    uint64_t matchesDigest = 0;

    // Every lookup made while building the match list is covered by the digest recorded below, so individual
    // lookups are not recorded as dependencies
    {
        const PGRunCache::SuspendRecording suspendRecording;

        unordered_set<shared_ptr<PGModManager::Mod>, PGModManager::Mod::ModHash> modSet;
        if (patcherObjects != nullptr && patchers.shaderPatchers.size() != patcherObjects->shaderPatchers.size()) {
            throw runtime_error("Patcher objects size mismatch");
        }

        if ((patcherObjects != nullptr && shape == nullptr) || (patcherObjects == nullptr && shape != nullptr)) {
            throw runtime_error("If shape or patcherObjects is set, both must be set");
        }

        for (const auto& [shader, patcher] : patchers.shaderPatchers) {
            // note: name is defined in source code in UTF8-encoded files
            const Logger::Prefix prefixPatches(patcher->getPatcherName());

            // Check if shader should be applied
            vector<PatcherMeshShader::PatcherMatch> curMatches;
            if (!patcher->shouldApply(slots, curMatches)) {
                Logger::trace(L"Rejecting: Shader not applicable");
                continue;
            }

            for (const auto& match : curMatches) {
                if (!PGGlobals::getPGD()->isFile(match.matchedPath)) {
                    Logger::trace(L"Rejecting: Matched path '{}' is not a file", match.matchedPath);
                    continue;
                }

                PatcherUtil::ShaderPatcherMatch curMatch;
                if (PGGlobals::isPGMMSet()) {
                    curMatch.mod = PGGlobals::getPGMM()->getModByFileSmart(match.matchedPath);
                }

                curMatch.shader = shader;
                curMatch.match = match;
                curMatch.shaderTransformTo = PGEnums::ShapeShader::UNKNOWN;

                matches.push_back(curMatch);
                if (curMatch.mod != nullptr) {
                    // add mod to set
                    modSet.insert(curMatch.mod);
                }
            }
        }

        // Populate conflict mods if set
        if (!modSet.empty()) {
            // add mods to conflict set
            for (const auto& match : matches) {
                if (match.mod == nullptr) {
                    continue;
                }

                const unique_lock lock(match.mod->mutex);

                for (const auto& conflictMod : modSet) {
                    if (conflictMod != match.mod) {
                        match.mod->conflicts.insert(conflictMod);
                    }
                }
            }
        }

        // Sort before filtering (filtering preserves relative order) so the digest covers the complete ordered list,
        // which is what incremental runs recompute without a loaded mesh
        sortMatches(matches);
        matchesDigest = digestMatches(matches, patchers);

        // Loop through matches and delete any that cannot apply
        // Verify shape can apply
        if (patcherObjects != nullptr) {
            for (auto it = matches.begin(); it != matches.end();) {
                auto& curMatch = *it;

                // check canApply for this shape
                bool canApplyBaseShader = false;
                {
                    const auto& curPatcher = patcherObjects->shaderPatchers.at(curMatch.shader);
                    canApplyBaseShader = curPatcher->canApply(*shape, singlepassMATO, modelRecordType);
                }
                bool canApplyTransformShader = false;

                // See if transform is possible
                if (patchers.shaderTransformPatchers.contains(curMatch.shader)) {
                    // a transform patcher is avilable, see if it should be applied
                    // get objects
                    const auto& transformPatcherPair = patcherObjects->shaderTransformPatchers.at(curMatch.shader);
                    auto* const transformPatcher = transformPatcherPair.second.get();

                    // check if transform should be applied
                    if (transformPatcher->shouldTransform(curMatch.match, canApplyBaseShader)) {
                        // transform can apply
                        const auto transformToShader = transformPatcherPair.first;
                        {
                            const auto& curPatcher = patcherObjects->shaderPatchers.at(transformToShader);
                            canApplyTransformShader = curPatcher->canApply(*shape, singlepassMATO, modelRecordType);
                        }

                        if (canApplyTransformShader) {
                            curMatch.shaderTransformTo = transformToShader;
                        }
                    }
                }

                if (!canApplyBaseShader) {
                    // base shaders can't do it, lets check transforms
                    if (curMatch.shaderTransformTo == PGEnums::ShapeShader::UNKNOWN) {
                        it = matches.erase(it);
                        continue;
                    }

                    if (!canApplyTransformShader) {
                        it = matches.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
    }

    // record the digest as a dependency of the mesh being patched (no-op unless recording)
    PGRunCache::recordMatches(slots, singlepassMATO, modelRecordType, matchesDigest);

    return matches;
}

auto PGPatcher::applyTransformIfNeeded(PatcherUtil::ShaderPatcherMatch& match,
                                       const PatcherUtil::PatcherMeshObjectSet& patchers) -> bool
{
    // Transform if required
    if (match.shaderTransformTo != PGEnums::ShapeShader::UNKNOWN) {
        // Find transform object
        auto* const transform = patchers.shaderTransformPatchers.at(match.shader).second.get();

        // Transform Shader
        transform->transform(match.match, match.match);

        match.shader = match.shaderTransformTo;
        match.shaderTransformTo = PGEnums::ShapeShader::UNKNOWN;

        return true;
    }

    return false;
}

auto PGPatcher::createNIFPatcherObjects(const std::filesystem::path& nifPath,
                                        nifly::NifFile* nif) -> PatcherUtil::PatcherMeshObjectSet
{
    auto patcherObjects = PatcherUtil::PatcherMeshObjectSet();
    for (const auto& factory : s_meshPatchers.prePatchers) {
        auto patcher = factory(nifPath, nif);
        patcherObjects.prePatchers.emplace_back(std::move(patcher));
    }
    for (const auto& [shader, factory] : s_meshPatchers.shaderPatchers) {
        auto patcher = factory(nifPath, nif);
        patcherObjects.shaderPatchers.emplace(shader, std::move(patcher));
    }
    for (const auto& [shader, factory] : s_meshPatchers.shaderTransformPatchers) {
        auto transform = factory.second(nifPath, nif);
        patcherObjects.shaderTransformPatchers[shader] = {factory.first, std::move(transform)};
    }
    for (const auto& factory : s_meshPatchers.postPatchers) {
        auto patcher = factory(nifPath, nif);
        patcherObjects.postPatchers.emplace_back(std::move(patcher));
    }
    for (const auto& factory : s_meshPatchers.globalPatchers) {
        auto patcher = factory(nifPath, nif);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    return patcherObjects;
}

auto PGPatcher::patchDDS(const filesystem::path& ddsPath) -> TaskTracker::Result
{
    auto result = TaskTracker::Result::SUCCESS;

    auto* const pgd = PGGlobals::getPGD();

    // Check if this texture needs to be processed
    if (s_texPatchers.globalPatchers.empty() && !PatcherTextureHookConvertToCM::isInProcessList(ddsPath)
        && !PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        // No patchers, so we can skip
        return result;
    }

    Logger::debug(L"Cache for DDS {} is invalidated or nonexistent", ddsPath.wstring());

    // Prep
    Logger::trace(L"Starting Processing");

    // only allow DDS files
    const string ddsFileExt = ddsPath.extension().string();
    if (ddsFileExt != ".dds") {
        throw runtime_error("File is not a DDS file");
    }

    DirectX::ScratchImage ddsImage;
    if (!PGGlobals::getPGD3D()->getDDS(ddsPath, ddsImage)) {
        Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
        return TaskTracker::Result::FAILURE;
    }

    // Run any hook patchers (these create other textures)
    if (PatcherTextureHookConvertToCM::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookConvertToCM(ddsPath, &ddsImage);
        if (!patcher.applyPatch()) {
            Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
            return TaskTracker::Result::FAILURE;
        }
    }
    if (PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookFixSSS(ddsPath, &ddsImage);
        if (!patcher.applyPatch()) {
            Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
            return TaskTracker::Result::FAILURE;
        }
    }

    bool ddsModified = false;

    const auto patcherObjects = createDDSPatcherObjects(ddsPath, &ddsImage);

    // global patchers
    for (const auto& patcher : patcherObjects.globalPatchers) {
        patcher->applyPatch(ddsModified);
    }

    if (ddsModified) {
        // save to output
        const filesystem::path outputFile = pgd->getGeneratedPath() / ddsPath;
        filesystem::create_directories(outputFile.parent_path());

        const HRESULT hr = DirectX::SaveToDDSFile(ddsImage.GetImages(),
                                                  ddsImage.GetImageCount(),
                                                  ddsImage.GetMetadata(),
                                                  DirectX::DDS_FLAGS_NONE,
                                                  outputFile.c_str());
        if (FAILED(hr)) {
            Logger::error(L"Unable to save texture {}: {}", outputFile.wstring(), PGD3D::getHRESULTErrorMessage(hr));
            return TaskTracker::Result::FAILURE;
        }

        // Update file map with generated file
        pgd->addGeneratedFile(ddsPath);
    }

    return result;
}

auto PGPatcher::createDDSPatcherObjects(const std::filesystem::path& ddsPath,
                                        DirectX::ScratchImage* dds) -> PatcherUtil::PatcherTextureObjectSet
{
    auto patcherObjects = PatcherUtil::PatcherTextureObjectSet();
    for (const auto& factory : s_texPatchers.globalPatchers) {
        auto patcher = factory(ddsPath, dds);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    return patcherObjects;
}
