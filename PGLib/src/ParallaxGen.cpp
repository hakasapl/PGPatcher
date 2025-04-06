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
#include <fstream>
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
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenTask.hpp"
#include "ParallaxGenUtil.hpp"
#include "ParallaxGenWarnings.hpp"
#include "patchers/base/PatcherUtil.hpp"

using namespace std;
using namespace ParallaxGenUtil;
using namespace nifly;

ParallaxGen::ParallaxGen(
    filesystem::path outputDir, ParallaxGenDirectory* pgd, ParallaxGenD3D* pgd3D, const bool& optimizeMeshes)
    : m_outputDir(std::move(outputDir))
    , m_pgd(pgd)
    , m_pgd3D(pgd3D)
    , m_modPriority(nullptr)
{
    // constructor

    // set optimize meshes flag
    m_nifSaveOptions.optimize = optimizeMeshes;
}

void ParallaxGen::loadPatchers(
    const PatcherUtil::PatcherMeshSet& meshPatchers, const PatcherUtil::PatcherTextureSet& texPatchers)
{
    this->m_meshPatchers = meshPatchers;
    this->m_texPatchers = texPatchers;
}

void ParallaxGen::loadModPriorityMap(unordered_map<wstring, int>* modPriority)
{
    this->m_modPriority = modPriority;
    ParallaxGenPlugin::loadModPriorityMap(modPriority);
}

void ParallaxGen::patch(const bool& multiThread, const bool& patchPlugin)
{
    auto meshes = m_pgd->getMeshes();

    // Define diff JSON
    mutex diffJSONMutex;
    nlohmann::json diffJSON = nlohmann::json::object();

    // Create task tracker
    ParallaxGenTask taskTracker("Mesh Patcher", meshes.size());

    // Create runner
    ParallaxGenRunner meshRunner(multiThread);

    // Add tasks
    for (const auto& mesh : meshes) {
        meshRunner.addTask([this, &taskTracker, &diffJSONMutex, &diffJSON, &mesh, &patchPlugin] {
            taskTracker.completeJob(processNIF(mesh, &diffJSON, &diffJSONMutex, patchPlugin));
        });
    }

    // Blocks until all tasks are done
    meshRunner.runTasks();

    // Print any resulting warning
    ParallaxGenWarnings::printWarnings();

    if (!m_texPatchers.globalPatchers.empty()) {
        // texture runner
        auto textures = m_pgd->getTextures();

        // Create task tracker
        ParallaxGenTask textureTaskTracker("Texture Patcher", textures.size());

        // Create runner
        ParallaxGenRunner textureRunner(multiThread);

        // Add tasks
        for (const auto& texture : textures) {
            textureRunner.addTask(
                [this, &textureTaskTracker, &texture] { textureTaskTracker.completeJob(processDDS(texture)); });
        }

        // Blocks until all tasks are done
        textureRunner.runTasks();
    }

    // Write diffJSON file
    spdlog::info("Saving diff JSON file...");
    const filesystem::path diffJSONPath = m_outputDir / getDiffJSONName();
    ofstream diffJSONFile(diffJSONPath);
    diffJSONFile << diffJSON << "\n";
    diffJSONFile.close();
}

auto ParallaxGen::findModConflicts(const bool& multiThread, const bool& patchPlugin)
    -> unordered_map<wstring, tuple<set<NIFUtil::ShapeShader>, unordered_set<wstring>>>
{
    auto meshes = m_pgd->getMeshes();

    // Create task tracker
    ParallaxGenTask taskTracker("Finding Mod Conflicts", meshes.size(), PROGRESS_INTERVAL_CONFLICTS);

    // Define conflicts
    PatcherUtil::ConflictModResults conflictMods;

    // Create runner
    ParallaxGenRunner runner(multiThread);

    // Add tasks
    for (const auto& mesh : meshes) {
        runner.addTask([this, &taskTracker, &mesh, &patchPlugin, &conflictMods] {
            taskTracker.completeJob(processNIF(mesh, nullptr, nullptr, patchPlugin, &conflictMods));
        });
    }

    // Blocks until all tasks are done
    runner.runTasks();

    return conflictMods.mods;
}

void ParallaxGen::zipMeshes() const
{
    // Zip meshes
    spdlog::info("Zipping meshes...");
    zipDirectory(m_outputDir, m_outputDir / getOutputZipName());
}

void ParallaxGen::deleteOutputDir(const bool& preOutput) const
{
    static const unordered_set<filesystem::path> foldersToDelete = { "lightplacer", "pbrtexturesets", "strings" };
    static const unordered_set<filesystem::path> filesToDelete
        = { "parallaxgen.esp", boost::to_lower_copy(getDiffJSONName().wstring()), "parallaxgen_diag.json" };
    static const vector<pair<wstring, wstring>> filesToDeleteParseRules = { { L"pg_", L".esp" } };
    static const unordered_set<filesystem::path> foldersToIgnore = { "meshes", "textures" };
    static const unordered_set<filesystem::path> filesToIgnore = { "meta.ini" };
    static const unordered_set<filesystem::path> filesToDeletePreOutput
        = { boost::to_lower_copy(getOutputZipName().wstring()) };

    if (!filesystem::exists(m_outputDir) || !filesystem::is_directory(m_outputDir)) {
        return;
    }

    vector<filesystem::path> filesToDeleteParsed;
    for (const auto& entry : filesystem::directory_iterator(m_outputDir)) {
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
        const auto file = m_outputDir / fileToDelete;
        if (filesystem::exists(file)) {
            filesystem::remove(file);
        }
    }

    for (const auto& folderToDelete : foldersToDelete) {
        const auto folder = m_outputDir / folderToDelete;
        if (filesystem::exists(folder)) {
            filesystem::remove_all(folder);
        }
    }

    if (preOutput) {
        for (const auto& fileToDelete : filesToDeletePreOutput) {
            const auto file = m_outputDir / fileToDelete;
            if (filesystem::exists(file)) {
                filesystem::remove(file);
            }
        }
    }
}

void ParallaxGen::cleanStaleOutput()
{
    // recurse through generated directory
    static const unordered_set<filesystem::path> foldersToCheck = { "meshes", "textures" };
    for (const auto& folder : foldersToCheck) {
        const auto folderPath = m_outputDir / folder;
        if (!filesystem::exists(folderPath)) {
            continue;
        }

        // recurse through folder
        for (const auto& entry : filesystem::recursive_directory_iterator(folderPath)) {
            // find relative path to output directory
            const auto relPath = filesystem::relative(entry.path(), m_outputDir);
            if (entry.is_regular_file() && !m_pgd->isGenerated(relPath)) {
                // delete stale output file
                Logger::debug(L"Deleting stale output file: {}", entry.path().wstring());
                filesystem::remove(entry.path());
            }
        }

        // Check for any empty folders that should be deleted
        for (auto it = filesystem::recursive_directory_iterator(
                 m_outputDir, filesystem::directory_options::skip_permission_denied);
            it != filesystem::recursive_directory_iterator(); ++it) {
            const auto& entry = *it;

            if (entry.is_directory() && filesystem::is_empty(entry.path())) {
                // delete empty folder
                filesystem::remove_all(entry.path());

                // skip iterator
                it.disable_recursion_pending();
            }
        }
    }
}

auto ParallaxGen::getOutputZipName() -> filesystem::path { return "ParallaxGen_Output.zip"; }

auto ParallaxGen::getDiffJSONName() -> filesystem::path { return "ParallaxGen_Diff.json"; }

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

auto ParallaxGen::nifShouldProcess(const std::filesystem::path& nifFile, nlohmann::json& nifCache,
    std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs, const bool& patchPlugin,
    PatcherUtil::ConflictModResults* conflictMods) -> bool
{
    createdNIFs.clear();

    const auto outputFile = m_outputDir / nifFile;

    if (!PGCache::getNIFCache(nifFile, nifCache)) {
        return false;
    }

    // Cache is valid!

    if (!nifCache.contains("modified") || !nifCache["modified"].is_boolean()) {
        // no modified flag in cache, so we need to process
        return false;
    }

    // find winning match from cache data
    if (!nifCache.contains("shapes") || !nifCache["shapes"].is_object()) {
        // no shapes in cache, so we need to process
        return false;
    }

    // 1. build patchers
    auto patcherObjects = PatcherUtil::PatcherMeshObjectSet();
    for (const auto& [shader, factory] : m_meshPatchers.shaderPatchers) {
        auto patcher = factory(nifFile, nullptr);
        patcherObjects.shaderPatchers.emplace(shader, std::move(patcher));
    }
    for (const auto& [shader, factory] : m_meshPatchers.shaderTransformPatchers) {
        for (const auto& [transformShader, transformFactory] : factory) {
            auto transform = transformFactory(nifFile, nullptr);
            patcherObjects.shaderTransformPatchers[shader].emplace(transformShader, std::move(transform));
        }
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

        unordered_map<NIFUtil::ShapeShader, bool> canApplyMap;
        for (const auto& [shader, canApply] : shape["canapply"].items()) {
            if (!canApply.is_boolean()) {
                // canApply is not a boolean, so we need to process
                return false;
            }

            const auto curCanApplyShader = NIFUtil::getShaderFromStr(shader);
            if (curCanApplyShader == NIFUtil::ShapeShader::UNKNOWN) {
                // unknown shader, so we need to process
                return false;
            }

            canApplyMap[curCanApplyShader] = canApply.get<bool>();
        }

        // 4. Get slots from cache
        if (!shape.contains("slots") || !shape["slots"].is_string()) {
            // no slots in cache, so we need to process
            return false;
        }

        const auto slotsStr = shape["slots"].get<string>();
        const auto slots = NIFUtil::getTextureSlotsFromStr(slotsStr);

        // 5. Get matches
        const auto curMatches = PatcherUtil::getMatches(slots, patcherObjects, canApplyMap, conflictMods);

        // 6. Get plugin results
        if (!shape.contains("blockid") || !shape["blockid"].is_number()) {
            // no blockid in cache, so we need to process
            return false;
        }
        const auto shapeBlockID = shape["blockid"].get<int>();

        if (!shape.contains("name") || !shape["name"].is_string()) {
            // no name in cache, so we need to process
            return false;
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
                    nifFile.wstring(), canApplyMap, cacheOldIndex3D, patcherObjects, results, shapeIDStr, conflictMods);
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
                    continue;
                }

                recordHandleTracker[result.modelRecHandle][cacheOldIndex3D] = result;
            }
        }

        if (conflictMods != nullptr) {
            // all we need if we're only looking at conflictmods
            continue;
        }

        // 7. Get winning match
        if (!shape.contains("winningmatch") || !shape["winningmatch"].is_object()) {
            // no winning match in cache, so we need to process
            return false;
        }

        const auto cacheWinningMatch = PatcherUtil::ShaderPatcherMatch::fromJSON(shape["winningmatch"]);
        auto winningMatch = PatcherUtil::getWinningMatch(curMatches, m_modPriority);
        if (PatcherUtil::applyTransformIfNeeded(winningMatch, patcherObjects, true)) {
            // for now, transforms are not cached
            return false;
        }

        if (winningMatch.shader == NIFUtil::ShapeShader::UNKNOWN) {
            // no winning match in cache, so we need to process
            shadersAppliedMesh[cacheOldIndex3D] = NIFUtil::ShapeShader::NONE;
        } else {
            shadersAppliedMesh[cacheOldIndex3D] = winningMatch.shader;
        }

        if (cacheWinningMatch != winningMatch) {
            // winning match does not match
            return false;
        }

        // Post warnings if any
        for (const auto& curMatchedFrom : winningMatch.match.matchedFrom) {
            ParallaxGenWarnings::mismatchWarn(
                winningMatch.match.matchedPath, slots.at(static_cast<int>(curMatchedFrom)));
        }

        ParallaxGenWarnings::meshWarn(winningMatch.match.matchedPath, nifFile.wstring());
    }

    if (!nifCache["modified"].get<bool>()) {
        // nif was not modified but cache is valid
        createdNIFs[nifFile] = mainNifParams;
        return true;
    }

    // if NIF was modified we also need these fields

    // 8. Get meshes from plugin results

    // get meshes from plugin results
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
                if (!shapeCache.contains(to_string(oldIndex3D)) || !shapeCache[to_string(oldIndex3D)].is_object()) {
                    // oldIndex3D not in cache, so we need to process
                    return false;
                }

                const auto& curShapeCache = shapeCache[to_string(oldIndex3D)];
                if (!curShapeCache.contains("appliedshader") || !curShapeCache["appliedshader"].is_object()) {
                    // no applied shader in cache, so we need to process
                    return false;
                }

                const auto& appliedShaderCache = curShapeCache["appliedshader"];
                if (!appliedShaderCache.contains(to_string(dupIdx))
                    || !appliedShaderCache[to_string(dupIdx)].is_string()) {
                    return false;
                }

                const auto appliedShaderCacheStr = appliedShaderCache[to_string(dupIdx)].get<string>();
                const auto appliedShaderCacheShader = NIFUtil::getShaderFromStr(appliedShaderCacheStr);
                if (appliedShaderCacheShader != shaderApplied) {
                    // applied shader does not match
                    return false;
                }
            }

            // add to createdNIFs
            NifFileResult nifParams;
            nifParams.txstResults = results.first;
            nifParams.shadersAppliedMesh = results.second;
            createdNIFs[getDuplicateNIFPath(nifFile, dupIdx)] = nifParams;
        }
    }

    // cache says nif was modified and saved to output
    if (!filesystem::exists(outputFile)) {
        // Output file doesn't exist, so we need to process
        return false;
    }

    if (patchPlugin) {
        for (const auto& [old3DIndex, shape] : shapeCache.items()) {
            // shape items that are only needed if NIF was modified and cache is valid
            if (!shape.contains("newindex3d") || !shape["newindex3d"].is_number()) {
                // no newindex3d in cache, so we need to process
                return false;
            }

            if (!shape.contains("name") || !shape["name"].is_string()) {
                // no name in cache, so we need to process
                return false;
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
        return false;
    }

    if (!nifCache.contains("newCRC32") || !nifCache["newCRC32"].is_number()) {
        // no CRC in cache, so we need to process
        return false;
    }

    createdNIFs[nifFile] = mainNifParams;

    return true;
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

auto ParallaxGen::processNIF(const filesystem::path& nifFile, nlohmann::json* diffJSON, mutex* diffJSONMutex,
    const bool& patchPlugin, PatcherUtil::ConflictModResults* conflictMods) -> ParallaxGenTask::PGResult
{
    if (diffJSON != nullptr && diffJSONMutex == nullptr) {
        throw runtime_error("Diff JSON mutex must be set if diff JSON is set");
    }

    const PGDiag::Prefix nifPrefix("meshes", nlohmann::json::value_t::object);
    const PGDiag::Prefix diagNIFFilePrefix(nifFile.wstring(), nlohmann::json::value_t::object);

    auto result = ParallaxGenTask::PGResult::SUCCESS;

    const Logger::Prefix prefixNIF(nifFile.wstring());
    Logger::trace(L"Starting processing");

    unordered_map<filesystem::path, NifFileResult> createdNIFs;

    nlohmann::json nifCache;
    bool validNifCache = true;
    if (!PGCache::isCacheEnabled() || !nifShouldProcess(nifFile, nifCache, createdNIFs, patchPlugin, conflictMods)) {
        Logger::debug(L"Cache for NIF {} is invalidated or nonexistent", nifFile.wstring());
        validNifCache = false;
    }

    vector<std::byte> nifFileData;
    if (!validNifCache) {
        // Load NIF file
        try {
            nifFileData = m_pgd->getFile(nifFile);
        } catch (const exception& e) {
            Logger::trace(L"NIF Rejected: Unable to load NIF: {}", utf8toUTF16(e.what()));
            Logger::error(L"Unable to load NIF (most likely corrupt): {}", nifFile.wstring());
            result = ParallaxGenTask::PGResult::FAILURE;
            return result;
        }

        // Process NIF
        bool nifModified = false;
        processNIF(nifFile, nifFileData, createdNIFs, nifModified, nifCache, nullptr, patchPlugin, conflictMods);
        nifCache["modified"] = nifModified;
        if (!nifModified) {
            PGCache::setNIFCache(nifFile, nifCache);
            return result;
        }
    }

    if (validNifCache && !nifCache["modified"].get<bool>()) {
        // NIF is not modified, so we can skip
        return result;
    }

    const auto oldNifMTime = m_pgd->getFileMTime(nifFile);

    for (auto& [createdNIFFile, nifParams] : createdNIFs) {
        const bool needsDiff = createdNIFFile == nifFile;

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

        const filesystem::path outputFile = m_outputDir / createdNIFFile;
        if (!validNifCache) {
            // create directories if required
            filesystem::create_directories(outputFile.parent_path());

            // delete old nif if exists
            if (filesystem::exists(outputFile)) {
                filesystem::remove(outputFile);
            }

            if (nifParams.nifFile.Save(outputFile, m_nifSaveOptions) != 0) {
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
            if (diffJSON != nullptr) {
                auto jsonKey = utf16toUTF8(nifFile.wstring());
                threadSafeJSONUpdate(
                    [&](nlohmann::json& json) {
                        json[jsonKey]["crc32original"] = crcBefore;
                        json[jsonKey]["crc32patched"] = crcAfter;
                    },
                    *diffJSON, *diffJSONMutex);
            }
        }

        // tell PGD that this is a generated file
        m_pgd->addGeneratedFile(createdNIFFile, m_pgd->getMod(nifFile));

        // assign mesh in plugin
        {
            const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
            ParallaxGenPlugin::assignMesh(createdNIFFile, nifFile, nifParams.txstResults);

            for (const auto& [oldIndex3D, newIndex3D, shapeName] : nifParams.idxCorrections) {
                // Update 3D indices in plugin
                ParallaxGenPlugin::set3DIndices(nifFile.wstring(), oldIndex3D, newIndex3D, shapeName);
            }
        }
    }

    // Save NIF cache
    if (!validNifCache) {
        PGCache::setNIFCache(nifFile, nifCache, oldNifMTime);
    }

    return result;
}

auto ParallaxGen::processNIF(const std::filesystem::path& nifFile, const vector<std::byte>& nifBytes,
    std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs, bool& nifModified, nlohmann::json& nifCache,
    const unordered_map<int, NIFUtil::ShapeShader>* forceShaders, const bool& patchPlugin,
    PatcherUtil::ConflictModResults* conflictMods) -> bool
{
    PGDiag::insert("mod", m_pgd->getMod(nifFile));

    NifFile nif;
    try {
        nif = NIFUtil::loadNIFFromBytes(nifBytes);
    } catch (const exception& e) {
        Logger::error(L"Failed to load NIF (most likely corrupt) {}: {}", nifFile.wstring(), utf8toUTF16(e.what()));
        return false;
    }

    createdNIFs[nifFile] = {};

    nifModified = false;

    // Create patcher objects
    auto patcherObjects = PatcherUtil::PatcherMeshObjectSet();
    for (const auto& factory : m_meshPatchers.prePatchers) {
        auto patcher = factory(nifFile, &nif);
        patcherObjects.prePatchers.emplace_back(std::move(patcher));
    }
    for (const auto& [shader, factory] : m_meshPatchers.shaderPatchers) {
        auto patcher = factory(nifFile, &nif);
        patcherObjects.shaderPatchers.emplace(shader, std::move(patcher));
    }
    for (const auto& [shader, factory] : m_meshPatchers.shaderTransformPatchers) {
        for (const auto& [transformShader, transformFactory] : factory) {
            auto transform = transformFactory(nifFile, &nif);
            patcherObjects.shaderTransformPatchers[shader].emplace(transformShader, std::move(transform));
        }
    }
    for (const auto& factory : m_meshPatchers.postPatchers) {
        auto patcher = factory(nifFile, &nif);
        patcherObjects.postPatchers.emplace_back(std::move(patcher));
    }
    for (const auto& factory : m_meshPatchers.globalPatchers) {
        auto patcher = factory(nifFile, &nif);
        patcherObjects.globalPatchers.emplace_back(std::move(patcher));
    }

    // shapes and 3d indices
    const auto shapes = NIFUtil::getShapesWithBlockIDs(&nif);

    // shadersAppliedMesh stores the shaders that were applied on the current mesh by shape for comparison later
    unordered_map<int, NIFUtil::ShapeShader> shadersAppliedMesh;

    // recordHandleTracker tracks records while patching shapes.
    // The key is the model record handle
    // The first of the pair is a vector of shaders applied to each shape based on what is in the plugin
    // the second of the pair is a vector of plugin results for each shape
    unordered_map<int, unordered_map<int, ParallaxGenPlugin::TXSTResult>> recordHandleTracker;

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

            if (conflictMods == nullptr && forceShaders == nullptr) {
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

                if (conflictMods == nullptr && forceShaders == nullptr) {
                    // Add to cache
                    nifCache["shapes"][to_string(oldIndex3D)]["canapply"][NIFUtil::getStrFromShader(shader)]
                        = canApplyMap[shader];
                }
            }

            nifModified |= processShape(nifFile, nif, nifShape, nifCache["shapes"][to_string(oldIndex3D)], canApplyMap,
                patcherObjects, shadersAppliedMesh[oldIndex3D], conflictMods, ptrShaderForce);
        }

        // Update nifModified if shape was modified
        if (shadersAppliedMesh[oldIndex3D] != NIFUtil::ShapeShader::UNKNOWN && patchPlugin && forceShaders == nullptr) {
            // Get all plugin results
            vector<ParallaxGenPlugin::TXSTResult> results;
            {
                const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
                ParallaxGenPlugin::processShape(
                    nifFile.wstring(), canApplyMap, oldIndex3D, patcherObjects, results, shapeIDStr, conflictMods);
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

    if (conflictMods != nullptr) {
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
                const auto newNIFPath = getDuplicateNIFPath(nifFile, dupIdx);
                // create a duplicate nif file
                if (processNIF(
                        newNIFPath, nifBytes, createdNIFs, nifModified, nifCache, &results.second, false, nullptr)) {
                    createdNIFs[newNIFPath].txstResults = results.first;
                    createdNIFs[newNIFPath].shadersAppliedMesh = results.second;
                }

                for (const auto& [oldIndex3D, shader] : results.second) {
                    nifCache["shapes"][to_string(oldIndex3D)]["appliedshader"][to_string(dupIdx)]
                        = NIFUtil::getStrFromShader(shader);
                }

                continue;
            }

            createdNIFs[nifFile].txstResults = results.first;
            createdNIFs[nifFile].shadersAppliedMesh = results.second;
        }
    }

    // Run global patchers
    {
        const PGDiag::Prefix diagGlobalPatcherPrefix("globalPatchers", nlohmann::json::value_t::object);
        for (const auto& globalPatcher : patcherObjects.globalPatchers) {
            const Logger::Prefix prefixPatches(utf8toUTF16(globalPatcher->getPatcherName()));
            bool globalPatcherChanged = false;
            globalPatcherChanged = globalPatcher->applyPatch();

            PGDiag::insert(globalPatcher->getPatcherName(), globalPatcherChanged);

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

            if (conflictMods == nullptr) {
                nifCache["shapes"][to_string(oldIndex3D)]["newindex3d"] = newIndex3D;
                nifCache["shapes"][to_string(oldIndex3D)]["name"] = nifShape->name.get();
            }

            const tuple<int, int, string> idxChange = { oldIndex3D, newIndex3D, nifShape->name.get() };
            createdNIFs[nifFile].idxCorrections.push_back(idxChange);
        }
    }

    createdNIFs[nifFile].nifFile = nif;

    return true;
}

auto ParallaxGen::processShape(const filesystem::path& nifPath, NifFile& nif, NiShape* nifShape,
    nlohmann::json& shapeCache, const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply,
    PatcherUtil::PatcherMeshObjectSet& patchers, NIFUtil::ShapeShader& shaderApplied,
    PatcherUtil::ConflictModResults* conflictMods, const NIFUtil::ShapeShader* forceShader) -> bool
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
        const PGDiag::Prefix diagPrePatcherPrefix("prePatchers", nlohmann::json::value_t::object);
        for (const auto& prePatcher : patchers.prePatchers) {
            const Logger::Prefix prefixPatches(prePatcher->getPatcherName());
            const bool prePatcherChanged = prePatcher->applyPatch(*nifShape);

            PGDiag::insert(prePatcher->getPatcherName(), prePatcherChanged);

            changed |= prePatcherChanged && prePatcher->triggerSave();
        }
    }

    shaderApplied = NIFUtil::ShapeShader::NONE;

    // Allowed shaders from result of patchers
    auto matches = PatcherUtil::getMatches(slots, patchers, canApply, conflictMods);
    if (conflictMods != nullptr) {
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

    if (matches.empty() && forceShader != nullptr) {
        // Force shader means we can just apply the shader and return
        // the only case this should be executing is if an alternate texture exists
        shaderApplied = *forceShader;
        // Find correct patcher
        const auto& patcher = patchers.shaderPatchers.at(*forceShader);
        patcher->applyShader(*nifShape);
        changed = true;
    }

    PatcherUtil::ShaderPatcherMatch winningShaderMatch;
    // Get winning match
    if (!matches.empty()) {
        winningShaderMatch = PatcherUtil::getWinningMatch(matches, m_modPriority);

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

auto ParallaxGen::processDDS(const filesystem::path& ddsFile) -> ParallaxGenTask::PGResult
{
    auto result = ParallaxGenTask::PGResult::SUCCESS;

    const PGDiag::Prefix nifPrefix("textures", nlohmann::json::value_t::object);

    // Prep
    Logger::trace(L"Starting Processing");

    // only allow DDS files
    const string ddsFileExt = ddsFile.extension().string();
    if (ddsFileExt != ".dds") {
        throw runtime_error("File is not a DDS file");
    }

    DirectX::ScratchImage ddsImage;
    if (!m_pgd3D->getDDS(ddsFile, ddsImage)) {
        Logger::error(L"Unable to load DDS file: {}", ddsFile.wstring());
        return ParallaxGenTask::PGResult::FAILURE;
    }

    bool ddsModified = false;

    for (const auto& factory : m_texPatchers.globalPatchers) {
        auto patcher = factory(ddsFile, &ddsImage);
        patcher->applyPatch(ddsModified);
    }

    if (ddsModified) {
        // save to output
        const filesystem::path outputFile = m_outputDir / ddsFile;
        filesystem::create_directories(outputFile.parent_path());

        const HRESULT hr = DirectX::SaveToDDSFile(ddsImage.GetImages(), ddsImage.GetImageCount(),
            ddsImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
        if (FAILED(hr)) {
            Logger::error(L"Unable to save DDS {}: {}", outputFile.wstring(),
                ParallaxGenUtil::utf8toUTF16(ParallaxGenD3D::getHRESULTErrorMessage(hr)));
            return ParallaxGenTask::PGResult::FAILURE;
        }

        // Update file map with generated file
        m_pgd->addGeneratedFile(ddsFile, m_pgd->getMod(ddsFile));
    }

    return result;
}

void ParallaxGen::threadSafeJSONUpdate(
    const std::function<void(nlohmann::json&)>& operation, nlohmann::json& j, mutex& mutex)
{
    const std::lock_guard<std::mutex> lock(mutex);
    operation(j);
}

void ParallaxGen::addFileToZip(
    mz_zip_archive& zip, const filesystem::path& filePath, const filesystem::path& zipPath) const
{
    // ignore Zip file itself
    if (filePath == zipPath) {
        return;
    }

    vector<std::byte> buffer = getFileBytes(filePath);

    const filesystem::path relativePath = filePath.lexically_relative(m_outputDir);
    const string relativeFilePathAscii = utf16toASCII(relativePath.wstring());

    // add file to Zip
    if (mz_zip_writer_add_mem(&zip, relativeFilePathAscii.c_str(), buffer.data(), buffer.size(), MZ_NO_COMPRESSION)
        == 0) {
        spdlog::error(L"Error adding file to zip: {}", filePath.wstring());
        exit(1);
    }
}

void ParallaxGen::zipDirectory(const filesystem::path& dirPath, const filesystem::path& zipPath) const
{
    mz_zip_archive zip;

    // init to 0
    memset(&zip, 0, sizeof(zip));

    // Check if file already exists and delete
    if (filesystem::exists(zipPath)) {
        spdlog::info(L"Deleting existing output Zip file: {}", zipPath.wstring());
        filesystem::remove(zipPath);
    }

    // initialize file
    const string zipPathString = utf16toUTF8(zipPath);
    if (mz_zip_writer_init_file(&zip, zipPathString.c_str(), 0) == 0) {
        spdlog::critical(L"Error creating Zip file: {}", zipPath.wstring());
        exit(1);
    }

    // add each file in directory to Zip
    for (const auto& entry : filesystem::recursive_directory_iterator(dirPath)) {
        if (filesystem::is_regular_file(entry.path())) {
            addFileToZip(zip, entry.path(), zipPath);
        }
    }

    // finalize Zip
    if (mz_zip_writer_finalize_archive(&zip) == 0) {
        spdlog::critical(L"Error finalizing Zip archive: {}", zipPath.wstring());
        exit(1);
    }

    mz_zip_writer_end(&zip);

    spdlog::info(L"Please import this file into your mod manager: {}", zipPath.wstring());
}
