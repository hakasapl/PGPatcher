#include "PGPatcher.hpp"

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "handlers/HandlerLightPlacerTracker.hpp"
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
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
#include "Shaders.hpp"
#include "util/TaskQueue.hpp"
#include <DirectXTex.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/crc.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

// Statics.
std::shared_mutex PGPatcher::s_diffJSONMutex;
nlohmann::json PGPatcher::s_diffJSON;
void PGPatcher::patchMeshes(const bool& shouldMultithread,
                            const bool& forceBasePatch,
                            const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                            const bool& checkAllowedRecTypes,
                            const bool& excludeFacegens,
                            const std::function<void(size_t,
                                                     size_t)>& progressCallback,
                            const std::function<void(MeshPatchStage)>& stageCallback)
{
    auto* const pgd = PGGlobals::pgd();
    pgd->waitForMeshMapping();
    pgd->waitForCMClassification();

    // Init Handlers.
    HandlerLightPlacerTracker::init(pgd->lightPlacerJSONs());
    PatcherTextureHookConvertToCM::reset();
    PatcherTextureHookFixSSS::reset();

    //
    // MESH PATCHING.
    //

    // Create task queue for setting model uses.
    TaskQueue setModelUsesQueue;

    // Add tasks.
    auto meshes = pgd->meshes();

    // Incremental runs: find the meshes whose previous output is still valid and remove outputs that are not.
    std::unordered_set<std::filesystem::path> skippable;
    if (PGRunCache::hasPreviousRun()) {
        if (stageCallback)
            stageCallback(MeshPatchStage::EvaluatingPreviousOutput);

        skippable = PGRunCache::evaluateMeshes(meshes, shouldMultithread, progressCallback);
        PGRunCache::pruneStaleOutputs(skippable);
    }

    if (stageCallback)
        stageCallback(MeshPatchStage::Patching);

    // Create task tracker.
    TaskTracker taskTracker("Mesh Patcher", meshes.size());

    // Create runner.
    TaskPoolRunner meshRunner(shouldMultithread);
    if (progressCallback)
        taskTracker.setCallbackFunc(progressCallback);

    // Model uses of replayed meshes are applied in one batch: thousands of individual plugin calls would otherwise
    // dominate the runtime of an incremental run.
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

    // Blocks until all tasks are done.
    meshRunner.runTasks();

    // Apply the plugin model uses of all replayed meshes at once.
    if (!replayedMeshResults.empty())
        setModelUsesQueue.queueTask([results = std::move(replayedMeshResults)] { PGPlugin::setModelUses(results); });

    // Final validation for weight variants.
    const auto weightVariantErrors = PGMeshPermutationTracker::validateWeightedVariants();
    for (const auto& [meshPath, message] : weightVariantErrors) {
        // These errors belong to the mesh so they are replayed when the mesh is skipped in a later run.
        PGRunCache::appendDeferredMessage(meshPath, spdlog::level::err, message);
    }

    // Finalize handlers.
    HandlerLightPlacerTracker::finalize();

    // Wait for model uses to complete.
    // FIXME: This wait does not need to happen here, it just needs to end before finalize.
    if (setModelUsesQueue.isWorking()) {
        Logger::info("Waiting for setting plugin model uses to complete...");
        setModelUsesQueue.waitForCompletion();
    }
}
void PGPatcher::patchTextures(const bool& shouldMultithread,
                              const std::function<void(size_t,
                                                       size_t)>& progressCallback)
{
    auto* const pgd = PGGlobals::pgd();
    pgd->waitForMeshMapping();
    pgd->waitForCMClassification();

    // Incremental runs: replay reused generated textures and delete generated textures nobody needs anymore.
    PGRunCache::finalizeHooks();

    //
    // TEXTURE PATCHING.
    //

    // Texture runner.
    const auto textures = pgd->textures();

    // Create task tracker.
    TaskTracker textureTaskTracker("Texture Patcher", textures.size());

    // Create runner.
    TaskPoolRunner textureRunner(shouldMultithread);
    if (progressCallback)
        textureTaskTracker.setCallbackFunc(progressCallback);

    // Add tasks.
    for (const auto& texture : textures)
        textureRunner.addTask([&textureTaskTracker, &texture] { textureTaskTracker.completeJob(patchDDS(texture)); });

    // Blocks until all tasks are done.
    textureRunner.runTasks();
}
auto PGPatcher::patchMeta() -> std::map<std::filesystem::path,
                                        MeshMeta>
{
    const std::shared_lock lock(s_meshPatchInfoMutex);
    return s_meshPatchInfo;
}
void PGPatcher::sortMatches(std::vector<MatchMeta>& matches,
                            const std::vector<std::shared_ptr<PGModManager::Mod>>& modPriorityList)
{
    // Build a map of mod pointers to their priority rank (index in modPriorityList).
    std::unordered_map<const PGModManager::Mod*, size_t> modPriorityOrder;
    modPriorityOrder.reserve(modPriorityList.size());
    size_t priorityRank = 0;
    for (const auto& mod : modPriorityList)
        modPriorityOrder.emplace(mod.get(), priorityRank++);

    const size_t fallbackRank = modPriorityList.size();

    std::ranges::sort(matches, [&](const MatchMeta& a, const MatchMeta& b) {
        // Get priority ranks for each match's mod.
        size_t aRank = fallbackRank;
        if (a.mod) {
            const auto aIt = modPriorityOrder.find(a.mod.get());
            if (aIt != modPriorityOrder.end())
                aRank = aIt->second;
        }

        size_t bRank = fallbackRank;
        if (b.mod) {
            const auto bIt = modPriorityOrder.find(b.mod.get());
            if (bIt != modPriorityOrder.end())
                bRank = bIt->second;
        }

        // First sort by mod priority (lower rank = higher priority).
        if (aRank != bRank)
            return aRank < bRank;

        // Then sort by effective shader type (higher enum value wins; prefer transform target if set)
        const auto aShader = a.shaderTransformTo != PGEnums::ShapeShader::Unknown ? a.shaderTransformTo : a.shader;
        const auto bShader = b.shaderTransformTo != PGEnums::ShapeShader::Unknown ? b.shaderTransformTo : b.shader;
        if (aShader != bShader)
            return static_cast<int>(aShader) > static_cast<int>(bShader);

        // Finally sort by matched path filename A-Z.
        return a.matchedPath.native() < b.matchedPath.native();
    });
}
bool PGPatcher::hasConflictData()
{
    const std::shared_lock lock(s_meshPatchInfoMutex);
    return !s_meshPatchInfo.empty();
}
void PGPatcher::resetRunState()
{
    {
        const std::unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo.clear();
    }

    {
        const std::unique_lock lock(s_diffJSONMutex);
        s_diffJSON.clear();
    }
}
void PGPatcher::deleteOutputDir(const bool& preOutput,
                                const bool& shouldKeepIncrementalOutput)
{
    static const std::unordered_set<std::filesystem::path> foldersToDelete
        = { "meshes", "textures", "calientetools", "pbrnifpatcher", "lightplacer", "pbrtexturesets" };
    static const std::filesystem::path updateCacheFile = boost::to_lower_copy(PGRunCache::s_cacheFilename.wstring());
    static const std::filesystem::path updateCacheTempFile = updateCacheFile.wstring() + L".tmp";
    static const std::unordered_set<std::filesystem::path> filesToDelete
        = { "pgpatcher.esp", "parallaxgen_diff.json", updateCacheFile, updateCacheTempFile };
    static const std::vector<std::pair<std::wstring, std::wstring>> filesToDeleteParseRules = { { L"pg_", L".esp" } };
    static const std::unordered_set<std::filesystem::path> filesToIgnore = { "meta.ini" };
    static const std::unordered_set<std::filesystem::path> filesToDeletePreOutput = { "pgpatcher_output.zip" };

    // Kept when updating a previous output incrementally.
    static const std::unordered_set<std::filesystem::path> foldersToKeepIncremental
        = { "meshes", "textures", "calientetools" };
    static const std::unordered_set<std::filesystem::path> filesToKeepIncremental = { updateCacheFile };

    const auto outputDir = PGGlobals::pgd()->generatedPath();
    if (!std::filesystem::exists(outputDir) || !std::filesystem::is_directory(outputDir))
        return;

    std::vector<std::filesystem::path> filesToDeleteParsed;
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        const std::filesystem::path entryFilename = boost::to_lower_copy(entry.path().filename().wstring());

        bool outerContinue = false;
        for (const auto& [filesToDeleteStartsWith, filesToDeleteEndsWith] : filesToDeleteParseRules) {
            if (boost::starts_with(entryFilename.wstring(), filesToDeleteStartsWith)
                && boost::ends_with(entryFilename.wstring(), filesToDeleteEndsWith)) {
                filesToDeleteParsed.push_back(entry.path().filename());
                outerContinue = true;
                break;
            }
        }

        if (outerContinue)
            continue;

        if (entry.is_regular_file()
            && (filesToDelete.contains(entryFilename) || filesToIgnore.contains(entryFilename)
                || filesToDeletePreOutput.contains(entryFilename))) {
            continue;
        }

        if (entry.is_directory() && foldersToDelete.contains(entryFilename))
            continue;

        Logger::critical("Output directory has non-PGPatcher related files. The output directory should only contain "
                         "files generated by PGPatcher or empty. Exiting.");
        return;
    }

    if (shouldKeepIncrementalOutput) {
        Logger::info("Deleting old plugin and metadata files from output directory (meshes and textures are kept "
                     "for updating)...");
    } else {
        Logger::info("Deleting old output files from output directory...");
    }

    // Delete old output.
    try {
        filesToDeleteParsed.insert(filesToDeleteParsed.end(), filesToDelete.begin(), filesToDelete.end());
        for (const auto& fileToDelete : filesToDeleteParsed) {
            if (shouldKeepIncrementalOutput && filesToKeepIncremental.contains(fileToDelete))
                continue;

            const auto file = outputDir / fileToDelete;
            if (std::filesystem::exists(file))
                std::filesystem::remove(file);
        }

        for (const auto& folderToDelete : foldersToDelete) {
            if (shouldKeepIncrementalOutput && foldersToKeepIncremental.contains(folderToDelete))
                continue;

            const auto folder = outputDir / folderToDelete;
            if (std::filesystem::exists(folder))
                std::filesystem::remove_all(folder);
        }

        if (preOutput) {
            for (const auto& fileToDelete : filesToDeletePreOutput) {
                const auto file = outputDir / fileToDelete;
                if (std::filesystem::exists(file))
                    std::filesystem::remove(file);
            }
        }
    } catch (const std::exception& e) {
        Logger::critical("Failed to delete old output files: {}", e.what());
        return;
    }
}
bool PGPatcher::isOutputEmpty()
{
    static const std::unordered_set<std::filesystem::path> filesToIgnore
        = { "meta.ini", boost::to_lower_copy(PGRunCache::s_cacheFilename.wstring()) };

    // Recursive output dir.
    const auto outputDir = PGGlobals::pgd()->generatedPath();
    if (!std::filesystem::exists(outputDir) || !std::filesystem::is_directory(outputDir))
        return true;

    // Check if output dir is empty.
    for (const auto& entry : // NOLINT(readability-use-anyofallof)
         std::filesystem::recursive_directory_iterator(outputDir)) {
        const std::filesystem::path entryFilename = boost::to_lower_copy(entry.path().filename().wstring());
        if (entry.is_regular_file() && !filesToIgnore.contains(entryFilename))
            return false;
    }

    return true;
}
TaskTracker::Result PGPatcher::replayNIF(const std::filesystem::path& nifPath,
                                         std::vector<PGMeshPermutationTracker::MeshResult>& replayedMeshResults,
                                         std::mutex& replayedMeshResultsMutex)
{
    const Logger::Prefix nifPrefix(nifPath.wstring());

    const auto* record = PGRunCache::previousRecord(nifPath);
    if (!record)
        throw std::runtime_error("Update cache record not found for mesh: " + nifPath.string());

    Logger::trace("Mesh is unchanged since the previous run, reusing previous output");

    // Warnings and errors that patching this mesh produced.
    PGRunCache::replayMessages(*record);

    // Previous output files are still in the output directory.
    auto* const pgd = PGGlobals::pgd();
    for (const auto& [output, size] : record->outputFiles)
        pgd->addGeneratedFile(output);

    // Generated textures this mesh depends on.
    for (const auto& [kind, texPath] : record->hookRegistrations)
        PGRunCache::replayHookRegistration(kind, texPath);

    // Plugin model uses (applied in one batch by the caller).
    {
        const std::scoped_lock lock(replayedMeshResultsMutex);
        replayedMeshResults.insert(replayedMeshResults.end(), record->meshResults.begin(), record->meshResults.end());
    }

    // Light placer handler.
    for (const auto& meshResult : record->meshResults)
        HandlerLightPlacerTracker::handleNIFCreated(nifPath, meshResult.meshPath);

    // Diff JSON.
    if (record->hasDiff) {
        const auto diffJSONKey = StringUtil::utf16toUTF8(nifPath.wstring());
        const std::unique_lock lock(s_diffJSONMutex);
        s_diffJSON[diffJSONKey]["crc32original"] = record->crc32Original;
        s_diffJSON[diffJSONKey]["crc32patched"] = record->crc32Patched;
    }

    // Conflict viewer metadata.
    {
        auto meshMeta = PGRunCache::buildMeshMeta(record->meta);
        const std::unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo[nifPath] = std::move(meshMeta);
    }

    // The record stays valid for the next run.
    PGRunCache::carryOverRecord(nifPath);

    return TaskTracker::Result::Success;
}
nlohmann::json PGPatcher::diffJSON()
{
    const std::shared_lock lock(s_diffJSONMutex);
    return s_diffJSON;
}
TaskTracker::Result PGPatcher::patchNIF(const std::filesystem::path& nifPath,
                                        TaskQueue& setModelUsesQueue,
                                        const bool& forceBasePatch,
                                        const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                                        const bool& checkAllowedRecTypes,
                                        const bool& excludeFacegens)
{
    const Logger::Prefix nifPrefix(nifPath.wstring());

    // Record everything this mesh depends on so a later run can skip it if nothing changed.
    std::unique_ptr<PGRunCache::MeshRecorder> recorder;
    if (PGRunCache::isEnabled())
        recorder = std::make_unique<PGRunCache::MeshRecorder>(nifPath);

    // Get mod of nif.
    if (PGGlobals::isPGMMSet()) {
        const auto mod = PGGlobals::pgmm()->modByFileSmart(nifPath);
        if (mod) {
            const std::shared_lock<std::shared_mutex> modLock(mod->mutex);
            PGRunCache::recordModState(mod->name, mod->isEnabled, mod->areMeshesIgnored);
            if (mod && mod->areMeshesIgnored) {
                Logger::trace(L"Skipping NIF patching for mod with ignored meshes: {}", mod->name);
                return TaskTracker::Result::Success;
            }
        }
    }

    // Create mesh tracker for this NIF.
    auto meshTracker = PGMeshPermutationTracker(nifPath);

    // Check if we have the nif in cache.
    const auto* const pgd = PGGlobals::pgd();
    const auto& meshes = pgd->meshes();
    if (!meshes.contains(nifPath))
        throw std::runtime_error("NIF not found in cache: " + nifPath.string());

    auto nifCache = meshes.at(nifPath);
    const auto originalMeshUses = nifCache.meshUses;
    const bool isFacegen = PGNIFUtil::isFacegenMesh(nifPath);
    if (isFacegen && excludeFacegens) {
        Logger::trace(L"Skipping NIF patching for facegen mesh (facegens excluded): {}", nifPath.wstring());
        return TaskTracker::Result::Success;
    }

    // BodySlide ShapeData never appears in plugins either, so it is always patched for a dummy use like facegen.
    const bool isBodySlideShapeData = PGNIFUtil::isBodySlideShapeDataMesh(nifPath);

    if (nifCache.meshUses.empty() && !forceBasePatch && !isFacegen && !isBodySlideShapeData) {
        Logger::trace(L"Skipping NIF patching for mesh with no plugin uses: {}", nifPath.wstring());
        return TaskTracker::Result::Success;
    }

    meshTracker.load();

    // Prepare meta.
    MeshMeta meshMeta;

    if (nifCache.meshUses.empty() && (forceBasePatch || isFacegen || isBodySlideShapeData)) {
        // Add a dummy mesh use to trigger base patching (pgtools uses this since no plugins).
        // Always trigger dummy for facegen and BodySlide ShapeData meshes since they never appear in plugins.
        Logger::debug(L"Forcing non-plugin patching context for mesh: {}", nifPath.wstring());
        const PGMeshPermutationTracker::FormKey dummyFormKey = { .modKey = L"", .formID = 0, .subMODL = "" };
        const PGPlugin::MeshUseAttributes dummyUse = {
            .isWeighted = false,
            .isSinglepassMATO = false,
            .isFacegen = isFacegen,
            .isBodySlideShapeData = isBodySlideShapeData,
            .isIgnored = false,
            .isDummyUse = true,
            .recType = PGPlugin::ModelRecordType::Unknown,
            .alternateTextures = { },
        };
        nifCache.meshUses.insert(nifCache.meshUses.begin(), std::make_pair(dummyFormKey, dummyUse));
    } else if (isFacegen) {
        // If this is true then there are mesh uses but this is a facegen mesh so we should throw a warning.
        Logger::warn(L"NIF has mesh uses but is detected as a facegen mesh: {}", nifPath.wstring());
        return TaskTracker::Result::Failure;
    }

    // Alternate texture results of plugin uses that keep using the unchanged original mesh.
    std::vector<std::pair<PGMeshPermutationTracker::FormKey, std::unordered_map<unsigned, PGTypes::TextureSet>>>
        originalMeshAltTexResults;

    // Loop through each use.
    for (auto use : nifCache.meshUses) {
        // Process mesh patch for each and every occurance of the mesh in plugins.
        if (use.second.isIgnored) {
            // This record is ignored, trigger tracker to ignore the base mesh and skip this patch.
            meshTracker.ignoreBaseMesh();
            continue;
        }

        if (checkAllowedRecTypes && !use.second.isDummyUse && !allowedModelRecTypes.contains(use.second.recType)) {
            // This record is not in the allowed record types, trigger tracker to ignore the base mesh and skip this
            // patch.
            meshTracker.ignoreBaseMesh();
            continue;
        }

        const auto formKey = use.first;

        const Logger::Prefix dupPrefix(
            fmt::format(L"{}:{:06X}:{}", formKey.modKey, formKey.formID, StringUtil::utf8toUTF16(formKey.subMODL)));

        // Alternate textures do exist so we need to do some processing.
        // Stage a new mesh.
        auto* stagedNIF = meshTracker.stageMesh();
        std::unordered_set<unsigned> enforceCheckBlocks;
        const auto alternateTexturesBefore = use.second.alternateTextures;
        if (!processNIF(nifPath,
                        stagedNIF,
                        meshMeta,
                        use.second,
                        use.second.isSinglepassMATO,
                        formKey,
                        use.second.recType,
                        use.second.alternateTextures,
                        enforceCheckBlocks)) {
            return TaskTracker::Result::Failure;
        }

        switch (
            meshTracker.commitMesh(formKey, use.second.isWeighted, use.second.alternateTextures, enforceCheckBlocks)) {
        case PGMeshPermutationTracker::CommitResult::Added:
            Logger::trace("Mesh committed");
            break;
        case PGMeshPermutationTracker::CommitResult::MergedIntoOutput:
            Logger::trace("Mesh not committed (identical to an existing output mesh)");
            break;
        case PGMeshPermutationTracker::CommitResult::UnchangedFromOriginal:
            Logger::trace("Mesh not committed (no changes)");
            if (use.second.alternateTextures != alternateTexturesBefore) {
                // The mesh itself needed no changes (it was patched before, for example), but the alternate textures of
                // this use were patched. They still have to reach the plugin, for the original mesh.
                originalMeshAltTexResults.emplace_back(formKey, use.second.alternateTextures);
            }
            break;
        case PGMeshPermutationTracker::CommitResult::AlreadyProcessed:
            Logger::trace("Mesh not committed (form key already processed)");
            break;
        }
    }

    // Save meshes.
    auto saveResults = meshTracker.saveMeshes();
    if (!originalMeshAltTexResults.empty()) {
        // No file is written for these uses: their records keep the original mesh and only get the patched alternate
        // textures. The shapes are not reordered since the mesh is not saved, so the shape indices stay as they are.
        saveResults.first.push_back(PGMeshPermutationTracker::MeshResult {
            .meshPath = nifPath,
            .altTexResults = std::move(originalMeshAltTexResults),
            .idxCorrections = { },
            .inverseIdxCorrectionsPatching = { },
        });
    }
    setModelUsesQueue.queueTask([saveResults] { PGPlugin::setModelUses(saveResults.first); });

    // Run handlers.
    for (const auto& meshResult : saveResults.first) {
        const Logger::Prefix handlerPrefix(L"Handler: " + meshResult.meshPath.wstring());
        HandlerLightPlacerTracker::handleNIFCreated(nifPath, meshResult.meshPath);
    }
    // Add to diff JSON.
    const auto diffJSONKey = StringUtil::utf16toUTF8(nifPath.wstring());
    if (saveResults.second.second) {
        // Only add to diff if the base mesh actually saved, which is indicated by a non-zero patched crc32.
        Logger::trace(
            "Base mesh was updated, saving diff CRC32: {} -> {}", saveResults.second.first, saveResults.second.second);

        const std::unique_lock lock(s_diffJSONMutex);
        s_diffJSON[diffJSONKey]["crc32original"] = saveResults.second.first;
        s_diffJSON[diffJSONKey]["crc32patched"] = saveResults.second.second;
    }

    // Save mesh meta.
    {
        const std::unique_lock lock(s_meshPatchInfoMutex);
        s_meshPatchInfo[nifPath] = meshMeta;
    }

    // Store the record for incremental runs.
    if (recorder) {
        recorder->setUses(originalMeshUses);
        recorder->setMeshResults(saveResults.first);
        if (saveResults.second.second)
            recorder->setDiff(saveResults.second.first, saveResults.second.second);
        recorder->setMeta(meshMeta);
        recorder->commit();
    }

    return TaskTracker::Result::Success;
}
TaskTracker::Result PGPatcher::patchDDS(const std::filesystem::path& ddsPath)
{
    const auto result = TaskTracker::Result::Success;

    auto* const pgd = PGGlobals::pgd();

    // Check if this texture needs to be processed.
    if (s_texPatchers.globalPatchers.empty() && !PatcherTextureHookConvertToCM::isInProcessList(ddsPath)
        && !PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        // No patchers, so we can skip.
        return result;
    }

    Logger::debug(L"Cache for DDS {} is invalidated or nonexistent", ddsPath.wstring());

    // Prep.
    Logger::trace(L"Starting Processing");

    // Only allow DDS files.
    const std::string ddsFileExt = ddsPath.extension().string();
    if (ddsFileExt != ".dds")
        throw std::runtime_error("File is not a DDS file");

    DirectX::ScratchImage ddsImage;
    if (!PGGlobals::pgD3D()->getDDS(ddsPath, ddsImage)) {
        Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
        return TaskTracker::Result::Failure;
    }

    // Run any hook patchers (these create other textures).
    if (PatcherTextureHookConvertToCM::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookConvertToCM(ddsPath, &ddsImage);
        if (!patcher.applyPatch()) {
            Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
            return TaskTracker::Result::Failure;
        }
    }
    if (PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookFixSSS(ddsPath, &ddsImage);
        if (!patcher.applyPatch()) {
            Logger::error(L"Unable to process texture: {}", ddsPath.wstring());
            return TaskTracker::Result::Failure;
        }
    }

    bool ddsModified = false;

    const auto patcherObjects = createDDSPatcherObjects(ddsPath, &ddsImage);

    // Global patchers.
    for (const auto& patcher : patcherObjects.globalPatchers)
        patcher->applyPatch(ddsModified);

    if (ddsModified) {
        // Save to output.
        const std::filesystem::path outputFile = pgd->generatedPath() / ddsPath;
        std::filesystem::create_directories(outputFile.parent_path());

        const HRESULT hr = DirectX::SaveToDDSFile(ddsImage.GetImages(),
                                                  ddsImage.GetImageCount(),
                                                  ddsImage.GetMetadata(),
                                                  DirectX::DDS_FLAGS_NONE,
                                                  outputFile.c_str());
        if (FAILED(hr)) {
            Logger::error(L"Unable to save texture {}: {}", outputFile.wstring(), PGD3D::hresultErrorMessage(hr));
            return TaskTracker::Result::Failure;
        }

        // Update file map with generated file.
        pgd->addGeneratedFile(ddsPath);
    }

    return result;
}
PatcherUtil::PatcherTextureObjectSet PGPatcher::createDDSPatcherObjects(const std::filesystem::path& ddsPath,
                                                                        DirectX::ScratchImage* dds)
{
    auto patcherObjects = PatcherUtil::PatcherTextureObjectSet();
    for (const auto& factory : s_texPatchers.globalPatchers) {
        auto patcher = factory(ddsPath, dds);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    return patcherObjects;
}
