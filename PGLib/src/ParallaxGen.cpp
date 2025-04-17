#include "ParallaxGen.hpp"

#include <DirectXTex.h>
#include <Geometry.hpp>
#include <NifFile.hpp>
#include <algorithm>
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
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <d3d11.h>

#include "Logger.hpp"
#include "NIFUtil.hpp"
#include "PGCache.hpp"
#include "PGDiag.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenTask.hpp"
#include "ParallaxGenUtil.hpp"
#include "ParallaxGenWarnings.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherUtil.hpp"

using namespace std;
using namespace ParallaxGenUtil;
using namespace nifly;

// statics
PatcherUtil::PatcherMeshSet ParallaxGen::s_meshPatchers;
PatcherUtil::PatcherTextureSet ParallaxGen::s_texPatchers;

std::mutex ParallaxGen::s_diffJSONMutex;
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

    //
    // MESH PATCHING
    //

    // Add tasks
    auto meshes = pgd->getMeshes();

    // Create task tracker
    ParallaxGenTask taskTracker("Mesh Patcher", meshes.size());

    // Create runner
    ParallaxGenRunner meshRunner(multiThread);

    for (const auto& mesh : meshes) {
        meshRunner.addTask(
            [&taskTracker, &mesh, &patchPlugin] { taskTracker.completeJob(patchNIF(mesh, patchPlugin, false)); });
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
    for (const auto& mesh : meshes) {
        runner.addTask(
            [&taskTracker, &mesh, &patchPlugin] { taskTracker.completeJob(patchNIF(mesh, patchPlugin, true)); });
    }

    // Blocks until all tasks are done
    runner.runTasks();
}

void ParallaxGen::deleteOutputDir(const bool& preOutput)
{
    static const unordered_set<filesystem::path> foldersToDelete = { "lightplacer", "pbrtexturesets", "strings" };
    static const unordered_set<filesystem::path> filesToDelete
        = { "parallaxgen.esp", "parallaxgen_diff.json", "parallaxgen_diag.json" };
    static const vector<pair<wstring, wstring>> filesToDeleteParseRules = { { L"pg_", L".esp" } };
    static const unordered_set<filesystem::path> foldersToIgnore = { "meshes", "textures" };
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

        if (entry.is_directory()
            && (foldersToDelete.contains(entryFilename) || foldersToIgnore.contains(entryFilename))) {
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

void ParallaxGen::cleanStaleOutput()
{
    auto* const pgd = PGGlobals::getPGD();

    // recurse through generated directory
    static const unordered_set<filesystem::path> foldersToCheck = { "meshes", "textures" };

    const auto outputDir = pgd->getGeneratedPath();

    for (const auto& folder : foldersToCheck) {
        const auto folderPath = outputDir / folder;
        if (!filesystem::exists(folderPath)) {
            continue;
        }

        // recurse through folder
        for (const auto& entry : filesystem::recursive_directory_iterator(folderPath)) {
            // find relative path to output directory
            const auto relPath = filesystem::relative(entry.path(), outputDir);
            if (entry.is_regular_file() && !pgd->isGenerated(relPath)) {
                // delete stale output file
                Logger::debug(L"Deleting stale output file: {}", entry.path().wstring());
                filesystem::remove(entry.path());
            }
        }

        // Delete any empty folders
        std::vector<filesystem::path> directories;
        for (const auto& entry : filesystem::recursive_directory_iterator(outputDir)) {
            if (filesystem::is_directory(entry.path())) {
                directories.push_back(entry.path());
            }
        }

        std::ranges::sort(directories, [&](auto const& a, auto const& b) {
            // Convert both paths to something relative to 'root'
            const filesystem::path relA = filesystem::relative(a, outputDir);
            const filesystem::path relB = filesystem::relative(b, outputDir);
            // Compare the number of path components
            return distance(a.begin(), a.end()) > distance(b.begin(), b.end());
        });

        // Now remove empty directories in descending depth order
        for (auto const& dir : directories) {
            if (filesystem::exists(dir) && filesystem::is_empty(dir)) {
                filesystem::remove(dir);
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
    const lock_guard<std::mutex> lock(s_diffJSONMutex);
    return s_diffJSON;
}

auto ParallaxGen::getMeshesFromPluginResults(const std::unordered_map<int, NIFUtil::ShapeShader>& shadersAppliedMesh,
    const std::vector<int>& shapeIdxs,
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
        for (const auto& oldIndex3D : shapeIdxs) {
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
        output[newNifIdx].first = resultsToApply;
        output[newNifIdx].second = resultShaders;

        // Add to mesh tracker
        meshTracker[serializedMesh] = newNifIdx;
    }

    return output;
}

auto ParallaxGen::shouldProcessNIF(const std::filesystem::path& nifPath, const bool& patchPlugin, const bool& dryRun,
    nlohmann::json& nifCache, std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs) -> bool
{
    createdNIFs.clear();

    if (!PGCache::getNIFCache(nifPath, nifCache)) {
        Logger::trace(L"Cache Invalid: File cache invalidated or does not exist");
        return true;
    }

    // Cache is valid!

    if (!nifCache.contains("modified") || !nifCache["modified"].is_boolean()) {
        // no modified flag in cache, which means the cache is not complete
        Logger::trace(L"Cache Invalid: No modified field in cache");
        return true;
    }

    // find winning match from cache data
    if (!nifCache.contains("shapes") || !nifCache["shapes"].is_object()) {
        // no shapes in cache, so we need to process
        throw runtime_error("Cache Corrupt: No shapes field in cache");
    }

    // check globalpatchers
    if (!nifCache.contains("globalpatchers") || !nifCache["globalpatchers"].is_array()) {
        // no globalpatchers in cache, so we need to process
        throw runtime_error(
            "Cache Corrupt: No globalpatchers field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
    }

    // build patchers
    const auto patcherObjects = createNIFPatcherObjects(nifPath, nullptr);

    // create a unordered set of globalpatcher strings
    unordered_set<string> globalPatchersLoaded;
    for (const auto& globalPatcher : patcherObjects.globalPatchers) {
        globalPatchersLoaded.insert(globalPatcher->getPatcherName());
    }

    unordered_set<string> globalPatchersCache;
    for (const auto& globalPatcher : nifCache["globalpatchers"]) {
        if (!globalPatcher.is_string()) {
            // globalPatcher is not a string, so we need to process
            throw runtime_error(
                "Cache Corrupt: globalpatcher is not a string for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        globalPatchersCache.insert(globalPatcher.get<string>());
    }

    if (globalPatchersLoaded != globalPatchersCache) {
        // globalpatchers do not match, so we need to process
        Logger::trace(L"Cache Invalid: globalpatchers do not match");
        return true;
    }

    // 2. Loop through each shape in NIF
    unordered_map<int, NIFUtil::ShapeShader> shadersAppliedMesh;
    unordered_map<int, unordered_map<int, ParallaxGenPlugin::TXSTResult>> recordHandleTracker;
    vector<int> shapeIdxs;

    NifFileResult mainNifParams;

    const auto& shapeCache = nifCache["shapes"];
    for (const auto& [old3DIndex, shape] : shapeCache.items()) {
        // 3. Create canApply map
        if (!shape.contains("canapply") || !shape["canapply"].is_object()) {
            // no canApply map which means shape was not processed
            continue;
        }

        // check prepatchers
        if (!shape.contains("prepatchers") || !shape["prepatchers"].is_array()) {
            // no prepatchers in cache, so we need to process
            throw runtime_error(
                "Cache Corrupt: No prepatchers field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        // create a unordered set of prepatcher strings
        unordered_set<string> prePatchersLoaded;
        for (const auto& prePatcher : patcherObjects.prePatchers) {
            prePatchersLoaded.insert(prePatcher->getPatcherName());
        }

        unordered_set<string> prePatchersCache;
        for (const auto& prePatcher : shape["prepatchers"]) {
            if (!prePatcher.is_string()) {
                // prePatcher is not a string, so we need to process
                throw runtime_error(
                    "Cache Corrupt: prepatcher is not a string for NIF " + utf16toUTF8(nifPath.wstring()));
            }

            prePatchersCache.insert(prePatcher.get<string>());
        }

        if (prePatchersLoaded != prePatchersCache) {
            // prepatchers do not match, so we need to process
            Logger::trace(L"Cache Invalid: prepatchers do not match");
            return true;
        }

        // check postpatchers
        if (!shape.contains("postpatchers") || !shape["postpatchers"].is_array()) {
            // no postpatchers in cache, so we need to process
            throw runtime_error(
                "Cache Corrupt: No postpatchers field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        // create a unordered set of postpatcher strings
        unordered_set<string> postPatchersLoaded;
        for (const auto& postPatcher : patcherObjects.postPatchers) {
            postPatchersLoaded.insert(postPatcher->getPatcherName());
        }

        unordered_set<string> postPatchersCache;
        for (const auto& postPatcher : shape["postpatchers"]) {
            if (!postPatcher.is_string()) {
                // postPatcher is not a string, so we need to process
                throw runtime_error(
                    "Cache Corrupt: postpatcher is not a string for NIF " + utf16toUTF8(nifPath.wstring()));
            }

            postPatchersCache.insert(postPatcher.get<string>());
        }

        if (postPatchersLoaded != postPatchersCache) {
            // postpatchers do not match, so we need to process
            Logger::trace(L"Cache Invalid: postpatchers do not match");
            return true;
        }

        unordered_map<NIFUtil::ShapeShader, bool> canApplyMap;
        for (const auto& [shader, canApply] : shape["canapply"].items()) {
            if (!canApply.is_boolean()) {
                // canApply is not a boolean, so we need to process
                throw runtime_error(
                    "Cache Corrupt: canapply is not a boolean for NIF " + utf16toUTF8(nifPath.wstring()));
            }

            const auto curCanApplyShader = NIFUtil::getShaderFromStr(shader);
            if (curCanApplyShader == NIFUtil::ShapeShader::UNKNOWN) {
                // unknown shader, so we need to process
                throw runtime_error(
                    "Cache Corrupt: canapply shader is unknown for NIF " + utf16toUTF8(nifPath.wstring()));
            }

            canApplyMap[curCanApplyShader] = canApply.get<bool>();
        }

        // 4. Get slots from cache
        if (!shape.contains("slots") || !shape["slots"].is_string()) {
            // no slots in cache, so we need to process
            throw runtime_error("Cache Corrupt: No slots field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        const auto slotsStr = shape["slots"].get<string>();
        const auto slots = NIFUtil::getTextureSlotsFromStr(slotsStr);

        // 5. Get matches
        const auto curMatches = PatcherUtil::getMatches(slots, patcherObjects, canApplyMap, dryRun);

        // 6. Get plugin results
        if (!shape.contains("blockid") || !shape["blockid"].is_number()) {
            // no blockid in cache, so we need to process
            throw runtime_error("Cache Corrupt: No blockid field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }
        const auto shapeBlockID = shape["blockid"].get<int>();

        if (!shape.contains("name") || !shape["name"].is_string()) {
            // no name in cache, so we need to process
            throw runtime_error("Cache Corrupt: No name field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }
        const auto shapeName = shape["name"].get<string>();
        const auto shapeIDStr = to_string(shapeBlockID) + " / " + shapeName;

        const auto cacheOldIndex3D = atoi(old3DIndex.c_str());
        shapeIdxs.push_back(cacheOldIndex3D);

        if (patchPlugin) {
            // Get all plugin results
            vector<ParallaxGenPlugin::TXSTResult> results;
            {
                const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
                ParallaxGenPlugin::processShape(
                    nifPath.wstring(), dryRun, canApplyMap, patcherObjects, cacheOldIndex3D, shapeIDStr, results);
            }

            // Loop through results
            for (const auto& result : results) {
                if (!recordHandleTracker.contains(result.modelRecHandle)) {
                    // Create new entry for modelRecHandle
                    recordHandleTracker[result.modelRecHandle] = {};
                }

                if (recordHandleTracker.contains(result.modelRecHandle)
                    && recordHandleTracker[result.modelRecHandle].contains(cacheOldIndex3D)) {
                    // Duplicate result (isn't supposed to happen, issue with a plugin)
                    // TODO log warning
                    continue;
                }

                recordHandleTracker[result.modelRecHandle][cacheOldIndex3D] = result;
            }
        }

        if (dryRun) {
            // all we need if we're only looking at conflictmods
            continue;
        }

        // 7. Get winning match
        if (!shape.contains("winningmatch") || !shape["winningmatch"].is_object()) {
            // no winning match in cache, so we need to process
            throw runtime_error(
                "Cache Corrupt: No winningmatch field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        const auto cacheWinningMatch = PatcherUtil::ShaderPatcherMatch::fromJSON(shape["winningmatch"]);
        auto winningMatch = PatcherUtil::getWinningMatch(curMatches);

        // Check winning match mtime
        if (!shape.contains("winningmatchmtime") || !shape["winningmatchmtime"].is_number()) {
            // no winning match mtime in cache, so we need to process
            throw runtime_error(
                "Cache Corrupt: No winningmatchmtime field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
        }

        const auto cacheWinningMatchMTime = shape["winningmatchmtime"].get<size_t>();
        size_t winningMatchMTime = 0;
        if (!winningMatch.match.matchedPath.empty()) {
            winningMatchMTime = PGGlobals::getPGD()->getFileMTime(winningMatch.match.matchedPath);
        }

        if (cacheWinningMatchMTime != winningMatchMTime) {
            // winning match mtime does not match
            Logger::trace(L"Cache Invalid: winning match mtime does not match");
            return true;
        }

        PatcherUtil::applyTransformIfNeeded(winningMatch, patcherObjects);

        if (winningMatch.shader == NIFUtil::ShapeShader::UNKNOWN) {
            shadersAppliedMesh[cacheOldIndex3D] = NIFUtil::ShapeShader::NONE;
        } else {
            shadersAppliedMesh[cacheOldIndex3D] = winningMatch.shader;
        }

        if (cacheWinningMatch != winningMatch) {
            // winning match does not match
            Logger::trace(L"Cache Invalid: winning match does not match");
            return true;
        }

        // Post warnings if any
        for (const auto& curMatchedFrom : winningMatch.match.matchedFrom) {
            ParallaxGenWarnings::mismatchWarn(
                winningMatch.match.matchedPath, slots.at(static_cast<int>(curMatchedFrom)));
        }

        ParallaxGenWarnings::meshWarn(winningMatch.match.matchedPath, nifPath.wstring());
    }

    if (dryRun) {
        // all we need if we're only looking at conflictmods
        return false;
    }

    // Duplicate meshes and plugin results
    if (patchPlugin) {
        const auto resultsToApply = getMeshesFromPluginResults(shadersAppliedMesh, shapeIdxs, recordHandleTracker);
        for (const auto& [dupIdx, results] : resultsToApply) {
            if (dupIdx == 0) {
                // no duplicate mesh, so we can skip
                mainNifParams.txstResults = results.first;
                mainNifParams.shadersAppliedMesh = results.second;
                continue;
            }

            const auto curShadersApplied = results.second;
            for (const auto& [oldIndex3D, shaderApplied] : curShadersApplied) {
                if (!shapeCache.contains(to_string(oldIndex3D))) {
                    Logger::trace(L"Cache Invalid: oldIndex3D not in cache");
                    return true;
                }

                if (!shapeCache[to_string(oldIndex3D)].is_object()) {
                    // oldIndex3D not in cache, so we need to process
                    throw runtime_error(
                        "Cache Corrupt: oldIndex3D not in cache for NIF " + utf16toUTF8(nifPath.wstring()));
                }

                const auto& curShapeCache = shapeCache[to_string(oldIndex3D)];
                if (!curShapeCache.contains("appliedshader") || !curShapeCache["appliedshader"].is_object()) {
                    // no applied shader in cache, so we need to process
                    throw runtime_error(
                        "Cache Corrupt: No appliedshader field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
                }

                const auto& appliedShaderCache = curShapeCache["appliedshader"];
                if (!appliedShaderCache.contains(to_string(dupIdx))) {
                    Logger::trace(L"Cache Invalid: dupIdx not in applied shader");
                    return true;
                }

                if (!appliedShaderCache[to_string(dupIdx)].is_string()) {
                    throw runtime_error(
                        "Cache Corrupt: appliedshader is not a string for NIF " + utf16toUTF8(nifPath.wstring()));
                }

                const auto appliedShaderCacheStr = appliedShaderCache[to_string(dupIdx)].get<string>();
                const auto appliedShaderCacheShader = NIFUtil::getShaderFromStr(appliedShaderCacheStr);
                if (appliedShaderCacheShader != shaderApplied) {
                    // applied shader does not match
                    Logger::trace(L"Cache Invalid: applied shader does not match for duplicate");
                    return true;
                }
            }

            // add to createdNIFs
            NifFileResult nifParams;
            nifParams.txstResults = results.first;
            nifParams.shadersAppliedMesh = results.second;
            createdNIFs[getDuplicateNIFPath(nifPath, dupIdx)] = nifParams;
        }

        // loop through cache to see if there is any dupIdx that is NOT in resultsToApply
        for (const auto& [old3DIndex, shape] : shapeCache.items()) {
            if (!shape.contains("appliedshader") || !shape["appliedshader"].is_object()) {
                // no appliedshader in this shape so we just continue
                continue;
            }

            for (const auto& [dupIdx, appliedShader] : shape["appliedshader"].items()) {
                if (!appliedShader.is_string()) {
                    // applied shader is not a string, so we need to process
                    throw runtime_error(
                        "Cache Corrupt: appliedshader is not a string for NIF " + utf16toUTF8(nifPath.wstring()));
                }

                const auto dupIdxInt = atoi(dupIdx.c_str());
                if (!resultsToApply.contains(dupIdxInt)) {
                    // dupIdx not in resultsToApply, so we need to process
                    Logger::trace(L"Cache Invalid: dupIdx not in resultsToApply");
                    return true;
                }
            }
        }
    }

    const auto outputDir = PGGlobals::getPGD()->getGeneratedPath();
    const auto outputFile = outputDir / nifPath;

    // cache says nif was modified and saved to output
    if (!filesystem::exists(outputFile) && nifCache["modified"].get<bool>()) {
        // Output file doesn't exist, so we need to process
        Logger::trace(L"Cache Invalid: Output file doesn't exist");
        return true;
    }

    if (patchPlugin) {
        for (const auto& [old3DIndex, shape] : shapeCache.items()) {
            // shape items that are only needed if NIF was modified and cache is valid
            if (!shape.contains("newindex3d") || !shape["newindex3d"].is_number()) {
                // no newindex3d in cache, so we continue
                continue;
            }

            if (!shape.contains("name") || !shape["name"].is_string()) {
                // no name in cache, so we need to process
                throw runtime_error("Cache Corrupt: No name field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
            }

            const auto cacheOldIndex3D = atoi(old3DIndex.c_str());
            const auto cacheNewIndex3D = shape["newindex3d"].get<int>();
            const auto shapeName = shape["name"].get<string>();
            const tuple<int, int, string> idxCorrection = { cacheOldIndex3D, cacheNewIndex3D, shapeName };
            mainNifParams.idxCorrections.push_back(idxCorrection);
        }
    }

    // check for CRC info
    if (!nifCache.contains("oldCRC32") || !nifCache["oldCRC32"].is_number()) {
        // no CRC in cache, so we need to process
        throw runtime_error("Cache Corrupt: No oldCRC32 field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
    }

    if (!nifCache.contains("newCRC32") || !nifCache["newCRC32"].is_number()) {
        // no CRC in cache, so we need to process
        throw runtime_error("Cache Corrupt: No newCRC32 field in cache for NIF " + utf16toUTF8(nifPath.wstring()));
    }

    // We add the mainNifParams
    createdNIFs[nifPath] = mainNifParams;

    return false;
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

auto ParallaxGen::patchNIF(const std::filesystem::path& nifPath, const bool& patchPlugin, const bool& dryRun)
    -> ParallaxGenTask::PGResult
{
    auto* const pgd = PGGlobals::getPGD();

    const PGDiag::Prefix nifPrefix("meshes", nlohmann::json::value_t::object);
    const PGDiag::Prefix diagNIFFilePrefix(nifPath.wstring(), nlohmann::json::value_t::object);

    auto result = ParallaxGenTask::PGResult::SUCCESS;

    const Logger::Prefix prefixNIF(nifPath.wstring());
    Logger::trace(L"Starting processing");

    unordered_map<filesystem::path, NifFileResult> createdNIFs;

    nlohmann::json nifCache;
    bool validNifCache = true;
    if (!PGCache::isCacheEnabled() || shouldProcessNIF(nifPath, patchPlugin, dryRun, nifCache, createdNIFs)) {
        Logger::debug(L"Cache for NIF {} is invalidated or nonexistent: {}", nifPath.wstring());
        createdNIFs.clear();
        validNifCache = false;
    }

    // We keep nifFileData in memory to calculate CRC32 later if required
    vector<std::byte> nifFileData;
    bool nifModified = false;
    if (validNifCache) {
        nifModified = nifCache["modified"].get<bool>();
    } else {
        // Load NIF file
        try {
            nifFileData = pgd->getFile(nifPath);
        } catch (const exception& e) {
            Logger::error(L"Failed to load NIF (most likely corrupt) {}: {}", nifPath.wstring(), utf8toUTF16(e.what()));
            return ParallaxGenTask::PGResult::FAILURE;
        }

        // Process NIF
        processNIF(nifPath, nifFileData, patchPlugin, dryRun, nifCache, createdNIFs, nifModified);

        if (!dryRun) {
            nifCache["modified"] = nifModified;
        }
    }

    if (dryRun) {
        // we're done
        return result;
    }

    const auto oldNifMTime = pgd->getFileMTime(nifPath);

    for (auto& [createdNIFFile, nifParams] : createdNIFs) {
        const bool needsDiff = createdNIFFile == nifPath;

        unsigned int crcBefore = 0;
        if (needsDiff) {
            if (validNifCache) {
                crcBefore = nifCache["oldCRC32"].get<unsigned int>();
            } else {
                // created NIF is the same filename as original so we need to write to diff file
                // Calculate CRC32 hash before
                boost::crc_32_type crcBeforeResult {};
                crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
                crcBefore = crcBeforeResult.checksum();

                nifCache["oldCRC32"] = crcBefore;
            }
        }

        const filesystem::path outputFile = pgd->getGeneratedPath() / createdNIFFile;
        if (!validNifCache && nifModified) {
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
        }

        if (needsDiff) {
            unsigned int crcAfter = 0;
            if (validNifCache) {
                crcAfter = nifCache["newCRC32"].get<unsigned int>();
            } else {
                // created NIF is the same filename as original so we need to write to diff file
                // Calculate CRC32 hash after
                const auto outputFileBytes = getFileBytes(outputFile);
                boost::crc_32_type crcResultAfter {};
                crcResultAfter.process_bytes(outputFileBytes.data(), outputFileBytes.size());
                crcAfter = crcResultAfter.checksum();

                nifCache["newCRC32"] = crcAfter;
            }

            // Add to diff JSON
            const auto diffJSONKey = utf16toUTF8(nifPath.wstring());
            {
                const lock_guard<mutex> lock(s_diffJSONMutex);
                s_diffJSON[diffJSONKey]["crc32original"] = crcBefore;
                s_diffJSON[diffJSONKey]["crc32patched"] = crcAfter;
            }
        }

        if (nifModified) {
            // tell PGD that this is a generated file
            pgd->addGeneratedFile(createdNIFFile, nullptr);
        }

        // assign mesh in plugin
        {
            const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
            ParallaxGenPlugin::assignMesh(createdNIFFile, nifPath, nifParams.txstResults);

            for (const auto& [oldIndex3D, newIndex3D, shapeName] : nifParams.idxCorrections) {
                // Update 3D indices in plugin
                ParallaxGenPlugin::set3DIndices(nifPath.wstring(), oldIndex3D, newIndex3D, shapeName);
            }
        }
    }

    // Save NIF cache
    if (!validNifCache) {
        PGCache::setNIFCache(nifPath, nifCache, oldNifMTime);
    }

    return result;
}

auto ParallaxGen::processNIF(const std::filesystem::path& nifPath, const std::vector<std::byte>& nifBytes,
    const bool& patchPlugin, const bool& dryRun, nlohmann::json& nifCache,
    std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs, bool& nifModified,
    const std::unordered_map<int, NIFUtil::ShapeShader>* forceShaders) -> bool
{
    auto* const pgd = PGGlobals::getPGD();
    PGDiag::insert("mod", pgd->getMod(nifPath)->name);

    // Load NIF file
    NifFile nif;
    try {
        nif = NIFUtil::loadNIFFromBytes(nifBytes);
    } catch (const exception& e) {
        Logger::error(L"Failed to load NIF (most likely corrupt) {}: {}", nifPath.wstring(), utf8toUTF16(e.what()));
        return false;
    }

    createdNIFs[nifPath] = {};

    nifModified = false;

    // Create patcher objects
    const auto patcherObjects = createNIFPatcherObjects(nifPath, &nif);

    // shapes and 3d indices
    const auto shapes = NIFUtil::getShapesWithBlockIDs(&nif);

    // shadersAppliedMesh stores the shaders that were applied on the current mesh by shape for comparison later
    unordered_map<int, NIFUtil::ShapeShader> shadersAppliedMesh;

    // recordHandleTracker tracks records while patching shapes.
    // The key is the model record handle
    // The first of the pair is a vector of shaders applied to each shape based on what is in the plugin
    // the second of the pair is a vector of plugin results for each shape
    unordered_map<int, unordered_map<int, ParallaxGenPlugin::TXSTResult>> recordHandleTracker;

    if (!dryRun && forceShaders == nullptr) {
        // reset shapes cache
        nifCache["shapes"] = nlohmann::json::object();
    }

    unordered_set<int> foundShapeIDxs;
    for (const auto& [nifShape, oldIndex3D] : shapes) {
        // Define forced shader if needed
        const NIFUtil::ShapeShader* ptrShaderForce = nullptr;
        if (forceShaders != nullptr && forceShaders->contains(oldIndex3D)) {
            if (forceShaders->at(oldIndex3D) == NIFUtil::ShapeShader::UNKNOWN) {
                // skip unknown force shaders (invalid shapes)
                continue;
            }
            ptrShaderForce = &forceShaders->at(oldIndex3D);
        }

        // get shape name and blockid
        const auto shapeBlockID = nif.GetBlockID(nifShape);
        const auto shapeName = nifShape->name.get();
        const auto shapeIDStr = to_string(shapeBlockID) + " / " + shapeName;
        const string shapeBlockName = nifShape->GetBlockName();

        const Logger::Prefix prefixShape(shapeIDStr);
        unordered_map<NIFUtil::ShapeShader, bool> canApplyMap;
        shadersAppliedMesh[oldIndex3D] = NIFUtil::ShapeShader::UNKNOWN;

        {
            const PGDiag::Prefix diagShapesPrefix("shapes", nlohmann::json::value_t::object);
            const PGDiag::Prefix diagShapeIDPrefix(shapeIDStr, nlohmann::json::value_t::object);

            // Check if shape should be patched or not
            if (shapeBlockName != "NiTriShape" && shapeBlockName != "BSTriShape" && shapeBlockName != "BSLODTriShape"
                && shapeBlockName != "BSMeshLODTriShape") {
                PGDiag::insert("rejectReason", "Incorrect shape block type: " + shapeName);
                continue;
            }

            // get NIFShader type
            if (!nifShape->HasShaderProperty()) {
                PGDiag::insert("rejectReason", "No NIFShader property");
                continue;
            }

            // get NIFShader from shape
            NiShader* nifShader = nif.GetShader(nifShape);
            if (nifShader == nullptr) {
                PGDiag::insert("rejectReason", "No NIFShader block");
                continue;
            }

            // check that NIFShader is a BSLightingShaderProperty
            const string nifShaderName = nifShader->GetBlockName();
            if (nifShaderName != "BSLightingShaderProperty") {
                PGDiag::insert("rejectReason", "Incorrect NIFShader block type: " + nifShaderName);
                continue;
            }

            // check that NIFShader has a texture set
            if (!nifShader->HasTextureSet()) {
                PGDiag::insert("rejectReason", "No texture set");
                continue;
            }

            foundShapeIDxs.insert(oldIndex3D);

            if (!dryRun && forceShaders == nullptr) {
                nifCache["shapes"][to_string(oldIndex3D)]["canapply"] = nlohmann::json::object();
                nifCache["shapes"][to_string(oldIndex3D)]["blockid"] = shapeBlockID;
                nifCache["shapes"][to_string(oldIndex3D)]["name"] = shapeName;
                if (patchPlugin) {
                    nifCache["shapes"][to_string(oldIndex3D)]["appliedshader"] = nlohmann::json::object();
                }
            }

            // Create canapply map
            for (const auto& [shader, patcher] : patcherObjects.shaderPatchers) {
                // Check if shader can be applied
                canApplyMap[shader] = patcher->canApply(*nifShape);

                if (!dryRun && forceShaders == nullptr) {
                    // Add to cache
                    nifCache["shapes"][to_string(oldIndex3D)]["canapply"][NIFUtil::getStrFromShader(shader)]
                        = canApplyMap[shader];
                }
            }

            nifModified |= processNIFShape(nifPath, nif, nifShape, dryRun, nifCache["shapes"][to_string(oldIndex3D)],
                canApplyMap, patcherObjects, shadersAppliedMesh[oldIndex3D], ptrShaderForce);
        }

        // Update nifModified if shape was modified
        if (shadersAppliedMesh[oldIndex3D] != NIFUtil::ShapeShader::UNKNOWN && patchPlugin && forceShaders == nullptr) {
            // Get all plugin results
            vector<ParallaxGenPlugin::TXSTResult> results;
            {
                const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
                ParallaxGenPlugin::processShape(
                    nifPath.wstring(), dryRun, canApplyMap, patcherObjects, oldIndex3D, shapeIDStr, results);
            }

            // Loop through results
            for (const auto& result : results) {
                if (!recordHandleTracker.contains(result.modelRecHandle)) {
                    // Create new entry for modelRecHandle
                    recordHandleTracker[result.modelRecHandle] = {};
                }

                if (recordHandleTracker.contains(result.modelRecHandle)
                    && recordHandleTracker[result.modelRecHandle].contains(oldIndex3D)) {
                    // Duplicate result (isn't supposed to happen, issue with a plugin)
                    continue;
                }

                recordHandleTracker[result.modelRecHandle][oldIndex3D] = result;
            }
        }
    }

    if (dryRun) {
        // no need to continue if just getting mod conflicts
        nifModified = false;
        return false;
    }

    if (patchPlugin && forceShaders == nullptr) {
        // create ShapeIdxs vector
        vector<int> shapeIdxs;
        shapeIdxs.reserve(shapes.size());
        for (const auto& [nifShape, oldIndex3D] : shapes) {
            shapeIdxs.push_back(oldIndex3D);
        }

        // Create any required duplicate NIFs
        const auto resultsToApply = getMeshesFromPluginResults(shadersAppliedMesh, shapeIdxs, recordHandleTracker);
        for (const auto& [dupIdx, results] : resultsToApply) {
            if (dupIdx > 0) {
                const auto newNIFPath = getDuplicateNIFPath(nifPath, dupIdx);
                // create a duplicate nif file
                if (processNIF(newNIFPath, nifBytes, patchPlugin, dryRun, nifCache, createdNIFs, nifModified,
                        &results.second)) {
                    createdNIFs[newNIFPath].txstResults = results.first;
                    createdNIFs[newNIFPath].shadersAppliedMesh = results.second;
                }

                for (const auto& [oldIndex3D, shader] : results.second) {
                    nifCache["shapes"][to_string(oldIndex3D)]["appliedshader"][to_string(dupIdx)]
                        = NIFUtil::getStrFromShader(shader);
                }

                continue;
            }

            createdNIFs[nifPath].txstResults = results.first;
            createdNIFs[nifPath].shadersAppliedMesh = results.second;
        }
    }

    // Run global patchers
    {
        const PGDiag::Prefix diagGlobalPatcherPrefix("globalPatchers", nlohmann::json::value_t::object);
        if (!dryRun && forceShaders == nullptr) {
            nifCache["globalpatchers"] = nlohmann::json::array();
        }
        for (const auto& globalPatcher : patcherObjects.globalPatchers) {
            const Logger::Prefix prefixPatches(utf8toUTF16(globalPatcher->getPatcherName()));
            bool globalPatcherChanged = false;
            globalPatcherChanged = globalPatcher->applyPatch();

            PGDiag::insert(globalPatcher->getPatcherName(), globalPatcherChanged);
            if (!dryRun && forceShaders == nullptr) {
                nifCache["globalpatchers"].push_back(globalPatcher->getPatcherName());
            }

            nifModified |= globalPatcherChanged && globalPatcher->triggerSave();
        }
    }

    if (!nifModified && forceShaders == nullptr) {
        // No changes were made
        return {};
    }

    // Delete unreferenced blocks
    nif.DeleteUnreferencedBlocks();

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

            if (!dryRun) {
                nifCache["shapes"][to_string(oldIndex3D)]["newindex3d"] = newIndex3D;
                nifCache["shapes"][to_string(oldIndex3D)]["name"] = nifShape->name.get();
            }

            const tuple<int, int, string> idxChange = { oldIndex3D, newIndex3D, nifShape->name.get() };
            createdNIFs[nifPath].idxCorrections.push_back(idxChange);
        }
    }

    createdNIFs[nifPath].nifFile = nif;

    return true;
}

auto ParallaxGen::processNIFShape(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape* nifShape,
    const bool& dryRun, nlohmann::json& shapeCache, const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply,
    const PatcherUtil::PatcherMeshObjectSet& patchers, NIFUtil::ShapeShader& shaderApplied,
    const NIFUtil::ShapeShader* forceShader) -> bool
{
    bool changed = false;

    // Prep
    Logger::trace(L"Starting Processing");

    if (forceShader != nullptr && *forceShader == NIFUtil::ShapeShader::UNKNOWN) {
        throw runtime_error("Force shader is UNKNOWN");
    }

    const auto slots = NIFUtil::getTextureSlots(&nif, nifShape);
    shapeCache["slots"] = NIFUtil::getStrFromTextureSlots(slots);

    PGDiag::insert("origTextures", NIFUtil::textureSetToStr(slots));

    // apply prepatchers
    {
        if (!dryRun && forceShader == nullptr) {
            shapeCache["prepatchers"] = nlohmann::json::array();
        }
        const PGDiag::Prefix diagPrePatcherPrefix("prePatchers", nlohmann::json::value_t::object);
        for (const auto& prePatcher : patchers.prePatchers) {
            const Logger::Prefix prefixPatches(prePatcher->getPatcherName());
            const bool prePatcherChanged = prePatcher->applyPatch(*nifShape);

            PGDiag::insert(prePatcher->getPatcherName(), prePatcherChanged);
            if (!dryRun && forceShader == nullptr) {
                shapeCache["prepatchers"].push_back(prePatcher->getPatcherName());
            }

            changed |= prePatcherChanged && prePatcher->triggerSave();
        }
    }

    shaderApplied = NIFUtil::ShapeShader::NONE;

    // Allowed shaders from result of patchers
    auto matches = PatcherUtil::getMatches(slots, patchers, canApply, dryRun);
    if (dryRun) {
        return true;
    }

    // if forceshader is set, remove any matches that cannot be applied
    if (!matches.empty() && forceShader != nullptr) {
        for (auto it = matches.begin(); it != matches.end();) {
            if (it->shader != *forceShader && it->shaderTransformTo != *forceShader) {
                it = matches.erase(it);
            } else {
                ++it;
            }
        }
    }

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
    shapeCache["winningmatchmtime"] = 0;
    // Get winning match
    if (!matches.empty()) {
        winningShaderMatch = PatcherUtil::getWinningMatch(matches);
        if (!winningShaderMatch.match.matchedPath.empty()) {
            // we do this before transform because resulting file may not exist
            shapeCache["winningmatchmtime"] = PGGlobals::getPGD()->getFileMTime(winningShaderMatch.match.matchedPath);
        }

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

    if (forceShader == nullptr) {
        shapeCache["winningmatch"] = winningShaderMatch.getJSON();
    }

    // apply postpatchers
    {
        if (forceShader == nullptr) {
            shapeCache["postpatchers"] = nlohmann::json::array();
        }
        const PGDiag::Prefix diagPostPatcherPrefix("postPatchers", nlohmann::json::value_t::object);
        for (const auto& postPatcher : patchers.postPatchers) {
            const Logger::Prefix prefixPatches(postPatcher->getPatcherName());
            const bool postPatcherChanged = postPatcher->applyPatch(*nifShape);

            PGDiag::insert(postPatcher->getPatcherName(), postPatcherChanged);
            if (forceShader == nullptr) {
                shapeCache["postpatchers"].push_back(postPatcher->getPatcherName());
            }

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
        for (const auto& [transformShader, transformFactory] : factory) {
            auto transform = transformFactory(nifPath, nif);
            patcherObjects.shaderTransformPatchers[shader].emplace(transformShader, std::move(transform));
        }
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

auto ParallaxGen::shouldProcessDDS(const std::filesystem::path& ddsPath, nlohmann::json& ddsCache,
    std::unordered_set<std::filesystem::path>& createdDDS) -> bool
{
    // pull cache
    if (!PGCache::getTEXCache(ddsPath, ddsCache)) {
        Logger::trace(L"Cache Invalid: File cache invalidated or does not exist");
        return true;
    }

    // check global patchers
    if (!ddsCache.contains("globalpatchers") || !ddsCache["globalpatchers"].is_array()) {
        // no globalpatchers in cache, so we need to process
        throw runtime_error(
            "Cache Corrupt: No globalpatchers field in cache for DDS " + utf16toUTF8(ddsPath.wstring()));
    }

    // create patcher objects
    auto patcherObjects = createDDSPatcherObjects(ddsPath, nullptr);

    // create a unordered set of globalpatcher strings
    unordered_set<string> globalPatchersLoaded;
    for (const auto& globalPatcher : patcherObjects.globalPatchers) {
        globalPatchersLoaded.insert(globalPatcher->getPatcherName());
    }

    unordered_set<string> globalPatchersCache;
    for (const auto& globalPatcher : ddsCache["globalpatchers"]) {
        if (!globalPatcher.is_string()) {
            // globalPatcher is not a string, so we need to process
            throw runtime_error("Cache Corrupt: globalpatcher item is not a string " + utf16toUTF8(ddsPath.wstring()));
        }

        globalPatchersCache.insert(globalPatcher.get<string>());
    }

    if (globalPatchersLoaded != globalPatchersCache) {
        // globalpatchers do not match, so we need to process
        Logger::trace(L"Cache Invalid: globalpatchers do not match");
        return true;
    }

    // Check hook patchers
    if (!ddsCache.contains("hookpatchers") || !ddsCache["hookpatchers"].is_array()) {
        // no hookpatchers in cache, so we need to process
        throw runtime_error("Cache Corrupt: No hookpatchers field in cache for DDS " + utf16toUTF8(ddsPath.wstring()));
    }

    static const auto hookpatcherConvertToCMName = PatcherTextureHookConvertToCM(ddsPath, nullptr).getPatcherName();
    const bool hasConvertToCMHookPatcher
        = ParallaxGenUtil::checkIfStringInJSONArray(ddsCache["hookpatchers"], hookpatcherConvertToCMName);
    const bool hasConvertToCMHookPatcherLoaded = PatcherTextureHookConvertToCM::isInProcessList(ddsPath);
    if ((hasConvertToCMHookPatcher && !hasConvertToCMHookPatcherLoaded)
        || (!hasConvertToCMHookPatcher && hasConvertToCMHookPatcherLoaded)) {
        // hookpatcher is not in cache, so we need to process
        Logger::trace(L"Cache Invalid: hookpatcherConvertToCM does not match");
        return true;
    }

    static const auto hookpatcherFixSSSName = PatcherTextureHookFixSSS(ddsPath, nullptr).getPatcherName();
    const bool hasFixSSSHookPatcher
        = ParallaxGenUtil::checkIfStringInJSONArray(ddsCache["hookpatchers"], hookpatcherFixSSSName);
    const bool hasFixSSSHookPatcherLoaded = PatcherTextureHookFixSSS::isInProcessList(ddsPath);
    if ((hasFixSSSHookPatcher && !hasFixSSSHookPatcherLoaded)
        || (!hasFixSSSHookPatcher && hasFixSSSHookPatcherLoaded)) {
        // hookpatcher is not in cache, so we need to process
        Logger::trace(L"Cache Invalid: hookpatcherFixSSS does not match");
        return true; // NOLINT(readability-simplify-boolean-expr)
    }

    const auto genPath = PGGlobals::getPGD()->getGeneratedPath();

    // check if output file exists
    if (hasConvertToCMHookPatcher) {
        const auto outputFile = PatcherTextureHookConvertToCM::getOutputFilename(ddsPath);
        if (!filesystem::exists(genPath / outputFile)) {
            // output file does not exist, so we need to process
            Logger::trace(L"Cache Invalid: hookpatcherConvertToCM output file does not exist");
            return true;
        }

        createdDDS.insert(outputFile);
    }

    if (hasFixSSSHookPatcher) {
        const auto outputFile = PatcherTextureHookFixSSS::getOutputFilename(ddsPath);
        if (!filesystem::exists(genPath / outputFile)) {
            // output file does not exist, so we need to process
            Logger::trace(L"Cache Invalid: hookpatcherFixSSS output file does not exist");
            return true;
        }

        createdDDS.insert(outputFile);
    }

    if (!ddsCache.contains("modified") || !ddsCache["modified"].is_boolean()) {
        // no modified in cache, so we need to process
        throw runtime_error("Cache Corrupt: No modified field in cache for DDS " + utf16toUTF8(ddsPath.wstring()));
    }

    if (ddsCache["modified"].get<bool>()) {
        // modified in cache, so we should add it to the createdDDS set
        createdDDS.insert(ddsPath);
    }

    return false;
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

    // Get cache
    nlohmann::json ddsCache;
    unordered_set<filesystem::path> createdDDS;
    if (PGCache::isCacheEnabled() && !shouldProcessDDS(ddsPath, ddsCache, createdDDS)) {
        // Cache is valid, so we can skip
        for (const auto& createdDDSPath : createdDDS) {
            // tell PGD that this is a generated file
            pgd->addGeneratedFile(createdDDSPath, nullptr);
        }
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
    ddsCache["hookpatchers"] = nlohmann::json::array();
    if (PatcherTextureHookConvertToCM::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookConvertToCM(ddsPath, &ddsImage);
        patcher.applyPatch();

        ddsCache["hookpatchers"].push_back(patcher.getPatcherName());
    }
    if (PatcherTextureHookFixSSS::isInProcessList(ddsPath)) {
        auto patcher = PatcherTextureHookFixSSS(ddsPath, &ddsImage);
        patcher.applyPatch();

        ddsCache["hookpatchers"].push_back(patcher.getPatcherName());
    }

    bool ddsModified = false;

    const auto patcherObjects = createDDSPatcherObjects(ddsPath, &ddsImage);

    // global patchers
    ddsCache["globalpatchers"] = nlohmann::json::array();
    for (const auto& patcher : patcherObjects.globalPatchers) {
        patcher->applyPatch(ddsModified);
        ddsCache["globalpatchers"].push_back(patcher->getPatcherName());
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

    // save cache
    if (PGCache::isCacheEnabled()) {
        // save cache
        ddsCache["modified"] = ddsModified;
        PGCache::setTEXCache(ddsPath, ddsCache);
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
