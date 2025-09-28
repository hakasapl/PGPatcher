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

auto ParallaxGen::getMeshesFromPluginResults(const std::unordered_map<int, NIFUtil::ShapeShader>& shadersAppliedMesh,
    const unordered_map<nifly::NiShape*, int>& shapeIdxs,
    const std::unordered_map<int, std::unordered_map<int, ParallaxGenPlugin::TXSTResult>>& recordHandleTracker)
    -> std::unordered_map<int,
        std::pair<std::vector<ParallaxGenPlugin::TXSTResult>, std::unordered_map<int, NIFUtil::ShapeShader>>>
{
    unordered_map<int, pair<vector<ParallaxGenPlugin::TXSTResult>, unordered_map<int, NIFUtil::ShapeShader>>> output;

    unordered_map<string, int> meshTracker;

    int numMesh = 0;
    for (const auto& [modelRecHandle, results] : recordHandleTracker) {
        // Store results that should apply
        vector<ParallaxGenPlugin::TXSTResult> resultsToApply;

        // store the serialization of shaders applied to meshes to keep track of what already exists
        string serializedMesh;

        // make a result shaders map which is the shaders needed for this specific plugin MODL record on the mesh
        unordered_map<int, NIFUtil::ShapeShader> resultShaders;
        for (const auto& [shape, oldIndex3D] : shapeIdxs) {
            NIFUtil::ShapeShader shaderApplied = NIFUtil::ShapeShader::UNKNOWN;
            if (results.contains(oldIndex3D)) {
                // plugin has a result
                shaderApplied = results.at(oldIndex3D).shader;
                resultsToApply.push_back(results.at(oldIndex3D));
            }

            if (shaderApplied == NIFUtil::ShapeShader::UNKNOWN) {
                // Set shader in nif shape
                shaderApplied = shadersAppliedMesh.at(oldIndex3D);
            }

            resultShaders[oldIndex3D] = shaderApplied;
            serializedMesh += to_string(oldIndex3D) + "/" + to_string(static_cast<int>(shaderApplied)) + ",";
        }

        int newNifIdx = 0;
        if (meshTracker.contains(serializedMesh)) {
            // duplicate mesh already exists, we only need to assign mesh
            newNifIdx = meshTracker[serializedMesh];
        } else {
            if (resultShaders != shadersAppliedMesh) {
                // Different from mesh which means duplicate is needed
                newNifIdx = ++numMesh;

                const PGDiag::Prefix diagDupPrefix("dups", nlohmann::json::value_t::object);
                const auto numMeshStr = to_string(numMesh);
                const PGDiag::Prefix diagNumMeshPrefix(numMeshStr, nlohmann::json::value_t::object);
            }
        }

        // Add to output
        output[newNifIdx].first.insert(output[newNifIdx].first.end(), resultsToApply.begin(), resultsToApply.end());
        output[newNifIdx].second = resultShaders;

        // Add to mesh tracker
        meshTracker[serializedMesh] = newNifIdx;
    }

    return output;
}

auto ParallaxGen::getDuplicateNIFPath(const std::filesystem::path& nifPath, const int& index) -> std::filesystem::path
{
    // Different from mesh which means duplicate is needed
    filesystem::path newNIFPath;
    auto it = nifPath.begin();
    newNIFPath /= *it++ / (L"pg" + to_wstring(index));
    while (it != nifPath.end()) {
        newNIFPath /= *it++;
    }

    return newNIFPath;
}

auto ParallaxGen::populateModInfoFromNIF(const std::filesystem::path& nifPath,
    const ParallaxGenDirectory::NifCache& nifCache, const bool& patchPlugin) -> ParallaxGenTask::PGResult
{
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nullptr);

    // loop through each texture set in cache
    for (const auto& textureSet : nifCache.textureSets) {
        // find matches
        const auto matches = PatcherUtil::getMatches(textureSet.second, patcherObjects, true);

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

        if (patchPlugin) {
            ParallaxGenPlugin::processShape(nifPath.wstring(), patcherObjects, textureSet.first, true);
        }
    }

    return ParallaxGenTask::PGResult::SUCCESS;
}

auto ParallaxGen::patchNIF(const std::filesystem::path& nifPath, const bool& patchPlugin) -> ParallaxGenTask::PGResult
{
    auto* const pgd = PGGlobals::getPGD();

    const PGDiag::Prefix nifPrefix("meshes", nlohmann::json::value_t::object);
    const PGDiag::Prefix diagNIFFilePrefix(nifPath.wstring(), nlohmann::json::value_t::object);

    auto result = ParallaxGenTask::PGResult::SUCCESS;

    const Logger::Prefix prefixNIF(nifPath.wstring());
    Logger::trace(L"Starting processing");

    unordered_map<filesystem::path, NifFileResult> createdNIFs;

    // nifCache is not loaded, load it
    const vector<std::byte> nifFileData = pgd->getFile(nifPath);
    auto origNif = NIFUtil::loadNIFFromBytes(nifFileData, false);

    // Process NIF
    bool nifModified = false;
    processNIF(nifPath, origNif, patchPlugin, createdNIFs, nifModified);

    if (!nifModified) {
        return result;
    }

    for (auto& [createdNIFFile, nifParams] : createdNIFs) {
        const filesystem::path outputFile = pgd->getGeneratedPath() / createdNIFFile;
        // create directories if required
        filesystem::create_directories(outputFile.parent_path());

        // delete old nif if exists
        if (filesystem::exists(outputFile)) {
            filesystem::remove(outputFile);
        }

        if (nifParams.nifFile.Save(outputFile, { .optimize = false, .sortBlocks = false }) != 0) {
            Logger::error(L"Unable to save NIF file {}", createdNIFFile.wstring());
            result = ParallaxGenTask::PGResult::FAILURE;
            continue;
        }

        Logger::debug(L"Saving patched NIF to output");

        // Clear NIF from memory (no longer needed)
        nifParams.nifFile.Clear();

        // Find CRC before
        boost::crc_32_type crcBeforeResult {};
        crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
        const auto crcBefore = crcBeforeResult.checksum();

        // created NIF is the same filename as original so we need to write to diff file
        // Calculate CRC32 hash after
        const auto outputFileBytes = getFileBytes(outputFile);
        boost::crc_32_type crcResultAfter {};
        crcResultAfter.process_bytes(outputFileBytes.data(), outputFileBytes.size());
        const auto crcAfter = crcResultAfter.checksum();

        // Add to diff JSON
        const auto diffJSONKey = utf16toUTF8(nifPath.wstring());
        {
            const unique_lock lock(s_diffJSONMutex);
            s_diffJSON[diffJSONKey]["crc32original"] = crcBefore;
            s_diffJSON[diffJSONKey]["crc32patched"] = crcAfter;
        }

        // tell PGD that this is a generated file
        pgd->addGeneratedFile(createdNIFFile, nullptr);
        HandlerLightPlacerTracker::handleNIFCreated(nifPath, createdNIFFile);

        // assign mesh in plugins
        {
            const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
            ParallaxGenPlugin::assignMesh(createdNIFFile, nifPath, nifParams.txstResults);

            for (const auto& [oldIndex3D, newIndex3D, shapeName] : nifParams.idxCorrections) {
                // Update 3D indices in plugin
                ParallaxGenPlugin::set3DIndices(nifPath.wstring(), oldIndex3D, newIndex3D, shapeName);
            }
        }
    }

    return result;
}

auto ParallaxGen::processNIF(const std::filesystem::path& nifPath, const nifly::NifFile& origNif,
    const bool& patchPlugin, std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs, bool& nifModified,
    const std::unordered_map<int, NIFUtil::ShapeShader>* forceShaders,
    const std::unordered_map<int, NIFUtil::ShapeShader>* origShadersApplied) -> bool
{
    if (forceShaders != nullptr && origShadersApplied == nullptr) {
        throw runtime_error("Force shaders are set but origShadersApplied is null");
    }

    auto* const pgd = PGGlobals::getPGD();
    const auto modPtr = pgd->getMod(nifPath);
    if (modPtr != nullptr) {
        PGDiag::insert("mod", modPtr->name);
    }

    // Create the current working nif file, which is copied from original NIF
    NifFile nif;
    nif.CopyFrom(origNif);

    // add a nifPath entry to createdNIFs
    createdNIFs[nifPath] = {};

    // Create patcher objects
    const auto patcherObjects = createNIFPatcherObjects(nifPath, &nif);

    // Get shapes and index 3ds (this is in the order as they would show up as 3d indices in plugins)
    const auto shapes = NIFUtil::getShapesWithBlockIDs(&nif);

    // shadersAppliedMesh stores the shaders that were applied on the current mesh by shape for comparison later
    unordered_map<int, NIFUtil::ShapeShader> shadersAppliedMesh;

    // recordHandleTracker tracks records while patching shapes.
    // The key is the model record handle
    // The key of the unordered map is the OLD index 3d of the shape
    // The value of the unordered map is the TXSTResult for that current shape and model record
    // The idea is to sort results by model record because we want to check each model record later
    unordered_map<int, unordered_map<int, ParallaxGenPlugin::TXSTResult>> recordHandleTracker;

    for (const auto& [nifShape, oldIndex3D] : shapes) {
        if (nifShape == nullptr) {
            // Skip if shape is null (invalid shapes)
            continue;
        }

        // Init with UNKNOWN for shadersApplied
        shadersAppliedMesh[oldIndex3D] = NIFUtil::ShapeShader::UNKNOWN;

        if (!NIFUtil::isPatchableShape(nif, *nifShape)) {
            // Skip if not patchable shape
            continue;
        }

        // Define forced shader if needed
        const NIFUtil::ShapeShader* ptrShaderForce = nullptr;
        if (forceShaders != nullptr && forceShaders->contains(oldIndex3D)) {
            if (forceShaders->at(oldIndex3D) == NIFUtil::ShapeShader::UNKNOWN) {
                // skip unknown force shaders (invalid shapes)
                continue;
            }

            if (forceShaders->at(oldIndex3D) == origShadersApplied->at(oldIndex3D)) {
                // skip if the force shader is the same as the original shader
                continue;
            }

            ptrShaderForce = &forceShaders->at(oldIndex3D);
        }

        // get shape name and blockid
        const auto shapeBlockID = nif.GetBlockID(nifShape);
        const auto shapeName = nifShape->name.get();
        const auto shapeIDStr = to_string(shapeBlockID) + " / " + shapeName;

        const Logger::Prefix prefixShape(shapeIDStr);

        // Can apply map
        unordered_map<NIFUtil::ShapeShader, bool> canApplyMap;
        if (NIFUtil::isShaderPatchableShape(nif, *nifShape)) {
            canApplyMap = PatcherUtil::createCanApplyMap(patcherObjects, *nifShape, ptrShaderForce);
        }

        {
            const PGDiag::Prefix diagShapesPrefix("shapes", nlohmann::json::value_t::object);
            const PGDiag::Prefix diagShapeIDPrefix(shapeIDStr, nlohmann::json::value_t::object);

            nifModified |= processNIFShape(
                nifPath, &nif, nifShape, canApplyMap, patcherObjects, shadersAppliedMesh[oldIndex3D], ptrShaderForce);
        }

        // Update nifModified if shape was modified
        if (shadersAppliedMesh[oldIndex3D] != NIFUtil::ShapeShader::UNKNOWN && patchPlugin && forceShaders == nullptr) {
            const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);

            ParallaxGenPlugin::processShape(
                nifPath.wstring(), patcherObjects, oldIndex3D, false, &canApplyMap, shapeIDStr, &recordHandleTracker);
        }
    }

    if (patchPlugin && forceShaders == nullptr) {
        // Create any required duplicate NIFs
        const auto resultsToApply = getMeshesFromPluginResults(shadersAppliedMesh, shapes, recordHandleTracker);
        for (const auto& [dupIdx, results] : resultsToApply) {
            if (dupIdx == 0) {
                // This is the primary NIF
                createdNIFs[nifPath].txstResults = results.first;
                createdNIFs[nifPath].shadersAppliedMesh = results.second;

                continue;
            }

            const auto newNIFPath = getDuplicateNIFPath(nifPath, dupIdx);
            // create a duplicate nif file
            if (processNIF(
                    newNIFPath, origNif, patchPlugin, createdNIFs, nifModified, &results.second, &shadersAppliedMesh)) {
                createdNIFs[newNIFPath].txstResults = results.first;
                createdNIFs[newNIFPath].shadersAppliedMesh = results.second;
            }
        }
    }

    // Run global patchers
    {
        const PGDiag::Prefix diagGlobalPatcherPrefix("globalPatchers", nlohmann::json::value_t::object);
        for (const auto& globalPatcher : patcherObjects.globalPatchers) {
            const Logger::Prefix prefixPatches(utf8toUTF16(globalPatcher->getPatcherName()));
            const bool globalPatcherChanged = globalPatcher->applyPatch();

            PGDiag::insert(globalPatcher->getPatcherName(), globalPatcherChanged);

            nifModified |= globalPatcherChanged && globalPatcher->triggerSave();
        }
    }

    if (!nifModified && forceShaders == nullptr) {
        // No changes were made
        return {};
    }

    // Sort blocks and set plugin indices
    nif.PrettySortBlocks();

    // get newShapes
    if (patchPlugin && forceShaders == nullptr) {
        const auto newShapes = NIFUtil::getShapesWithBlockIDs(&nif);

        for (const auto& [nifShape, oldIndex3D] : shapes) {
            if (!newShapes.contains(nifShape)) {
                throw runtime_error("Shape not found in new NIF after patching");
            }

            const auto newIndex3D = newShapes.at(nifShape);

            const tuple<int, int, string> idxChange = { oldIndex3D, newIndex3D, nifShape->name.get() };
            createdNIFs[nifPath].idxCorrections.push_back(idxChange);
        }
    }

    createdNIFs[nifPath].nifFile = nif;

    return true;
}

auto ParallaxGen::processNIFShape(const std::filesystem::path& nifPath, nifly::NifFile* nif, nifly::NiShape* nifShape,
    const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply, const PatcherUtil::PatcherMeshObjectSet& patchers,
    NIFUtil::ShapeShader& shaderApplied, const NIFUtil::ShapeShader* forceShader) -> bool
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    if (nifShape == nullptr) {
        throw runtime_error("NIFShape is null");
    }

    bool changed = false;

    // Prep
    Logger::trace(L"Starting Processing");

    if (forceShader != nullptr && *forceShader == NIFUtil::ShapeShader::UNKNOWN) {
        throw runtime_error("Force shader is UNKNOWN");
    }

    // apply prepatchers
    {
        const PGDiag::Prefix diagPrePatcherPrefix("prePatchers", nlohmann::json::value_t::object);
        for (const auto& prePatcher : patchers.prePatchers) {
            const Logger::Prefix prefixPatches(prePatcher->getPatcherName());
            const bool prePatcherChanged = prePatcher->applyPatch(*nifShape);

            PGDiag::insert(prePatcher->getPatcherName(), prePatcherChanged);

            changed |= prePatcherChanged && prePatcher->triggerSave();
        }
    }

    if (NIFUtil::isShaderPatchableShape(*nif, *nifShape)) {
        const auto slots = PatcherMeshShader::getTextureSet(nifPath, *nif, *nifShape);

        PGDiag::insert("origTextures", NIFUtil::textureSetToStr(slots));

        // this shape is shader patchable so we can run shader patchers
        shaderApplied = NIFUtil::ShapeShader::NONE;

        // Allowed shaders from result of patchers
        auto matches = PatcherUtil::getMatches(slots, patchers, false);
        // remove any matches that cannot apply
        PatcherUtil::filterMatches(matches, canApply);

        if (forceShader != nullptr) {
            changed = true;
        }

        if (matches.empty() && forceShader != nullptr) {
            // Force shader means we can just apply the shader and return
            // the only case this should be executing is if an alternate texture exists
            shaderApplied = *forceShader;
            // Find correct patcher
            const auto& patcher = patchers.shaderPatchers.at(*forceShader);
            patcher->applyShader(*nifShape);
        }

        PatcherUtil::ShaderPatcherMatch winningShaderMatch;
        // Get winning match
        if (!matches.empty()) {
            winningShaderMatch = PatcherUtil::getWinningMatch(matches);

            PGDiag::insert("winningShaderMatch", winningShaderMatch.getJSON());

            // Apply transforms
            if (PatcherUtil::applyTransformIfNeeded(winningShaderMatch, patchers)) {
                PGDiag::insert("shaderTransformResult", winningShaderMatch.getJSON());
            }

            shaderApplied = winningShaderMatch.shader;
            if (shaderApplied != NIFUtil::ShapeShader::UNKNOWN) {
                // loop through patchers
                NIFUtil::TextureSet newSlots;
                changed |= patchers.shaderPatchers.at(winningShaderMatch.shader)
                               ->applyPatch(*nifShape, winningShaderMatch.match, newSlots);

                PGDiag::insert("newTextures", NIFUtil::textureSetToStr(newSlots));

                // Post warnings if any
                for (const auto& curMatchedFrom : winningShaderMatch.match.matchedFrom) {
                    ParallaxGenWarnings::mismatchWarn(
                        winningShaderMatch.match.matchedPath, newSlots.at(static_cast<int>(curMatchedFrom)));
                }

                ParallaxGenWarnings::meshWarn(winningShaderMatch.match.matchedPath, nifPath.wstring());
            }
        }
    }

    // apply postpatchers
    {
        const PGDiag::Prefix diagPostPatcherPrefix("postPatchers", nlohmann::json::value_t::object);
        for (const auto& postPatcher : patchers.postPatchers) {
            const Logger::Prefix prefixPatches(postPatcher->getPatcherName());
            const bool postPatcherChanged = postPatcher->applyPatch(*nifShape);

            PGDiag::insert(postPatcher->getPatcherName(), postPatcherChanged);

            changed |= postPatcherChanged && postPatcher->triggerSave();
        }
    }

    return changed;
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
