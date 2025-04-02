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
#include "PGDiag.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenTask.hpp"
#include "ParallaxGenUtil.hpp"
#include "ParallaxGenWarnings.hpp"

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
    static const unordered_set<filesystem::path> foldersToDelete
        = { "meshes", "textures", "lightplacer", "pbrtexturesets", "strings" };
    static const unordered_set<filesystem::path> filesToDelete
        = { "parallaxgen.esp", boost::to_lower_copy(getDiffJSONName().wstring()), "parallaxgen_diag.json" };
    static const vector<pair<wstring, wstring>> filesToDeleteParseRules = { { L"pg_", L".esp" } };
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

        if (entry.is_directory() && foldersToDelete.contains(entryFilename)) {
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

auto ParallaxGen::getOutputZipName() -> filesystem::path { return "ParallaxGen_Output.zip"; }

auto ParallaxGen::getDiffJSONName() -> filesystem::path { return "ParallaxGen_Diff.json"; }

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

    // Determine output path for patched NIF
    const filesystem::path outputFile = m_outputDir / nifFile;
    if (filesystem::exists(outputFile)) {
        Logger::error(L"NIF Rejected: File already exists");
        result = ParallaxGenTask::PGResult::FAILURE;
        return result;
    }

    // Load NIF file
    vector<std::byte> nifFileData;
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
    vector<pair<filesystem::path, nifly::NifFile>> dupNIFs;

    auto nif = processNIF(nifFile, nifFileData, nifModified, nullptr, &dupNIFs, patchPlugin, conflictMods);

    // Save patched NIF if it was modified
    if (nifModified && conflictMods == nullptr && nif.IsValid()) {
        // Calculate CRC32 hash before
        boost::crc_32_type crcBeforeResult {};
        crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
        const auto crcBefore = crcBeforeResult.checksum();

        // create directories if required
        filesystem::create_directories(outputFile.parent_path());

        if (nif.Save(outputFile, m_nifSaveOptions) != 0) {
            Logger::error(L"Unable to save NIF file");
            result = ParallaxGenTask::PGResult::FAILURE;
            return result;
        }

        Logger::debug(L"Saving patched NIF to output");

        // Clear NIF from memory (no longer needed)
        nif.Clear();

        // Calculate CRC32 hash after
        const auto outputFileBytes = getFileBytes(outputFile);
        boost::crc_32_type crcResultAfter {};
        crcResultAfter.process_bytes(outputFileBytes.data(), outputFileBytes.size());
        const auto crcAfter = crcResultAfter.checksum();

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

    // Save any duplicate NIFs
    for (auto& [dupNIFFile, dupNIF] : dupNIFs) {
        const auto dupNIFPath = m_outputDir / dupNIFFile;
        filesystem::create_directories(dupNIFPath.parent_path());
        // TODO do we need to add info about this to diff json?
        Logger::debug(L"Saving duplicate NIF to output: {}", dupNIFPath.wstring());
        if (dupNIF.Save(dupNIFPath, m_nifSaveOptions) != 0) {
            Logger::error(L"Unable to save duplicate NIF file {}", dupNIFFile.wstring());
            result = ParallaxGenTask::PGResult::FAILURE;
            return result;
        }
    }

    return result;
}

auto ParallaxGen::processNIF(const std::filesystem::path& nifFile, const vector<std::byte>& nifBytes, bool& nifModified,
    const unordered_map<int, NIFUtil::ShapeShader>* forceShaders,
    vector<pair<filesystem::path, nifly::NifFile>>* dupNIFs, const bool& patchPlugin,
    PatcherUtil::ConflictModResults* conflictMods) -> nifly::NifFile
{
    if (patchPlugin && dupNIFs == nullptr) {
        // duplicating nifs is required for plugin patching
        throw runtime_error("DupNIFs must be set if patchPlugin is true");
    }

    PGDiag::insert("mod", m_pgd->getMod(nifFile));

    NifFile nif;
    try {
        nif = NIFUtil::loadNIFFromBytes(nifBytes);
    } catch (const exception& e) {
        Logger::trace(L"NIF Rejected: Unable to load NIF: {}", utf8toUTF16(e.what()));
        Logger::error(L"Unable to load NIF (most likely corrupt): {}", nifFile.wstring());
        return {};
    }

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

    // Loop through each shape in NIF
    for (const auto& [nifShape, oldIndex3D] : shapes) {
        if (nifShape == nullptr) {
            // Null nif shape (this shouldn't happen unless there is a corruption)
            spdlog::error(L"NIF {} has a null shape (skipping)", nifFile.wstring());
            nifModified = false;
            return {};
        }

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
        const Logger::Prefix prefixShape(shapeIDStr);

        // Check for any non-ascii chars
        for (uint32_t slot = 0; slot < NUM_TEXTURE_SLOTS; slot++) {
            string texture;
            nif.GetTextureSlot(nifShape, texture, slot);

            if (!containsOnlyAscii(texture)) {
                // NIFs cannot have non-ascii chars in their texture slots
                spdlog::error(L"NIF {} has texture slot(s) with invalid non-ASCII chars (skipping)", nifFile.wstring());
                nifModified = false;
                return {};
            }
        }

        NIFUtil::ShapeShader shaderApplied = NIFUtil::ShapeShader::UNKNOWN;

        {
            const PGDiag::Prefix diagShapesPrefix("shapes", nlohmann::json::value_t::object);
            const PGDiag::Prefix diagShapeIDPrefix(shapeIDStr, nlohmann::json::value_t::object);
            nifModified |= processShape(
                nifFile, nif, nifShape, oldIndex3D, patcherObjects, shaderApplied, conflictMods, ptrShaderForce);
        }

        shadersAppliedMesh[oldIndex3D] = shaderApplied;

        // Update nifModified if shape was modified
        if (shaderApplied != NIFUtil::ShapeShader::UNKNOWN && patchPlugin && forceShaders == nullptr) {
            // Get all plugin results
            vector<ParallaxGenPlugin::TXSTResult> results;
            {
                const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
                ParallaxGenPlugin::processShape(
                    nifFile.wstring(), nifShape, oldIndex3D, patcherObjects, results, shapeIDStr, conflictMods);
            }

            // Loop through results
            for (const auto& result : results) {
                if (!recordHandleTracker.contains(result.modelRecHandle)) {
                    // Create new entry for modelRecHandle
                    recordHandleTracker[result.modelRecHandle] = {};
                }

                if (recordHandleTracker.contains(result.modelRecHandle)
                    && recordHandleTracker[result.modelRecHandle].contains(oldIndex3D)) {
                    // Duplicate result (isn't supposed to happen)
                    throw runtime_error("Duplicate result for modelRecHandle and oldIndex3D");
                }

                recordHandleTracker[result.modelRecHandle][oldIndex3D] = result;
            }
        }
    }

    if (conflictMods != nullptr) {
        // no need to continue if just getting mod conflicts
        nifModified = false;
        return {};
    }

    if (patchPlugin && forceShaders == nullptr) {
        unordered_map<string, wstring> meshTracker;

        // Apply plugin results
        size_t numMesh = 0;
        for (const auto& [modelRecHandle, results] : recordHandleTracker) {
            // Store results that should apply
            vector<ParallaxGenPlugin::TXSTResult> resultsToApply;

            // store the serialization of shaders applied to meshes to keep track of what already exists
            string serializedMesh;

            // make a result shaders map which is the shaders needed for this specific plugin MODL record on the mesh
            unordered_map<int, NIFUtil::ShapeShader> resultShaders;
            for (const auto& [nifShape, oldIndex3D] : shapes) {
                NIFUtil::ShapeShader shaderApplied = NIFUtil::ShapeShader::UNKNOWN;
                if (results.contains(oldIndex3D)) {
                    // plugin has a result
                    shaderApplied = results.at(oldIndex3D).shader;
                    resultsToApply.push_back(results.at(oldIndex3D));
                }

                if (shaderApplied == NIFUtil::ShapeShader::UNKNOWN) {
                    // Set shader in nif shape
                    shaderApplied = shadersAppliedMesh[oldIndex3D];
                }

                resultShaders[oldIndex3D] = shaderApplied;
                serializedMesh += to_string(oldIndex3D) + "/" + to_string(static_cast<int>(shaderApplied)) + ",";
            }

            wstring newNIFName;
            if (meshTracker.contains(serializedMesh)) {
                // duplicate mesh already exists, we only need to assign mesh
                newNIFName = meshTracker[serializedMesh];
            } else {
                if (resultShaders == shadersAppliedMesh) {
                    // current NIF can be used as the shaders are the same
                    newNIFName = nifFile.wstring();
                } else {
                    // Different from mesh which means duplicate is needed
                    filesystem::path newNIFPath;
                    auto it = nifFile.begin();
                    newNIFPath /= *it++ / (L"pg" + to_wstring(++numMesh));
                    while (it != nifFile.end()) {
                        newNIFPath /= *it++;
                    }

                    const PGDiag::Prefix diagDupPrefix("dups", nlohmann::json::value_t::object);
                    const auto numMeshStr = to_string(numMesh);
                    const PGDiag::Prefix diagNumMeshPrefix(numMeshStr, nlohmann::json::value_t::object);

                    newNIFName = newNIFPath.wstring();
                    bool dupnifModified = false;
                    auto dupNIF
                        = processNIF(newNIFName, nifBytes, dupnifModified, &resultShaders, nullptr, false, nullptr);
                    dupNIFs->emplace_back(newNIFName, dupNIF);
                }
            }

            // assign mesh in plugin
            {
                const PGDiag::Prefix diagPluginPrefix("plugins", nlohmann::json::value_t::object);
                ParallaxGenPlugin::assignMesh(newNIFName, nifFile, resultsToApply);
            }

            // Add to mesh tracker
            meshTracker[serializedMesh] = newNIFName;
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
            ParallaxGenPlugin::set3DIndices(nifFile.wstring(), oldIndex3D, newIndex3D, nifShape->name.get());
        }
    }

    return nif;
}

auto ParallaxGen::processShape(const filesystem::path& nifPath, NifFile& nif, NiShape* nifShape, const int& shapeIndex,
    PatcherUtil::PatcherMeshObjectSet& patchers, NIFUtil::ShapeShader& shaderApplied,
    PatcherUtil::ConflictModResults* conflictMods, const NIFUtil::ShapeShader* forceShader) -> bool
{
    bool changed = false;

    // Prep
    Logger::trace(L"Starting Processing");

    if (forceShader != nullptr && *forceShader == NIFUtil::ShapeShader::UNKNOWN) {
        throw runtime_error("Force shader is UNKNOWN");
    }

    // Check for exclusions
    // only allow BSLightingShaderProperty blocks
    const string nifShapeName = nifShape->GetBlockName();
    if (nifShapeName != "NiTriShape" && nifShapeName != "BSTriShape" && nifShapeName != "BSLODTriShape"
        && nifShapeName != "BSMeshLODTriShape") {

        PGDiag::insert("rejectReason", "Incorrect shape block type: " + nifShapeName);
        return false;
    }

    // get NIFShader type
    if (!nifShape->HasShaderProperty()) {
        PGDiag::insert("rejectReason", "No NIFShader property");
        return false;
    }

    // get NIFShader from shape
    NiShader* nifShader = nif.GetShader(nifShape);
    if (nifShader == nullptr) {
        PGDiag::insert("rejectReason", "No NIFShader block");
        return false;
    }

    // check that NIFShader is a BSLightingShaderProperty
    const string nifShaderName = nifShader->GetBlockName();
    if (nifShaderName != "BSLightingShaderProperty") {
        PGDiag::insert("rejectReason", "Incorrect NIFShader block type: " + nifShaderName);
        return false;
    }

    // check that NIFShader has a texture set
    if (!nifShader->HasTextureSet()) {
        PGDiag::insert("rejectReason", "No texture set");
        return false;
    }

    PGDiag::insert("origTextures", NIFUtil::textureSetToStr(NIFUtil::getTextureSlots(&nif, nifShape)));

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

    // Create cache key for lookup
    bool cacheExists = false;
    const ParallaxGen::ShapeKey cacheKey = { .nifPath = nifPath, .shapeIndex = shapeIndex };

    // Allowed shaders from result of patchers
    vector<PatcherUtil::ShaderPatcherMatch> matches;

    // Restore cache if exists
    {
        const lock_guard<mutex> lock(m_allowedShadersCacheMutex);

        // Check if shape has already been processed
        if (m_allowedShadersCache.find(cacheKey) != m_allowedShadersCache.end()) {
            cacheExists = true;
            matches = m_allowedShadersCache.at(cacheKey);
        }
    }

    // Loop through each shader patcher if cache does not exist
    unordered_set<wstring> modSet;
    if (!cacheExists) {
        for (const auto& [shader, patcher] : patchers.shaderPatchers) {
            if (shader == NIFUtil::ShapeShader::NONE) {
                // TEMPORARILY disable default patcher
                continue;
            }

            const Logger::Prefix prefixPatches(patcher->getPatcherName());

            // Check if shader should be applied
            vector<PatcherMeshShader::PatcherMatch> curMatches;
            if (!patcher->shouldApply(*nifShape, curMatches)) {
                Logger::trace(L"Rejecting: Shader not applicable");
                continue;
            }

            for (const auto& match : curMatches) {
                PatcherUtil::ShaderPatcherMatch curMatch;
                curMatch.mod = m_pgd->getMod(match.matchedPath);
                curMatch.shader = shader;
                curMatch.match = match;
                curMatch.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

                // See if transform is possible
                if (patchers.shaderTransformPatchers.contains(shader)) {
                    const auto& availableTransforms = patchers.shaderTransformPatchers.at(shader);
                    // loop from highest element of map to 0
                    for (const auto& availableTransform : ranges::reverse_view(availableTransforms)) {
                        if (patchers.shaderPatchers.at(availableTransform.first)->canApply(*nifShape)) {
                            // Found a transform that can apply, set the transform in the match
                            curMatch.shaderTransformTo = availableTransform.first;
                            break;
                        }
                    }
                }

                // Add to matches if shader can apply (or if transform shader exists and can apply)
                if (patcher->canApply(*nifShape) || curMatch.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
                    matches.push_back(curMatch);
                    modSet.insert(curMatch.mod);
                }
            }
        }

        {
            // write to cache
            const lock_guard<mutex> lock(m_allowedShadersCacheMutex);
            m_allowedShadersCache[cacheKey] = matches;
        }
    }

    // Populate conflict mods if set
    if (conflictMods != nullptr && !matches.empty()) {
        if (modSet.size() > 1) {
            const lock_guard<mutex> lock(conflictMods->mutex);

            // add mods to conflict set
            for (const auto& match : matches) {
                if (conflictMods->mods.find(match.mod) == conflictMods->mods.end()) {
                    conflictMods->mods.insert({ match.mod, { set<NIFUtil::ShapeShader>(), unordered_set<wstring>() } });
                }

                get<0>(conflictMods->mods[match.mod]).insert(match.shader);
                get<1>(conflictMods->mods[match.mod]).insert(modSet.begin(), modSet.end());
            }
        }

        return false;
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

    // Populate diag JSON if set with each match
    {
        const PGDiag::Prefix diagShaderPatcherPrefix("shaderPatcherMatches", nlohmann::json::value_t::array);
        for (const auto& match : matches) {
            PGDiag::pushBack(match.getJSON());
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

    // Get winning match
    if (!matches.empty()) {
        auto winningShaderMatch = PatcherUtil::getWinningMatch(matches, m_modPriority);
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
