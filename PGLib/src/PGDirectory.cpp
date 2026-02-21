#include "PGDirectory.hpp"

#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"
#include "util/TaskTracker.hpp"

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
using namespace StringUtil;

PGDirectory::PGDirectory(BethesdaGame* bg,
                         filesystem::path outputPath)
    : BethesdaDirectory(bg,
                        PGGlobals::s_foldersToMap,
                        std::move(outputPath))
{
}

PGDirectory::PGDirectory(filesystem::path dataPath,
                         filesystem::path outputPath)
    : BethesdaDirectory(std::move(dataPath),
                        PGGlobals::s_foldersToMap,
                        std::move(outputPath))
{
}

auto PGDirectory::findFiles() -> void
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
                Logger::warn(L"Texture {} contains non-ascii characters which are not allowed - skipping",
                             path.wstring());
                continue;
            }

            // Found a DDS
            Logger::trace(L"Found texture: {} / {}",
                          path.wstring(),
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
                Logger::trace(L"Found PBR json: {} / {}",
                              path.wstring(),
                              file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
                m_pbrJSONs.push_back(path);
            } else if (boost::iequals(firstPath, L"lightplacer")) {
                // Found Light Placer JSON config
                Logger::trace(L"Found light placer json: {} / {}",
                              path.wstring(),
                              file.bsaFile == nullptr ? L"" : file.bsaFile->path.wstring());
                m_lightPlacerJSONs.push_back(path);
            }
        }
    }
}

void PGDirectory::waitForMeshMapping()
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

void PGDirectory::waitForCMClassification()
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

auto PGDirectory::mapFiles(const vector<wstring>& nifBlocklist,
                           const vector<wstring>& nifAllowlist,
                           const vector<pair<wstring,
                                             PGEnums::TextureType>>& manualTextureMaps,
                           const vector<wstring>& parallaxBSAExcludes,
                           const bool& multithreading,
                           const bool& highmem,
                           const std::function<void(size_t,
                                                    size_t)>& progressCallback) -> void
{
    findFiles();

    // Helpers
    const unordered_map<wstring, PGEnums::TextureType> manualTextureMapsMap(manualTextureMaps.begin(),
                                                                            manualTextureMaps.end());

    Logger::info("Starting to build texture mappings");

    // Create task tracker
    TaskTracker taskTracker("Loading NIFs", m_unconfirmedMeshes.size());
    if (progressCallback) {
        taskTracker.setCallbackFunc(progressCallback);
    }

    // Create runner
    TaskPoolRunner runner(multithreading);

    // Loop through each mesh to confirm textures
    for (const auto& mesh : m_unconfirmedMeshes) {
        if (!nifAllowlist.empty() && !checkGlobMatchInVector(mesh.wstring(), nifAllowlist)) {
            // Skip mesh because it is not on allowlist
            Logger::debug(L"Skipping mesh due to allowlist: {}", mesh.wstring());
            taskTracker.completeJob(TaskTracker::Result::SUCCESS);
            continue;
        }

        if (!nifBlocklist.empty() && checkGlobMatchInVector(mesh.wstring(), nifBlocklist)) {
            // Skip mesh because it is on blocklist
            Logger::debug(L"Skipping mesh due to blocklist: {}", mesh.wstring());
            taskTracker.completeJob(TaskTracker::Result::SUCCESS);
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
        PGEnums::TextureSlots winningSlot = {};
        for (const auto& [slot, count] : property.slots) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningSlot = slot;
            }
        }

        // Find winning texture type
        maxVal = 0;
        PGEnums::TextureType winningType = {};
        for (const auto& [type, count] : property.types) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningType = type;
            }
        }

        if (!foundInstance) {
            // Determine slot and type by suffix
            const auto defProperty = PGNIFUtil::getDefaultsFromSuffix(texture);
            winningSlot = get<0>(defProperty);
            winningType = get<1>(defProperty);
        }

        if (manualTextureMapsMap.contains(texture.wstring())) {
            // Manual texture map found, override
            winningType = manualTextureMapsMap.at(texture.wstring());
            winningSlot = PGNIFUtil::getSlotFromTexType(winningType);
        }

        if (winningSlot == PGEnums::TextureSlots::PARALLAX && isFileInBSA(texture, parallaxBSAExcludes)) {
            continue;
        }

        // extended classification
        // check if CM
        if (winningType == PGEnums::TextureType::ENVIRONMENTMASK && !isFileInBSA(texture, parallaxBSAExcludes)) {
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
        if (winningSlot != PGEnums::TextureSlots::UNKNOWN) {
            // Only add if no unknowns
            addToTextureMaps(texture, winningSlot, winningType, {});
        }
    }

    // cleanup
    m_unconfirmedTextures.clear();
    m_unconfirmedMeshes.clear();
}

void PGDirectory::checkIfCMAddToMap(const std::filesystem::path& texture,
                                    const PGEnums::TextureSlots& winningSlot)
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
        addToTextureMaps(texture, winningSlot, PGEnums::TextureType::ENVIRONMENTMASK, {});
        return;
    }

    unordered_set<PGEnums::TextureAttribute> attributes;
    if (hasEnvMask) {
        attributes.insert(PGEnums::TextureAttribute::CM_ENVMASK);
    }
    if (hasGlosiness) {
        attributes.insert(PGEnums::TextureAttribute::CM_GLOSSINESS);
    }
    if (hasMetalness) {
        attributes.insert(PGEnums::TextureAttribute::CM_METALNESS);
    }

    addToTextureMaps(texture, winningSlot, PGEnums::TextureType::COMPLEXMATERIAL, attributes);
}

auto PGDirectory::checkGlobMatchInVector(const wstring& check,
                                         const vector<std::wstring>& list) -> bool
{
    // convert wstring to LPCWSTR
    LPCWSTR checkCstr = check.c_str();

    // check if string matches any glob
    return std::ranges::any_of(list, [&](const wstring& glob) { return PathMatchSpecW(checkCstr, glob.c_str()); });
}

auto PGDirectory::mapTexturesFromNIF(const filesystem::path& nifPath,
                                     const bool& cachenif,
                                     const bool& multithreading) -> TaskTracker::Result
{
    auto result = TaskTracker::Result::SUCCESS;

    // Load NIF
    shared_ptr<nifly::NifFile> nif = nullptr;
    vector<std::byte> nifBytes;
    {
        try {
            nifBytes = getFile(nifPath);
        } catch (...) {
            Logger::error(L"Error reading NIF File \"{}\" (skipping)", nifPath.wstring());
            return TaskTracker::Result::FAILURE;
        }

        try {
            // Attempt to load NIF file
            nif = make_shared<nifly::NifFile>(PGNIFUtil::loadNIFFromBytes(nifBytes));
        } catch (...) {
            // Unable to read NIF, delete from Meshes set
            Logger::error(L"Error reading NIF File \"{}\" (skipping)", nifPath.wstring());
            return TaskTracker::Result::FAILURE;
        }
    }

    // Loop through each shape
    const auto shapes = PGNIFUtil::getShapesWithBlockIDs(nif.get());
    // clear shapes in cache
    std::vector<std::pair<int, PGTypes::TextureSet>> textureSets;
    for (const auto& [shape, oldindex3d] : shapes) {
        if (shape == nullptr) {
            // Skip if shape is null (invalid shapes)
            continue;
        }

        if (!PGNIFUtil::isPatchableShape(*nif, *shape)) {
            // Skip if not patchable shape
            continue;
        }

        if (!PGNIFUtil::isShaderPatchableShape(*nif, *shape)) {
            // Skip if not shader patchable shape
            continue;
        }

        auto* const shader = nif->GetShader(shape);
        const auto textureSet = PGNIFUtil::getTextureSlots(nif.get(), shape);
        textureSets.emplace_back(oldindex3d, textureSet);

        // Loop through each texture slot
        for (uint32_t slot = 0; slot < NUM_TEXTURE_SLOTS; slot++) {
            string texture = utf16toUTF8(textureSet.at(slot));

            if (!containsOnlyAscii(texture)) {
                Logger::error(L"NIF {} has texture slot(s) with invalid non-ASCII chars (skipping)", nifPath.wstring());
                return TaskTracker::Result::FAILURE;
            }

            if (texture.empty()) {
                // No texture in this slot
                continue;
            }

            toLowerASCIIFastInPlace(texture); // Lowercase for comparison

            const auto shaderType = shader->GetShaderType();
            PGEnums::TextureType textureType = {};

            // Check to make sure appropriate shaders are set for a given texture
            auto* const shaderBSSP = dynamic_cast<BSShaderProperty*>(shader);
            if (shaderBSSP == nullptr) {
                // Not a BSShaderProperty, skip
                continue;
            }

            switch (static_cast<PGEnums::TextureSlots>(slot)) {
            case PGEnums::TextureSlots::DIFFUSE:
                // Diffuse check
                textureType = PGEnums::TextureType::DIFFUSE;
                break;
            case PGEnums::TextureSlots::NORMAL:
                // Normal check
                if (shaderType == BSLSP_SKINTINT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map
                    textureType = PGEnums::TextureType::MODELSPACENORMAL;
                    break;
                }

                textureType = PGEnums::TextureType::NORMAL;
                break;
            case PGEnums::TextureSlots::GLOW:
                // Glowmap check
                if ((shaderType == BSLSP_GLOWMAP && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_GLOW_MAP))
                    || (shaderType == BSLSP_DEFAULT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01))) {
                    // This is an emmissive map (either vanilla glowmap shader or PBR)
                    textureType = PGEnums::TextureType::EMISSIVE;
                    break;
                }

                if (shaderType == BSLSP_MULTILAYERPARALLAX
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_MULTI_LAYER_PARALLAX)) {
                    // This is a subsurface map
                    textureType = PGEnums::TextureType::SUBSURFACECOLOR;
                    break;
                }

                if (shaderType == BSLSP_SKINTINT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map
                    textureType = PGEnums::TextureType::SKINTINT;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::PARALLAX:
                // Parallax check
                if ((shaderType == BSLSP_PARALLAX && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_PARALLAX))) {
                    // This is a height map
                    textureType = PGEnums::TextureType::HEIGHT;
                    break;
                }

                if ((shaderType == BSLSP_DEFAULT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01))) {
                    // This is a height map for PBR
                    textureType = PGEnums::TextureType::HEIGHTPBR;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::CUBEMAP:
                // Cubemap check
                if (shaderType == BSLSP_ENVMAP && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = PGEnums::TextureType::CUBEMAP;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::ENVMASK:
                // Envmap check
                if (shaderType == BSLSP_ENVMAP && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = PGEnums::TextureType::ENVIRONMENTMASK;
                    break;
                }

                if (shaderType == BSLSP_DEFAULT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                    textureType = PGEnums::TextureType::RMAOS;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::MULTILAYER:
                // Tint check
                if (shaderType == BSLSP_MULTILAYERPARALLAX
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_MULTI_LAYER_PARALLAX)) {
                    if (PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                        // 2 layer PBR
                        textureType = PGEnums::TextureType::COATNORMALROUGHNESS;
                    } else {
                        // normal multilayer
                        textureType = PGEnums::TextureType::INNERLAYER;
                    }
                    break;
                }

                continue;
            case PGEnums::TextureSlots::BACKLIGHT:
                // Backlight check
                if (shaderType == BSLSP_MULTILAYERPARALLAX && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_UNUSED01)) {
                    textureType = PGEnums::TextureType::SUBSURFACEPBR;
                    break;
                }

                if (PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_BACK_LIGHTING)) {
                    textureType = PGEnums::TextureType::BACKLIGHT;
                    break;
                }

                if (shaderType == BSLSP_SKINTINT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF1_FACEGEN_RGB_TINT)) {
                    textureType = PGEnums::TextureType::SPECULAR;
                    break;
                }

                if (shaderType == BSLSP_HAIRTINT && PGNIFUtil::hasShaderFlag(shaderBSSP, SLSF2_BACK_LIGHTING)) {
                    // Hair tint map
                    textureType = PGEnums::TextureType::HAIR_FLOWMAP;
                    break;
                }

                continue;
            default:
                textureType = PGEnums::TextureType::UNKNOWN;
            }

            // Update unconfirmed textures map
            updateUnconfirmedTexturesMap(texture, static_cast<PGEnums::TextureSlots>(slot), textureType);
        }
    }

    if (multithreading) {
        m_meshUseMappingQueue.queueTask([this, nifPath]() -> void {
            // send job to find mesh uses for this mesh
            const auto modelUses = PGPlugin::getModelUses(nifPath);
            updateNifCache(nifPath, modelUses);
        });
    } else {
        // send job to find mesh uses for this mesh
        const auto modelUses = PGPlugin::getModelUses(nifPath);
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
    if (PGGlobals::isPGMMSet()) {
        auto mod = PGGlobals::getPGMM()->getModByFileSmart(nifPath);
        if (mod != nullptr) {
            const unique_lock<shared_mutex> lock(mod->mutex);
            mod->hasMeshes = true;
        }
    }

    return result;
}

auto PGDirectory::updateUnconfirmedTexturesMap(const filesystem::path& path,
                                               const PGEnums::TextureSlots& slot,
                                               const PGEnums::TextureType& type) -> void
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

auto PGDirectory::addToTextureMaps(const filesystem::path& path,
                                   const PGEnums::TextureSlots& slot,
                                   const PGEnums::TextureType& type,
                                   const unordered_set<PGEnums::TextureAttribute>& attributes) -> void
{
    // Log result
    Logger::trace(L"Mapping Texture: {} / Slot: {} / Type: {}",
                  path.wstring(),
                  static_cast<size_t>(slot),
                  utf8toUTF16(PGEnums::getStrFromTexType(type)));

    // Get texture base
    const auto& base = PGNIFUtil::getTexBase(path, slot);
    const auto& slotInt = static_cast<size_t>(slot);

    // Add to texture map
    const PGTypes::PGTexture newPGTexture = {.path = path, .type = type};
    {
        const unique_lock lock(m_textureMapsMutex);
        m_textureMaps.at(slotInt)[base].insert(newPGTexture);
    }

    {
        const TextureDetails details = {.type = type, .attributes = attributes};

        const unique_lock lock(m_textureTypesMutex);
        m_textureTypes[path] = details;
    }
}

void PGDirectory::updateNifCache(const filesystem::path& path,
                                 const vector<pair<int,
                                                   PGTypes::TextureSet>>& txstSets)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).textureSets = txstSets;
}

void PGDirectory::updateNifCache(const filesystem::path& path,
                                 const vector<pair<PGMeshPermutationTracker::FormKey,
                                                   PGPlugin::MeshUseAttributes>>& meshUses)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).meshUses = meshUses;
}

void PGDirectory::updateNifCache(const filesystem::path& path,
                                 const shared_ptr<nifly::NifFile>& nif,
                                 const unsigned long long& crc32)
{
    const unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path)) {
        m_meshes[path] = NifCache {};
    }

    m_meshes.at(path).nif = nif;
    m_meshes.at(path).origCRC32 = crc32;
}

auto PGDirectory::getTextureMap(const PGEnums::TextureSlots& slot) -> map<wstring,
                                                                          unordered_set<PGTypes::PGTexture,
                                                                                        PGTypes::PGTextureHasher>>&
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

auto PGDirectory::getTextureMapConst(const PGEnums::TextureSlots& slot) const
    -> const map<wstring,
                 unordered_set<PGTypes::PGTexture,
                               PGTypes::PGTextureHasher>>&
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

auto PGDirectory::getMeshes() const -> const unordered_map<filesystem::path,
                                                           NifCache>&
{
    return m_meshes;
}

auto PGDirectory::getTextures() const -> const unordered_set<filesystem::path>& { return m_textures; }

auto PGDirectory::getPBRJSONs() const -> const vector<filesystem::path>& { return m_pbrJSONs; }

auto PGDirectory::getLightPlacerJSONs() const -> const vector<filesystem::path>& { return m_lightPlacerJSONs; }

auto PGDirectory::addTextureAttribute(const filesystem::path& path,
                                      const PGEnums::TextureAttribute& attribute) -> bool
{
    const unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.insert(attribute).second;
    }

    return false;
}

auto PGDirectory::removeTextureAttribute(const filesystem::path& path,
                                         const PGEnums::TextureAttribute& attribute) -> bool
{
    const unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.erase(attribute) > 0;
    }

    return false;
}

auto PGDirectory::hasTextureAttribute(const filesystem::path& path,
                                      const PGEnums::TextureAttribute& attribute) -> bool
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes.contains(attribute);
    }

    return false;
}

auto PGDirectory::getTextureAttributes(const filesystem::path& path) -> unordered_set<PGEnums::TextureAttribute>
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).attributes;
    }

    return {};
}

void PGDirectory::setTextureType(const filesystem::path& path,
                                 const PGEnums::TextureType& type)
{
    const unique_lock lock(m_textureTypesMutex);
    m_textureTypes[path].type = type;
}

auto PGDirectory::getTextureType(const filesystem::path& path) -> PGEnums::TextureType
{
    const shared_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path)) {
        return m_textureTypes.at(path).type;
    }

    return PGEnums::TextureType::UNKNOWN;
}
