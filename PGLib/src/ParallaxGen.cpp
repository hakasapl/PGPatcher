#include "ParallaxGen.hpp"

#include <DirectXTex.h>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/thread.hpp>
#include <filesystem>
#include <memory>
#include <miniz.h>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <ranges>
#include <shared_mutex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <d3d11.h>

#include "PGDiag.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenTask.hpp"
#include "ParallaxGenWarnings.hpp"
#include "handlers/HandlerLightPlacerTracker.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

using namespace std;
using namespace ParallaxGenUtil;
using namespace nifly;

// statics
PatcherUtil::PatcherMeshSet ParallaxGen::s_meshPatchers;
PatcherUtil::PatcherTextureSet ParallaxGen::s_texPatchers;

std::shared_mutex ParallaxGen::s_diffJSONMutex;
nlohmann::json ParallaxGen::s_diffJSON;

void ParallaxGen::loadPatchers(
    const PatcherUtil::PatcherMeshSet& meshPatchers, const PatcherUtil::PatcherTextureSet& texPatchers)
{
    s_meshPatchers = meshPatchers;
    s_texPatchers = texPatchers;
}

void ParallaxGen::patch(const bool& multiThread, const bool& patchPlugin)
{
    auto* const pgd = PGGlobals::getPGD();

    // Init Handlers
    HandlerLightPlacerTracker::init(pgd->getLightPlacerJSONs());

    //
    // MESH PATCHING
    //

    // Add tasks
    auto meshes = pgd->getMeshes();

    // Create task tracker
    ParallaxGenTask taskTracker("Mesh Patcher", meshes.size());

    // Create runner
    ParallaxGenRunner meshRunner(multiThread);

    for (auto& [mesh, nifCache] : meshes) {
        meshRunner.addTask(
            [&taskTracker, &mesh, &patchPlugin] { taskTracker.completeJob(patchNIF(mesh, patchPlugin)); });
    }

    // Blocks until all tasks are done
    meshRunner.runTasks();

    //
    // TEXTURE PATCHING
    //

    // texture runner
    auto textures = pgd->getTextures();

    // Create task tracker
    ParallaxGenTask textureTaskTracker("Texture Patcher", textures.size());

    // Create runner
    ParallaxGenRunner textureRunner(multiThread);

    // Add tasks
    for (const auto& texture : textures) {
        textureRunner.addTask([&textureTaskTracker, &texture] { textureTaskTracker.completeJob(patchDDS(texture)); });
    }

    // Blocks until all tasks are done
    textureRunner.runTasks();

    // Print any resulting warning
    ParallaxGenWarnings::printWarnings();

    // Finalize handlers
    HandlerLightPlacerTracker::finalize();
}

void ParallaxGen::populateModData(const bool& multiThread, const bool& patchPlugin)
{
    if (s_meshPatchers.globalPatchers.empty() && s_meshPatchers.shaderPatchers.empty()
        && s_meshPatchers.prePatchers.empty() && s_meshPatchers.postPatchers.empty()) {
        return;
    }

    auto* const pgd = PGGlobals::getPGD();
    auto meshes = pgd->getMeshes();

    // Create task tracker
    static constexpr int PROGRESS_INTERVAL_CONFLICTS = 10;
    ParallaxGenTask taskTracker("Finding Mod Conflicts", meshes.size(), PROGRESS_INTERVAL_CONFLICTS);

    // Create runner
    ParallaxGenRunner runner(multiThread);

    // Add tasks
    for (auto& [mesh, nifCache] : meshes) {
        runner.addTask([&taskTracker, &mesh, &nifCache, &patchPlugin] {
            taskTracker.completeJob(populateModInfoFromNIF(mesh, nifCache, patchPlugin));
        });
    }

    // Blocks until all tasks are done
    runner.runTasks();
}

void ParallaxGen::deleteOutputDir(const bool& preOutput)
{
    static const unordered_set<filesystem::path> foldersToDelete
        = { "meshes", "textures", "pbrnifpatcher", "lightplacer", "pbrtexturesets" };
    static const unordered_set<filesystem::path> filesToDelete
        = { "parallaxgen.esp", "parallaxgen_diff.json", "parallaxgen_diag.json" };
    static const vector<pair<wstring, wstring>> filesToDeleteParseRules = { { L"pg_", L".esp" } };
    static const unordered_set<filesystem::path> filesToIgnore = { "meta.ini" };
    static const unordered_set<filesystem::path> filesToDeletePreOutput = { "pgpatcher_output.zip" };

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

        spdlog::critical("Output directory has non-ParallaxGen related files. The output directory should only contain "
                         "files generated by ParallaxGen or empty. Exiting.");
        exit(1);
    }

    spdlog::info("Deleting old output files from output directory...");

    // Delete old output
    filesToDeleteParsed.insert(filesToDeleteParsed.end(), filesToDelete.begin(), filesToDelete.end());
    for (const auto& fileToDelete : filesToDeleteParsed) {
        const auto file = outputDir / fileToDelete;
        if (filesystem::exists(file)) {
            filesystem::remove(file);
        }
    }

    for (const auto& folderToDelete : foldersToDelete) {
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
}

auto ParallaxGen::isOutputEmpty() -> bool
{
    static const unordered_set<filesystem::path> filesToIgnore = { "meta.ini" };

    // recursive output dir
    const auto outputDir = PGGlobals::getPGD()->getGeneratedPath();
    if (!filesystem::exists(outputDir) || !filesystem::is_directory(outputDir)) {
        return true;
    }

    // check if output dir is empty
    for (const auto& entry : // NOLINT(readability-use-anyofallof)
        filesystem::recursive_directory_iterator(outputDir)) {
        if (entry.is_regular_file() && !filesToIgnore.contains(entry.path().filename())) {
            return false;
        }
    }

    return true;
}

auto ParallaxGen::getDiffJSON() -> nlohmann::json
{
    const shared_lock lock(s_diffJSONMutex);
    return s_diffJSON;
}

auto ParallaxGen::populateModInfoFromNIF(const std::filesystem::path& nifPath,
    const ParallaxGenDirectory::NifCache& nifCache, const bool& patchPlugin) -> ParallaxGenTask::PGResult
{
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nullptr);

    // loop through each texture set in cache
    for (const auto& textureSet : nifCache.textureSets) {
        // find matches
        auto matches = getMatches(textureSet.second, patcherObjects, true, false);

        // find all uses of this nif
        if (patchPlugin) {
            const auto meshUses = ParallaxGenPlugin::getModelUses(nifPath.wstring());
            for (const auto& use : meshUses) {
                if (use.second.alternateTextures.empty()) {
                    // no alternate textures, skip processing
                    continue;
                }

                // loop through each alternate texture set
                for (const auto& [altTexIndex, altTexSet] : use.second.alternateTextures) {
                    // find matches
                    const auto curMatches = getMatches(altTexSet, patcherObjects, true, use.second.singlepassMATO);
                    matches.insert(matches.end(), curMatches.begin(), curMatches.end());
                }
            }
        }

        // loop through matches
        for (const auto& match : matches) {
            if (match.mod == nullptr) {
                continue;
            }

            if (match.shader == NIFUtil::ShapeShader::NONE) {
                // this is a default match, so we don't auto enable
                continue;
            }

            // enable mod
            const unique_lock<shared_mutex> uniqueLock(match.mod->mutex);
            if (match.mod->isNew && !match.mod->isEnabled) {
                // we only care to auto enable NEW mods in the list
                match.mod->isEnabled = true;
            }
        }
    }

    return ParallaxGenTask::PGResult::SUCCESS;
}

auto ParallaxGen::patchNIF(const std::filesystem::path& nifPath, const bool& patchPlugin) -> ParallaxGenTask::PGResult
{
    // Create mesh tracker for this NIF
    auto meshTracker = MeshTracker(nifPath);

    // Patch base NIF
    auto* baseNIF = meshTracker.stageMesh();
    unordered_map<unsigned int, NIFUtil::TextureSet> alternateTextures; // empty alternate textures for base mesh
    if (!processNIF(nifPath, baseNIF, false, alternateTextures)) {
        return ParallaxGenTask::PGResult::FAILURE;
    }
    meshTracker.commitBaseMesh();

    if (!patchPlugin) {
        // Just save base mesh
        meshTracker.saveMeshes();
        return ParallaxGenTask::PGResult::SUCCESS;
    }

    // Find all uses of this mesh in plugins
    const auto meshUses = ParallaxGenPlugin::getModelUses(nifPath.wstring());
    // loop through each use
    for (auto use : meshUses) {
        // check if alternate textures even exist
        if (use.second.alternateTextures.empty()) {
            // no alternate textures, skip processing just register the use
            meshTracker.addFormKeyForBaseMesh(use.first);
            continue;
        }

        // alternate textures do exist so we need to do some processing
        // stage a new mesh
        auto* stagedNIF = meshTracker.stageMesh();
        if (!processNIF(nifPath, stagedNIF, use.second.singlepassMATO, use.second.alternateTextures)) {
            return ParallaxGenTask::PGResult::FAILURE;
        }
        meshTracker.commitDupMesh(use.first, use.second.alternateTextures);
    }

    // Save all meshes
    const auto saveResults = meshTracker.saveMeshes();
    ParallaxGenPlugin::setModelUses(saveResults.first);

    // Add to diff JSON
    const auto diffJSONKey = utf16toUTF8(nifPath.wstring());
    if (saveResults.second.second != 0) {
        // only add to diff if the base mesh actually saved, which is indicated by a non-zero patched crc32
        const unique_lock lock(s_diffJSONMutex);
        s_diffJSON[diffJSONKey]["crc32original"] = saveResults.second.first;
        s_diffJSON[diffJSONKey]["crc32patched"] = saveResults.second.second;
    }

    return ParallaxGenTask::PGResult::SUCCESS;
}

auto ParallaxGen::processNIF(const std::filesystem::path& nifPath, nifly::NifFile* nif, bool singlepassMATO,
    std::unordered_map<unsigned int, NIFUtil::TextureSet>& alternateTextures) -> bool
{
    auto* const pgd = PGGlobals::getPGD();
    const auto modPtr = pgd->getMod(nifPath);

    // Create patcher objects
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nif);

    // Get shapes and index 3ds (this is in the order as they would show up as 3d indices in plugins)
    const auto shapes = NIFUtil::getShapesWithBlockIDs(nif);

    for (const auto& [nifShape, oldIndex3D] : shapes) {
        if (nifShape == nullptr) {
            // Skip if shape is null (invalid shapes)
            continue;
        }

        if (!NIFUtil::isPatchableShape(*nif, *nifShape)) {
            // Skip if not patchable shape
            continue;
        }

        // get shape name and blockid
        const auto shapeBlockID = nif->GetBlockID(nifShape);
        const auto shapeName = nifShape->name.get();
        const auto shapeIDStr = to_string(shapeBlockID) + " / " + shapeName;

        const Logger::Prefix prefixShape(shapeIDStr);

        NIFUtil::TextureSet* ptrAltTex = nullptr;
        if (alternateTextures.contains(oldIndex3D)) {
            ptrAltTex = &alternateTextures.at(oldIndex3D);
        }
        if (!processNIFShape(nifPath, nif, nifShape, patcherObjects, singlepassMATO, ptrAltTex)) {
            return false;
        }
    }

    // Run global patchers
    for (const auto& globalPatcher : patcherObjects.globalPatchers) {
        const Logger::Prefix prefixPatches(utf8toUTF16(globalPatcher->getPatcherName()));
        globalPatcher->applyPatch();
    }

    return true;
}

auto ParallaxGen::processNIFShape(const std::filesystem::path& nifPath, nifly::NifFile* nif, nifly::NiShape* nifShape,
    const PatcherUtil::PatcherMeshObjectSet& patchers, bool singlepassMATO, NIFUtil::TextureSet* alternateTexture)
    -> bool
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    if (nifShape == nullptr) {
        throw runtime_error("NIFShape is null");
    }

    // Prep
    Logger::trace(L"Starting Processing");

    // apply prepatchers
    for (const auto& prePatcher : patchers.prePatchers) {
        const Logger::Prefix prefixPatches(prePatcher->getPatcherName());
        prePatcher->applyPatch(*nifShape);
    }

    if (NIFUtil::isShaderPatchableShape(*nif, *nifShape)) {
        NIFUtil::TextureSet slots;
        if (alternateTexture == nullptr) {
            slots = PatcherMeshShader::getTextureSet(nifPath, *nif, *nifShape);
        } else {
            slots = *alternateTexture;
        }

        // Allowed shaders from result of patchers
        auto matches = getMatches(slots, patchers, false, singlepassMATO, &patchers, nifShape);

        PatcherUtil::ShaderPatcherMatch winningShaderMatch;
        // Get winning match
        if (!matches.empty()) {
            winningShaderMatch = getWinningMatch(matches);

            // Apply transforms
            applyTransformIfNeeded(winningShaderMatch, patchers);

            // loop through patchers
            NIFUtil::TextureSet newSlots;
            patchers.shaderPatchers.at(winningShaderMatch.shader)
                ->applyPatch(slots, *nifShape, winningShaderMatch.match, newSlots);

            if (alternateTexture != nullptr) {
                // assign new slots to propogate upstream
                *alternateTexture = newSlots;
            }

            // Post warnings if any
            for (const auto& curMatchedFrom : winningShaderMatch.match.matchedFrom) {
                ParallaxGenWarnings::mismatchWarn(
                    winningShaderMatch.match.matchedPath, newSlots.at(static_cast<int>(curMatchedFrom)));
            }

            ParallaxGenWarnings::meshWarn(winningShaderMatch.match.matchedPath, nifPath.wstring());
        }
    }

    // apply postpatchers
    for (const auto& postPatcher : patchers.postPatchers) {
        const Logger::Prefix prefixPatches(postPatcher->getPatcherName());
        postPatcher->applyPatch(*nifShape);
    }

    return true;
}

auto ParallaxGen::getMatches(const NIFUtil::TextureSet& slots, const PatcherUtil::PatcherMeshObjectSet& patchers,
    const bool& dryRun, bool singlepassMATO, const PatcherUtil::PatcherMeshObjectSet* patcherObjects,
    nifly::NiShape* shape) -> std::vector<PatcherUtil::ShaderPatcherMatch>
{
    if (PGGlobals::getPGD() == nullptr) {
        throw runtime_error("PGD is null");
    }

    vector<PatcherUtil::ShaderPatcherMatch> matches;
    unordered_set<shared_ptr<ModManagerDirectory::Mod>, ModManagerDirectory::Mod::ModHash> modSet;

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
            PatcherUtil::ShaderPatcherMatch curMatch;
            curMatch.mod = PGGlobals::getPGD()->getMod(match.matchedPath);

            if (!dryRun && curMatch.mod != nullptr) {
                const std::shared_lock lk(curMatch.mod->mutex);
                if (!curMatch.mod->isEnabled) {
                    // do not add match if mod it matched from is disabled
                    Logger::trace(L"Rejecting: Mod '{}' is not enabled", curMatch.mod->name);
                    continue;
                }
            }

            curMatch.shader = shader;
            curMatch.match = match;
            curMatch.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

            // See if transform is possible
            if (patchers.shaderTransformPatchers.contains(shader)) {
                const auto& [shaderTransformTo, transformPatcher] = patchers.shaderTransformPatchers.at(shader);
                // loop from highest element of map to 0
                curMatch.shaderTransformTo = shaderTransformTo;
            }

            if (shader != NIFUtil::ShapeShader::NONE) {
                // a non-default match is available. If this match is the same mod as any default matches, remove the
                // defaults
                std::erase_if(matches, [&curMatch](const PatcherUtil::ShaderPatcherMatch& m) -> bool {
                    return m.shader == NIFUtil::ShapeShader::NONE && m.mod != nullptr && curMatch.mod != nullptr
                        && m.mod == curMatch.mod;
                });
            }

            // Verify shape can apply
            if (patcherObjects != nullptr) {
                if (!patcherObjects->shaderPatchers.at(shader)->canApply(*shape, singlepassMATO)) {
                    // base shaders can't do it, lets check transforms
                    if (curMatch.shaderTransformTo == NIFUtil::ShapeShader::UNKNOWN) {
                        Logger::trace(L"Rejecting: Shape cannot apply shader");
                        continue;
                    }

                    if (!patcherObjects->shaderPatchers.at(curMatch.shaderTransformTo)
                            ->canApply(*shape, singlepassMATO)) {
                        Logger::trace(L"Rejecting: Shape cannot apply shader");
                        continue;
                    }
                }
            }

            matches.push_back(curMatch);
            if (curMatch.mod != nullptr) {
                // add mod to set
                modSet.insert(curMatch.mod);
            }
        }
    }

    // Populate conflict mods if set
    if (dryRun && !modSet.empty()) {
        // add mods to conflict set
        for (const auto& match : matches) {
            if (match.mod == nullptr) {
                continue;
            }

            const unique_lock lock(match.mod->mutex);

            match.mod->shaders.insert(match.shader);
            for (const auto& conflictMod : modSet) {
                if (conflictMod != match.mod) {
                    match.mod->conflicts.insert(conflictMod);
                }
            }
        }

        return matches;
    }

    // Populate diag JSON if set with each match
    {
        const PGDiag::Prefix diagShaderPatcherPrefix("shaderPatcherMatches", nlohmann::json::value_t::array);
        for (const auto& match : matches) {
            PGDiag::pushBack(match.getJSON());
        }
    }

    return matches;
}

auto ParallaxGen::getWinningMatch(const vector<PatcherUtil::ShaderPatcherMatch>& matches)
    -> PatcherUtil::ShaderPatcherMatch
{
    // Find winning mod
    int maxPriority = -1;
    auto winningShaderMatch = PatcherUtil::ShaderPatcherMatch();

    for (const auto& match : matches) {
        wstring modName;
        int curPriority = -1;
        if (match.mod != nullptr) {
            modName = match.mod->name;
            curPriority = match.mod->priority;
        }

        const Logger::Prefix prefixMod(modName);
        Logger::trace("Checking mod");

        if (curPriority < maxPriority) {
            // skip mods with lower priority than current winner
            Logger::trace(L"Rejecting: Mod has lower priority than current winner");
            continue;
        }

        Logger::trace(L"Mod accepted");
        maxPriority = curPriority;
        winningShaderMatch = match;
    }

    Logger::trace(L"Winning mod: {}", winningShaderMatch.mod == nullptr ? L"" : winningShaderMatch.mod->name);
    return winningShaderMatch;
}

auto ParallaxGen::applyTransformIfNeeded(
    PatcherUtil::ShaderPatcherMatch& match, const PatcherUtil::PatcherMeshObjectSet& patchers) -> bool
{
    // Transform if required
    if (match.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
        // Find transform object
        auto* const transform = patchers.shaderTransformPatchers.at(match.shader).second.get();

        // Transform Shader
        transform->transform(match.match, match.match);

        match.shader = match.shaderTransformTo;
        match.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

        return true;
    }

    return false;
}

auto ParallaxGen::createNIFPatcherObjects(const std::filesystem::path& nifPath, nifly::NifFile* nif)
    -> PatcherUtil::PatcherMeshObjectSet
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
        patcherObjects.shaderTransformPatchers[shader] = { factory.first, std::move(transform) };
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

auto ParallaxGen::patchDDS(const filesystem::path& ddsPath) -> ParallaxGenTask::PGResult
{
    auto result = ParallaxGenTask::PGResult::SUCCESS;

    auto* const pgd = PGGlobals::getPGD();

    // Check if this texture needs to be processed
    if (s_texPatchers.globalPatchers.empty() && !PatcherTextureHookConvertToCM::isInProcessList(ddsPath)
        && !PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        // No patchers, so we can skip
        return result;
    }

    Logger::debug(L"Cache for DDS {} is invalidated or nonexistent", ddsPath.wstring());

    const PGDiag::Prefix nifPrefix("textures", nlohmann::json::value_t::object);

    // Prep
    Logger::trace(L"Starting Processing");

    // only allow DDS files
    const string ddsFileExt = ddsPath.extension().string();
    if (ddsFileExt != ".dds") {
        throw runtime_error("File is not a DDS file");
    }

    DirectX::ScratchImage ddsImage;
    if (!PGGlobals::getPGD3D()->getDDS(ddsPath, ddsImage)) {
        Logger::error(L"Unable to load DDS file: {}", ddsPath.wstring());
        return ParallaxGenTask::PGResult::FAILURE;
    }

    // Run any hook patchers (these create other textures)
    if (PatcherTextureHookConvertToCM::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookConvertToCM(ddsPath, &ddsImage);
        patcher.applyPatch();
    }
    if (PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookFixSSS(ddsPath, &ddsImage);
        patcher.applyPatch();
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

        const HRESULT hr = DirectX::SaveToDDSFile(ddsImage.GetImages(), ddsImage.GetImageCount(),
            ddsImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
        if (FAILED(hr)) {
            Logger::error(L"Unable to save DDS {}: {}", outputFile.wstring(),
                ParallaxGenUtil::utf8toUTF16(ParallaxGenD3D::getHRESULTErrorMessage(hr)));
            return ParallaxGenTask::PGResult::FAILURE;
        }

        // Update file map with generated file
        pgd->addGeneratedFile(ddsPath, nullptr);
    }

    return result;
}

auto ParallaxGen::createDDSPatcherObjects(const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds)
    -> PatcherUtil::PatcherTextureObjectSet
{
    auto patcherObjects = PatcherUtil::PatcherTextureObjectSet();
    for (const auto& factory : s_texPatchers.globalPatchers) {
        auto patcher = factory(ddsPath, dds);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    return patcherObjects;
}
