#include "ParallaxGenDirectory.hpp"

#include "BethesdaDirectory.hpp"
#include "BethesdaGame.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenRunner.hpp"
#include "ParallaxGenTask.hpp"
#include "util/Logger.hpp"
#include "util/MeshTracker.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include "NifFile.hpp"
#include "Shaders.hpp"
#include <DirectXTex.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/crc.hpp>
#include <boost/thread.hpp>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <shlwapi.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <winnt.h>

using namespace std;
using namespace ParallaxGenUtil;

ParallaxGenDirectory::ParallaxGenDirectory(BethesdaGame* bg, filesystem::path outputPath)
    : BethesdaDirectory(bg, std::move(outputPath))
{
}

ParallaxGenDirectory::ParallaxGenDirectory(filesystem::path dataPath, filesystem::path outputPath)
    : BethesdaDirectory(std::move(dataPath), std::move(outputPath))
{
}

auto ParallaxGenDirectory::findFiles() -> void
{
    // Clear existing unconfirmedtextures
    m_unconfirmedTextures.clear();
    m_unconfirmedMeshes.clear();

    // Populate unconfirmed maps
    Logger::info("Finding Relevant Files");
    const auto& fileMap = getFileMap();

    if (fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    for (const auto& [path, file] : fileMap) {
        const auto& firstPath = path.begin()->wstring();
        if (boost::iequals(firstPath, "textures") && boost::iequals(path.extension().wstring(), L".dds")) {
            if (!isPathAscii(path)) {
                // Skip non-ascii paths
                Logger::warn(
                    L"Texture {} contains non-ascii characters which are not allowed - skipping", path.wstring());
                continue;
            }

            // Found a DDS
            Logger::trace(L"Found texture: {} / {}", path.wstring(),
                file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
            m_unconfirmedTextures[path] = {};

            {
                // add to textures set
                const unique_lock lock(m_texturesMutex);
                m_textures.insert(path);
            }
        } else if (boost::iequals(firstPath, "meshes") && boost::iequals(path.extension().wstring(), L".nif")) {
            // Found a NIF
            Logger::trace(
                L"Found mesh: {} / {}", path.wstring(), file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
            m_unconfirmedMeshes.insert(path);
        } else if (boost::iequals(path.extension().wstring(), L".json")) {
            // Found a JSON file
            if (boost::iequals(firstPath, L"pbrnifpatcher")) {
                // Found PBR JSON config
                Logger::trace(L"Found PBR json: {} / {}", path.wstring(),
                    file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
                m_pbrJSONs.push_back(path);
            } else if (boost::iequals(firstPath, L"lightplacer")) {
                // Found Light Placer JSON config
                Logger::trace(L"Found light placer json: {} / {}", path.wstring(),
                    file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
                m_lightPlacerJSONs.push_back(path);
            }
        }
    }
}

void ParallaxGenDirectory::waitForMeshMapping()
{
    if (m_meshUseMappingQueue.isShutdown()) {
        // already done
        return;
    }

    if (m_meshUseMappingQueue.isProcessing()) {
        Logger::info("Waiting for plugin mesh use mapping to complete...");
        m_meshUseMappingQueue.waitForCompletion();
    }

    // shutdown the queue to free resources
    m_meshUseMappingQueue.shutdown();
}

void ParallaxGenDirectory::waitForCMClassification()
{
    if (m_CMClassificationQueue.isShutdown()) {
        // already done
        return;
    }

    if (m_CMClassificationQueue.isProcessing()) {
        Logger::info("Waiting for extended texture classification to complete...");
        m_CMClassificationQueue.waitForCompletion();
    }

    // shutdown the queue to free resources
    m_CMClassificationQueue.shutdown();
}

auto ParallaxGenDirectory::mapFiles(const vector<wstring>& nifBlocklist, const vector<wstring>& nifAllowlist,
    const vector<pair<wstring, NIFUtil::TextureType>>& manualTextureMaps, const vector<wstring>& parallaxBSAExcludes,
    const bool& multithreading, const bool& highmem, const std::function<void(size_t, size_t)>& progressCallback)
    -> void
{
    findFiles();

    // Helpers
    const unordered_map<wstring, NIFUtil::TextureType> manualTextureMapsMap(
        manualTextureMaps.begin(), manualTextureMaps.end());

    Logger::info("Starting to build texture mappings");

    // Create task tracker
    ParallaxGenTask taskTracker("Loading NIFs", m_unconfirmedMeshes.size());
    if (progressCallback) {
        taskTracker.setCallbackFunc(progressCallback);
    }

    // Create runner
    ParallaxGenRunner runner(multithreading);

    // Loop through each mesh to confirm textures
    for (const auto& mesh : m_unconfirmedMeshes) {
        if (!nifAllowlist.empty() && !checkGlobMatchInVector(mesh.wstring(), nifAllowlist)) {
            // Skip mesh because it is not on allowlist
            Logger::debug(L"Skipping mesh due to allowlist: {}", mesh.wstring());
            taskTracker.completeJob(ParallaxGenTask::PGResult::SUCCESS);
            continue;
        }

        if (!nifBlocklist.empty() && checkGlobMatchInVector(mesh.wstring(), nifBlocklist)) {
            // Skip mesh because it is on blocklist
            Logger::debug(L"Skipping mesh due to blocklist: {}", mesh.wstring());
            taskTracker.completeJob(ParallaxGenTask::PGResult::SUCCESS);
            continue;
        }

        runner.addTask([this, &taskTracker, &mesh, &highmem, &multithreading] {
            taskTracker.completeJob(mapTexturesFromNIF(mesh, highmem, multithreading));
        });
    }

    // Blocks until all tasks are done
    runner.runTasks();

    // Loop through unconfirmed textures to confirm them
    for (const auto& [texture, property] : m_unconfirmedTextures) {
        bool foundInstance = false;

        // Find winning texture slot
        size_t maxVal = 0;
        NIFUtil::TextureSlots winningSlot = {};
        for (const auto& [slot, count] : property.slots) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningSlot = slot;
            }
        }

        // Find winning texture type
        maxVal = 0;
        NIFUtil::TextureType winningType = {};
        for (const auto& [type, count] : property.types) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningType = type;
            }
        }

        if (!foundInstance) {
            // Determine slot and type by suffix
            const auto defProperty = NIFUtil::getDefaultsFromSuffix(texture);
            winningSlot = get<0>(defProperty);
            winningType = get<1>(defProperty);
        }

        if (manualTextureMapsMap.contains(texture.wstring())) {
            // Manual texture map found, override
            winningType = manualTextureMapsMap.at(texture.wstring());
            winningSlot = NIFUtil::getSlotFromTexType(winningType);
        }

        if (winningSlot == NIFUtil::TextureSlots::PARALLAX && isFileInBSA(texture, parallaxBSAExcludes)) {
            continue;
        }

        // extended classification
        // check if CM
        if (winningType == NIFUtil::TextureType::ENVIRONMENTMASK && !isFileInBSA(texture, parallaxBSAExcludes)) {
            if (multithreading) {
                m_CMClassificationQueue.queueTask(
                    [this, texture, winningSlot]() -> void { checkIfCMAddToMap(texture, winningSlot); });
            } else {
                checkIfCMAddToMap(texture, winningSlot);
            }

            // defer adding to texture maps until classification is done
            continue;
        }

        // Add to texture map
        if (winningSlot != NIFUtil::TextureSlots::UNKNOWN) {
            // Only add if no unknowns
            addToTextureMaps(texture, winningSlot, winningType, {});
        }
    }

    // cleanup
    m_unconfirmedTextures.clear();
    m_unconfirmedMeshes.clear();
}

void ParallaxGenDirectory::checkIfCMAddToMap(
    const std::filesystem::path& texture, const NIFUtil::TextureSlots& winningSlot)
{
    // classify as CM or not
    bool hasMetalness = false;
    bool hasGlosiness = false;
    bool hasEnvMask = false;
    bool result = false;

    bool success = false;
    try {
        success = PGGlobals::getPGD3D()->checkIfCM(texture, result, hasEnvMask, hasGlosiness, hasMetalness);
    } catch (...) {
        success = false;
    }

    if (!success) {
        Logger::error(L"Failed to check if {} is complex material", texture.wstring());
        return;
    }

    if (!result) {
        // regular env mask
        addToTextureMaps(texture, winningSlot, NIFUtil::TextureType::ENVIRONMENTMASK, {});
        return;
    }

    unordered_set<NIFUtil::TextureAttribute> attributes;
    if (hasEnvMask) {
        attributes.insert(NIFUtil::TextureAttribute::CM_ENVMASK);
    }
    if (hasGlosiness) {
        attributes.insert(NIFUtil::TextureAttribute::CM_GLOSSINESS);
    }
    if (hasMetalness) {
        attributes.insert(NIFUtil::TextureAttribute::CM_METALNESS);
    }

    addToTextureMaps(texture, winningSlot, NIFUtil::TextureType::COMPLEXMATERIAL, attributes);
}

auto ParallaxGenDirectory::checkGlobMatchInVector(const wstring& check, const vector<std::wstring>& list) -> bool
{
    // convert wstring to LPCWSTR
    LPCWSTR checkCstr = check.c_str();

    // check if string matches any glob
    return std::ranges::any_of(list, [&](const wstring& glob) { return PathMatchSpecW(checkCstr, glob.c_str()); });
}

auto ParallaxGenDirectory::mapTexturesFromNIF(
    const filesystem::path& nifPath, const bool& cachenif, const bool& multithreading) -> ParallaxGenTask::PGResult
{
    auto result = ParallaxGenTask::PGResult::SUCCESS;

    // Load NIF
    shared_ptr<nifly::NifFile> nif = nullptr;
    vector<std::byte> nifBytes;
    {
        try {
            nifBytes = getFile(nifPath);
        } catch (...) {
            Logger::error(L"Error reading NIF File \"{}\" (skipping)", nifPath.wstring());
            return ParallaxGenTask::PGResult::FAILURE;
        }

        try {
            // Attempt to load NIF file
            nif = make_shared<nifly::NifFile>(NIFUtil::loadNIFFromBytes(nifBytes));
        } catch (...) {
            // Unable to read NIF, delete from Meshes set
            Logger::error(L"Error reading NIF File \"{}\" (skipping)", nifPath.wstring());
            return ParallaxGenTask::PGResult::FAILURE;
        }
    }

    // Loop through each shape
    const auto shapes = NIFUtil::getShapesWithBlockIDs(nif.get());
    // clear shapes in cache
    std::vector<std::pair<int, NIFUtil::TextureSet>> textureSets;
    for (const auto& [shape, oldindex3d] : shapes) {
        if (shape == nullptr) {
            // Skip if shape is null (invalid shapes)
            continue;
        }

        if (!NIFUtil::isPatchableShape(*nif, *shape)) {
            // Skip if not patchable shape
            continue;
        }

        if (!NIFUtil::isShaderPatchableShape(*nif, *shape)) {
            // Skip if not shader patchable shape
            continue;
        }

        auto* const shader = nif->GetShader(shape);
        const auto textureSet = NIFUtil::getTextureSlots(nif.get(), shape);
        textureSets.emplace_back(oldindex3d, textureSet);

        // Loop through each texture slot
        for (uint32_t slot = 0; slot < NUM_TEXTURE_SLOTS; slot++) {
            string texture = utf16toUTF8(textureSet.at(slot));

            if (!containsOnlyAscii(texture)) {
                Logger::error(L"NIF {} has texture slot(s) with invalid non-ASCII chars (skipping)", nifPath.wstring());
                return ParallaxGenTask::PGResult::FAILURE;
            }

            if (texture.empty()) {
                // No texture in this slot
                continue;
            }

            toLowerASCIIFastInPlace(texture); // Lowercase for comparison

            const auto shaderType = shader->GetShaderType();
            NIFUtil::TextureType textureType = {};

            // Check to make sure appropriate shaders are set for a given texture
            auto* const shaderBSSP = dynamic_cast<BSShaderProperty*>(shader);
            if (shaderBSSP == nullptr) {
                // Not a BSShaderProperty, skip
                continue;
            }

            switch (static_cast<NIFUtil::TextureSlots>(slot)) {
            case NIFUtil::TextureSlots::DIFFUSE:
                // Diffuse check
                textureType = NIFUtil::TextureType::DIFFUSE;
                break;
            case NIFUtil::TextureSlots::NORMAL:
                // Normal check
                if (shaderType == BSLSP_SKINTINT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map
                    textureType = NIFUtil::TextureType::MODELSPACENORMAL;
                    break;
                }

                textureType = NIFUtil::TextureType::NORMAL;
                break;
            case NIFUtil::TextureSlots::GLOW:
                // Glowmap check
                if ((shaderType == BSLSP_GLOWMAP && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_GLOW_MAP))
                    || (shaderType == BSLSP_DEFAULT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01))) {
                    // This is an emmissive map (either vanilla glowmap shader or PBR)
                    textureType = NIFUtil::TextureType::EMISSIVE;
                    break;
                }

                if (shaderType == BSLSP_MULTILAYERPARALLAX
                    && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_MULTI_LAYER_PARALLAX)) {
                    // This is a subsurface map
                    textureType = NIFUtil::TextureType::SUBSURFACECOLOR;
                    break;
                }

                if (shaderType == BSLSP_SKINTINT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map
                    textureType = NIFUtil::TextureType::SKINTINT;
                    break;
                }

                continue;
            case NIFUtil::TextureSlots::PARALLAX:
                // Parallax check
                if ((shaderType == BSLSP_PARALLAX && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_PARALLAX))) {
                    // This is a height map
                    textureType = NIFUtil::TextureType::HEIGHT;
                    break;
                }

                if ((shaderType == BSLSP_DEFAULT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01))) {
                    // This is a height map for PBR
                    textureType = NIFUtil::TextureType::HEIGHTPBR;
                    break;
                }

                continue;
            case NIFUtil::TextureSlots::CUBEMAP:
                // Cubemap check
                if (shaderType == BSLSP_ENVMAP && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = NIFUtil::TextureType::CUBEMAP;
                    break;
                }

                continue;
            case NIFUtil::TextureSlots::ENVMASK:
                // Envmap check
                if (shaderType == BSLSP_ENVMAP && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = NIFUtil::TextureType::ENVIRONMENTMASK;
                    break;
                }

                if (shaderType == BSLSP_DEFAULT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                    textureType = NIFUtil::TextureType::RMAOS;
                    break;
                }

                continue;
            case NIFUtil::TextureSlots::MULTILAYER:
                // Tint check
                if (shaderType == BSLSP_MULTILAYERPARALLAX
                    && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_MULTI_LAYER_PARALLAX)) {
                    if (NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                        // 2 layer PBR
                        textureType = NIFUtil::TextureType::COATNORMALROUGHNESS;
                    } else {
                        // normal multilayer
                        textureType = NIFUtil::TextureType::INNERLAYER;
                    }
                    break;
                }

                continue;
            case NIFUtil::TextureSlots::BACKLIGHT:
                // Backlight check
                if (shaderType == BSLSP_MULTILAYERPARALLAX && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                    textureType = NIFUtil::TextureType::SUBSURFACEPBR;
                    break;
                }

                if (NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_BACK_LIGHTING)) {
                    textureType = NIFUtil::TextureType::BACKLIGHT;
                    break;
                }

                if (shaderType == BSLSP_SKINTINT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    textureType = NIFUtil::TextureType::SPECULAR;
                    break;
                }

                if (shaderType == BSLSP_HAIRTINT && NIFUtil::hasShaderFlag(shaderBSSP, SLSF2_BACK_LIGHTING)) {
                    // Hair tint map
                    textureType = NIFUtil::TextureType::HAIR_FLOWMAP;
                    break;
                }

                continue;
            default:
                textureType = NIFUtil::TextureType::UNKNOWN;
            }

            // Update unconfirmed textures map
            updateUnconfirmedTexturesMap(texture, static_cast<NIFUtil::TextureSlots>(slot), textureType);
        }
    }

    if (multithreading) {
        m_meshUseMappingQueue.queueTask([this, nifPath]() -> void {
            // send job to find mesh uses for this mesh
            const auto modelUses = ParallaxGenPlugin::getModelUses(nifPath);
            updateNifCache(nifPath, modelUses);
        });
    } else {
        // send job to find mesh uses for this mesh
        const auto modelUses = ParallaxGenPlugin::getModelUses(nifPath);
        updateNifCache(nifPath, modelUses);
    }

    if (cachenif) {
        // Calculate original CRC32
        boost::crc_32_type crcBeforeResult {};
        crcBeforeResult.process_bytes(nifBytes.data(), nifBytes.size());
        updateNifCache(nifPath, nif, crcBeforeResult.checksum());
    }

    // update nif cache
    updateNifCache(nifPath, textureSets);

    // find mod of this mesh
    if (PGGlobals::getMMD() != nullptr) {
        auto mod = PGGlobals::getMMD()->getModByFileSmart(nifPath);
        if (mod != nullptr) {
            const unique_lock<shared_mutex> lock(mod->mutex);
            mod->hasMeshes = true;
        }
    }

    return result;
}

auto ParallaxGenDirectory::updateUnconfirmedTexturesMap(
    const filesystem::path& path, const NIFUtil::TextureSlots& slot, const NIFUtil::TextureType& type) -> void
{
    // Use mutex to make this thread safe
    const lock_guard<mutex> lock(m_unconfirmedTexturesMutex);

    // Check if texture is already in map
    if (m_unconfirmedTextures.contains(path)) {
        // Texture is present
        m_unconfirmedTextures[path].slots[slot]++;
        m_unconfirmedTextures[path].types[type]++;
    }
}

auto ParallaxGenDirectory::addToTextureMaps(const filesystem::path& path, const NIFUtil::TextureSlots& slot,
    const NIFUtil::TextureType& type, const unordered_set<NIFUtil::TextureAttribute>& attributes) -> void
{
    // Log result
    Logger::trace(L"Mapping Texture: {} / Slot: {} / Type: {}", path.wstring(), static_cast<size_t>(slot),
        utf8toUTF16(NIFUtil::getStrFromTexType(type)));

    // Get texture base
    const auto& base = NIFUtil::getTexBase(path, slot);
    const auto& slotInt = static_cast<size_t>(slot);

    // Add to texture map
    const NIFUtil::PGTexture newPGTexture = { .path = path, .type = type };
    {
        const unique_lock lock(m_textureMapsMutex);
        m_textureMaps.at(slotInt)[base].insert(newPGTexture);
    }

    {
        const TextureDetails details = { .type = type, .attributes = attributes };

        const unique_lock lock(m_textureTypesMutex);
        m_textureTypes[path] = details;
    }
}

void ParallaxGenDirectory::updateNifCache(
    const filesystem::path& path, const vector<pair<int, NIFUtil::TextureSet>>& txstSets)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).textureSets = txstSets;
}

void ParallaxGenDirectory::updateNifCache(const filesystem::path& path,
    const vector<pair<MeshTracker::FormKey, ParallaxGenPlugin::MeshUseAttributes>>& meshUses)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).meshUses = meshUses;
}

void ParallaxGenDirectory::updateNifCache(
    const filesystem::path& path, const shared_ptr<nifly::NifFile>& nif, const unsigned long long& crc32)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).nif = nif;
    m_meshes.at(path).origCRC32 = crc32;
}

auto ParallaxGenDirectory::getTextureMap(const NIFUtil::TextureSlots& slot)
    -> map<wstring, unordered_set<NIFUtil::PGTexture, NIFUtil::PGTextureHasher>>&
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

auto ParallaxGenDirectory::getTextureMapConst(const NIFUtil::TextureSlots& slot) const
    -> const map<wstring, unordered_set<NIFUtil::PGTexture, NIFUtil::PGTextureHasher>>&
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

auto ParallaxGenDirectory::getMeshes() const -> const unordered_map<filesystem::path, NifCache>& { return m_meshes; }

auto ParallaxGenDirectory::getTextures() const -> const unordered_set<filesystem::path>& { return m_textures; }

auto ParallaxGenDirectory::getPBRJSONs() const -> const vector<filesystem::path>& { return m_pbrJSONs; }

auto ParallaxGenDirectory::getLightPlacerJSONs() const -> const vector<filesystem::path>& { return m_lightPlacerJSONs; }

auto ParallaxGenDirectory::addTextureAttribute(const filesystem::path& path, const NIFUtil::TextureAttribute& attribute)
    -> bool
{
    const unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.insert(attribute).second;
    }

    return false;
}

auto ParallaxGenDirectory::removeTextureAttribute(
    const filesystem::path& path, const NIFUtil::TextureAttribute& attribute) -> bool
{
    const unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.erase(attribute) > 0;
    }

    return false;
}

auto ParallaxGenDirectory::hasTextureAttribute(const filesystem::path& path, const NIFUtil::TextureAttribute& attribute)
    -> bool
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.contains(attribute);
    }

    return false;
}

auto ParallaxGenDirectory::getTextureAttributes(const filesystem::path& path)
    -> unordered_set<NIFUtil::TextureAttribute>
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes;
    }

    return {};
}

void ParallaxGenDirectory::setTextureType(const filesystem::path& path, const NIFUtil::TextureType& type)
{
    const unique_lock lock(m_textureTypesMutex);
    m_textureTypes[path].type = type;
}

auto ParallaxGenDirectory::getTextureType(const filesystem::path& path) -> NIFUtil::TextureType
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).type;
    }

    return NIFUtil::TextureType::UNKNOWN;
}
