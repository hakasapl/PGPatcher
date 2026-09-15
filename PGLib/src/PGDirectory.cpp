#include "PGDirectory.hpp"

#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
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

PGDirectory::PGDirectory(BethesdaGame* bg,
                         std::filesystem::path outputPath)
    : BethesdaDirectory(bg,
                        PGGlobals::s_foldersToMap,
                        std::move(outputPath))
{
}

PGDirectory::PGDirectory(std::filesystem::path dataPath,
                         std::filesystem::path outputPath)
    : BethesdaDirectory(std::move(dataPath),
                        PGGlobals::s_foldersToMap,
                        std::move(outputPath))
{
}

void PGDirectory::findFiles()
{
    // Clear existing unconfirmedtextures.
    m_unconfirmedTextures.clear();
    m_unconfirmedMeshes.clear();

    // Populate unconfirmed maps.
    Logger::info("Finding Relevant Files");
    const auto& fileMap = this->fileMap();

    if (fileMap.empty())
        throw std::runtime_error("File map was not populated");

    for (const auto& [path, file] : fileMap) {
        const auto& firstPath = path.begin()->wstring();
        if (boost::iequals(firstPath, "textures") && boost::iequals(path.extension().wstring(), L".dds")) {
            if (!isPathAscii(path)) {
                // Skip non-ascii paths.
                Logger::warn(L"Texture {} contains non-ascii characters which are not allowed", path.wstring());
                continue;
            }

            // Found a DDS.
            Logger::trace(
                L"Found texture: {} / {}", path.wstring(), !file.bsaFile ? L"" : file.bsaFile->path.wstring());
            m_unconfirmedTextures[path] = { };

            {
                // Add to textures set.
                const std::unique_lock lock(m_texturesMutex);
                m_textures.insert(path);
            }
        } else if (boost::iequals(firstPath, "meshes") && boost::iequals(path.extension().wstring(), L".nif")) {
            // Found a NIF.
            Logger::trace(L"Found mesh: {} / {}", path.wstring(), !file.bsaFile ? L"" : file.bsaFile->path.wstring());
            m_unconfirmedMeshes.insert(path);
        } else if (boost::iequals(path.extension().wstring(), L".json")) {
            // Found a JSON file.
            if (boost::iequals(firstPath, L"pbrnifpatcher")) {
                // Found PBR JSON config.
                Logger::trace(
                    L"Found PBR json: {} / {}", path.wstring(), !file.bsaFile ? L"" : file.bsaFile->path.wstring());
                m_pbrJSONs.push_back(path);

                if (PGGlobals::isPGMMSet())
                    PGGlobals::pgmm()->addShaderToModByFile(path, PGEnums::ShapeShader::TruePBR);
            } else if (boost::iequals(firstPath, L"lightplacer")) {
                // Found Light Placer JSON config.
                Logger::trace(L"Found light placer json: {} / {}",
                              path.wstring(),
                              !file.bsaFile ? L"" : file.bsaFile->path.wstring());
                m_lightPlacerJSONs.push_back(path);
            }
        }
    }
}

void PGDirectory::waitForMeshMapping()
{
    if (m_meshUseMappingQueue.isShutdown()) {
        // Already done.
        return;
    }

    if (m_meshUseMappingQueue.isProcessing()) {
        Logger::info("Waiting for plugin mesh use mapping to complete...");
        m_meshUseMappingQueue.waitForCompletion();
    }

    // Shutdown the queue to free resources.
    m_meshUseMappingQueue.shutdown();
}

void PGDirectory::waitForCMClassification()
{
    if (m_cmClassificationQueue.isShutdown()) {
        // Already done.
        return;
    }

    if (m_cmClassificationQueue.isProcessing()) {
        Logger::info("Waiting for extended texture classification to complete...");
        m_cmClassificationQueue.waitForCompletion();
    }

    // Shutdown the queue to free resources.
    m_cmClassificationQueue.shutdown();
}

void PGDirectory::mapFiles(const std::vector<std::wstring>& nifBlocklist,
                           const std::vector<std::wstring>& nifAllowlist,
                           const std::vector<std::pair<std::wstring,
                                                       PGEnums::TextureType>>& manualTextureMaps,
                           const std::vector<std::wstring>& parallaxBSAExcludes,
                           const bool& multithreading,
                           const std::function<void(size_t,
                                                    size_t)>& progressCallback)
{
    findFiles();

    // Helpers.
    const std::unordered_map<std::wstring, PGEnums::TextureType> manualTextureMapsMap(manualTextureMaps.begin(),
                                                                                      manualTextureMaps.end());

    Logger::info("Starting to build texture mappings");

    // Create task tracker.
    TaskTracker taskTracker("Loading NIFs", m_unconfirmedMeshes.size());
    if (progressCallback)
        taskTracker.setCallbackFunc(progressCallback);

    // Create runner.
    TaskPoolRunner runner(multithreading);

    // Loop through each mesh to confirm textures.
    for (const auto& mesh : m_unconfirmedMeshes) {
        if (!nifAllowlist.empty() && !checkGlobMatchInVector(mesh.wstring(), nifAllowlist)) {
            // Skip mesh because it is not on allowlist.
            Logger::debug(L"Skipping mesh due to allowlist: {}", mesh.wstring());
            taskTracker.completeJob(TaskTracker::Result::Success);
            continue;
        }

        if (!nifBlocklist.empty() && checkGlobMatchInVector(mesh.wstring(), nifBlocklist)) {
            // Skip mesh because it is on blocklist.
            Logger::debug(L"Skipping mesh due to blocklist: {}", mesh.wstring());
            taskTracker.completeJob(TaskTracker::Result::Success);
            continue;
        }

        runner.addTask([this, &taskTracker, &mesh, &multithreading] {
            taskTracker.completeJob(mapTexturesFromNIF(mesh, multithreading));
        });
    }

    // Blocks until all tasks are done.
    runner.runTasks();

    // Loop through unconfirmed textures to confirm them.
    for (const auto& [texture, property] : m_unconfirmedTextures) {
        bool foundInstance = false;

        // Find winning texture slot.
        size_t maxVal = 0;
        PGEnums::TextureSlots winningSlot = { };
        for (const auto& [slot, count] : property.slots) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningSlot = slot;
            }
        }

        // Find winning texture type.
        maxVal = 0;
        PGEnums::TextureType winningType = { };
        for (const auto& [type, count] : property.types) {
            foundInstance = true;
            if (count > maxVal) {
                maxVal = count;
                winningType = type;
            }
        }

        if (!foundInstance) {
            // Determine slot and type by suffix.
            const auto defProperty = PGNIFUtil::defaultsFromSuffix(texture);
            winningSlot = std::get<0>(defProperty);
            winningType = std::get<1>(defProperty);
        }

        if (manualTextureMapsMap.contains(texture.wstring())) {
            // Manual texture map found, override.
            winningType = manualTextureMapsMap.at(texture.wstring());
            winningSlot = PGNIFUtil::slotFromTexType(winningType);
        }

        if (winningSlot == PGEnums::TextureSlots::Parallax && isFileInBSA(texture, parallaxBSAExcludes))
            continue;

        // Extended classification.
        // Check if CM.
        if (winningType == PGEnums::TextureType::EnvironmentMask && !isFileInBSA(texture, parallaxBSAExcludes)) {
            // Reuse the classification of a previous run if the texture did not change.
            PGTypes::CMClassification cachedClassification;
            if (PGRunCache::tryGetCachedCMClassification(texture, fileIdentity(texture), cachedClassification)) {
                applyCMClassification(texture, winningSlot, cachedClassification);
                continue;
            }

            if (multithreading) {
                m_cmClassificationQueue.queueTask(
                    [this, texture, winningSlot] { checkIfCMAddToMap(texture, winningSlot); });
            } else {
                checkIfCMAddToMap(texture, winningSlot);
            }

            // Defer adding to texture maps until classification is done.
            continue;
        }

        // Add to texture map.
        if (winningSlot != PGEnums::TextureSlots::Unknown) {
            // Only add if no unknowns.
            addToTextureMaps(texture, winningSlot, winningType, { });
        }
    }

    // Cleanup.
    m_unconfirmedTextures.clear();
    m_unconfirmedMeshes.clear();
}

void PGDirectory::checkIfCMAddToMap(const std::filesystem::path& texture,
                                    const PGEnums::TextureSlots& winningSlot)
{
    // Classify as CM or not.
    PGTypes::CMClassification classification;

    bool success = false;
    try {
        success = PGGlobals::pgD3D()->checkIfCM(texture,
                                                classification.isCM,
                                                classification.hasEnvMask,
                                                classification.hasGlossiness,
                                                classification.hasMetalness);
    } catch (...) {
        success = false;
    }

    if (!success) {
        Logger::error(L"Unable to process texture: {}", texture.wstring());
        return;
    }

    // Remember the result for incremental runs.
    PGRunCache::storeCMClassification(texture, fileIdentity(texture), classification);

    applyCMClassification(texture, winningSlot, classification);
}

void PGDirectory::applyCMClassification(const std::filesystem::path& texture,
                                        const PGEnums::TextureSlots& winningSlot,
                                        const PGTypes::CMClassification& classification)
{
    if (!classification.isCM) {
        // Regular env mask.
        addToTextureMaps(texture, winningSlot, PGEnums::TextureType::EnvironmentMask, { });
        return;
    }

    std::unordered_set<PGEnums::TextureAttribute> attributes;
    if (classification.hasEnvMask)
        attributes.insert(PGEnums::TextureAttribute::CMEnvMask);
    if (classification.hasGlossiness)
        attributes.insert(PGEnums::TextureAttribute::CMGlossiness);
    if (classification.hasMetalness)
        attributes.insert(PGEnums::TextureAttribute::CMMetalness);

    addToTextureMaps(texture, winningSlot, PGEnums::TextureType::ComplexMaterial, attributes);
}

bool PGDirectory::checkGlobMatchInVector(const std::wstring& check,
                                         const std::vector<std::wstring>& list)
{
    // Convert wstring to LPCWSTR.
    LPCWSTR checkCstr = check.c_str();

    // Check if string matches any glob.
    return std::ranges::any_of(list, [&](const std::wstring& glob) { return PathMatchSpecW(checkCstr, glob.c_str()); });
}

TaskTracker::Result PGDirectory::mapTexturesFromNIF(const std::filesystem::path& nifPath,
                                                    const bool& multithreading)
{
    const auto result = TaskTracker::Result::Success;

    // Texture votes: reuse the votes of a previous run when the NIF did not change, otherwise read the NIF.
    const auto nifIdentity = fileIdentity(nifPath);
    std::vector<PGTypes::TextureVote> votes;
    if (!PGRunCache::tryGetCachedMeshVotes(nifPath, nifIdentity, votes)) {
        if (!readTextureVotesFromNIF(nifPath, votes)) {
            Logger::error(L"Unable to process mesh: {}", nifPath.wstring());
            return TaskTracker::Result::Failure;
        }

        PGRunCache::storeMeshVotes(nifPath, nifIdentity, votes);
    }

    for (const auto& vote : votes)
        updateUnconfirmedTexturesMap(vote.texture, vote.slot, vote.type);

    // Mesh uses: reuse the previous run's uses when no plugin changed, otherwise ask the plugin library.
    PGRunCache::MeshUses cachedUses;
    if (PGRunCache::tryGetCachedMeshUses(nifPath, cachedUses)) {
        updateNifCache(nifPath, cachedUses);
    } else if (multithreading) {
        m_meshUseMappingQueue.queueTask([this, nifPath] {
            // Send job to find mesh uses for this mesh.
            const auto modelUses = PGPlugin::modelUses(nifPath);
            updateNifCache(nifPath, modelUses);
        });
    } else {
        // Send job to find mesh uses for this mesh.
        const auto modelUses = PGPlugin::modelUses(nifPath);
        updateNifCache(nifPath, modelUses);
    }

    // Find mod of this mesh.
    if (PGGlobals::isPGMMSet()) {
        const auto mod = PGGlobals::pgmm()->modByFileSmart(nifPath);
        if (mod) {
            const std::unique_lock<std::shared_mutex> lock(mod->mutex);
            mod->hasMeshes = true;
        }
    }

    return result;
}

bool PGDirectory::readTextureVotesFromNIF(const std::filesystem::path& nifPath,
                                          std::vector<PGTypes::TextureVote>& votes)
{
    // Load NIF.
    std::shared_ptr<nifly::NifFile> nif = nullptr;
    std::vector<std::byte> nifBytes;
    {
        try {
            nifBytes = file(nifPath);
        } catch (...) {
            return false;
        }

        try {
            // Attempt to load NIF file.
            nif = std::make_shared<nifly::NifFile>(PGNIFUtil::loadNIFFromBytes(nifBytes));
        } catch (...) {
            // Unable to read NIF.
            return false;
        }
    }

    // Loop through each shape.
    const auto shapes = PGNIFUtil::shapesWith3DIdx(nif.get());
    // Clear shapes in cache.
    for (const auto& [shape, oldindex3d] : shapes) {
        if (!shape) {
            // Skip if shape is null (invalid shapes).
            continue;
        }

        if (!PGNIFUtil::isPatchableShape(*nif, *shape)) {
            // Skip if not patchable shape.
            continue;
        }

        if (!PGNIFUtil::isShaderPatchableShape(*nif, *shape)) {
            // Skip if not shader patchable shape.
            continue;
        }

        auto* const shader = nif->GetShader(shape);
        const auto textureSet = PGNIFUtil::textureSlots(nif.get(), shape);

        // Loop through each texture slot.
        for (uint32_t slot = 0; slot < numTextureSlots; slot++) {
            std::string texture = StringUtil::utf16toUTF8(textureSet.at(slot));

            if (texture.empty()) {
                // No texture in this slot.
                continue;
            }

            StringUtil::toLowerASCIIFastInPlace(texture); // Lowercase for comparison

            const auto shaderType = shader->GetShaderType();
            PGEnums::TextureType textureType = { };

            // Check to make sure appropriate shaders are set for a given texture.
            const auto* const shaderBSSP = dynamic_cast<nifly::BSShaderProperty*>(shader);
            if (!shaderBSSP) {
                // Not a BSShaderProperty, skip.
                continue;
            }

            switch (static_cast<PGEnums::TextureSlots>(slot)) {
            case PGEnums::TextureSlots::Diffuse:
                // Diffuse check.
                textureType = PGEnums::TextureType::Diffuse;
                break;
            case PGEnums::TextureSlots::Normal:
                // Normal check.
                if (shaderType == nifly::BSLSP_SKINTINT
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map.
                    textureType = PGEnums::TextureType::ModelSpaceNormal;
                    break;
                }

                textureType = PGEnums::TextureType::Normal;
                break;
            case PGEnums::TextureSlots::Glow:
                // Glowmap check.
                if ((shaderType == nifly::BSLSP_GLOWMAP && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_GLOW_MAP))
                    || (shaderType == nifly::BSLSP_DEFAULT
                        && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_UNUSED01))) {
                    // This is an emmissive map (either vanilla glowmap shader or PBR).
                    textureType = PGEnums::TextureType::Emissive;
                    break;
                }

                if (shaderType == nifly::BSLSP_MULTILAYERPARALLAX
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_MULTI_LAYER_PARALLAX)) {
                    // This is a subsurface map.
                    textureType = PGEnums::TextureType::SubsurfaceColor;
                    break;
                }

                if (shaderType == nifly::BSLSP_SKINTINT
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_FACEGEN_RGB_TINT)) {
                    // This is a skin tint map.
                    textureType = PGEnums::TextureType::SkinTint;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::Parallax:
                // Parallax check.
                if ((shaderType == nifly::BSLSP_PARALLAX
                     && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_PARALLAX))) {
                    // This is a height map.
                    textureType = PGEnums::TextureType::Height;
                    break;
                }

                if ((shaderType == nifly::BSLSP_DEFAULT
                     && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_UNUSED01))) {
                    // This is a height map for PBR.
                    textureType = PGEnums::TextureType::HeightPBR;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::Cubemap:
                // Cubemap check.
                if (shaderType == nifly::BSLSP_ENVMAP
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = PGEnums::TextureType::Cubemap;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::EnvMask:
                // Envmap check.
                if (shaderType == nifly::BSLSP_ENVMAP
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_ENVIRONMENT_MAPPING)) {
                    textureType = PGEnums::TextureType::EnvironmentMask;
                    break;
                }

                if (shaderType == nifly::BSLSP_DEFAULT && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_UNUSED01)) {
                    textureType = PGEnums::TextureType::RMAOS;
                    break;
                }

                continue;
            case PGEnums::TextureSlots::MultiLayer:
                // Tint check.
                if (shaderType == nifly::BSLSP_MULTILAYERPARALLAX
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_MULTI_LAYER_PARALLAX)) {
                    if (PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_UNUSED01)) {
                        // 2 layer PBR.
                        textureType = PGEnums::TextureType::CoatNormalRoughness;
                    } else {
                        // Normal multilayer.
                        textureType = PGEnums::TextureType::InnerLayer;
                    }
                    break;
                }

                continue;
            case PGEnums::TextureSlots::Backlight:
                // Backlight check.
                if (shaderType == nifly::BSLSP_MULTILAYERPARALLAX
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_UNUSED01)) {
                    textureType = PGEnums::TextureType::SubsurfacePBR;
                    break;
                }

                if (shaderType == nifly::BSLSP_HAIRTINT
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_BACK_LIGHTING)) {
                    // Hair tint map.
                    textureType = PGEnums::TextureType::HairFlowMap;
                    break;
                }

                if (shaderType == nifly::BSLSP_SKINTINT
                    && PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF1_FACEGEN_RGB_TINT)) {
                    textureType = PGEnums::TextureType::Specular;
                    break;
                }

                if (PGNIFUtil::hasShaderFlag(shaderBSSP, nifly::SLSF2_BACK_LIGHTING)) {
                    textureType = PGEnums::TextureType::Backlight;
                    break;
                }

                continue;
            default:
                textureType = PGEnums::TextureType::Unknown;
            }

            // Record vote (applied by the caller).
            votes.push_back({
                .texture = StringUtil::utf8toUTF16(texture),
                .slot = static_cast<PGEnums::TextureSlots>(slot),
                .type = textureType,
            });
        }
    }

    return true;
}

void PGDirectory::updateUnconfirmedTexturesMap(const std::filesystem::path& path,
                                               const PGEnums::TextureSlots& slot,
                                               const PGEnums::TextureType& type)
{
    // Use mutex to make this thread safe.
    const std::scoped_lock lock(m_unconfirmedTexturesMutex);

    // Check if texture is already in map.
    if (m_unconfirmedTextures.contains(path)) {
        // Texture is present.
        m_unconfirmedTextures[path].slots[slot]++;
        m_unconfirmedTextures[path].types[type]++;
    }
}

void PGDirectory::addToTextureMaps(const std::filesystem::path& path,
                                   const PGEnums::TextureSlots& slot,
                                   const PGEnums::TextureType& type,
                                   const std::unordered_set<PGEnums::TextureAttribute>& attributes)
{
    // Log result.
    Logger::trace(L"Mapping Texture: {} / Slot: {} / Type: {}",
                  path.wstring(),
                  static_cast<size_t>(slot),
                  StringUtil::utf8toUTF16(PGEnums::strFromTexType(type)));

    // Get texture base.
    const auto& base = PGNIFUtil::texBase(path, slot);
    const auto& slotInt = static_cast<size_t>(slot);

    // Add to texture map.
    const PGTypes::PGTexture newPGTexture = { .path = path, .type = type };
    {
        const std::unique_lock lock(m_textureMapsMutex);
        m_textureMaps.at(slotInt)[base].insert(newPGTexture);
    }

    {
        const TextureDetails details = { .type = type, .attributes = attributes };

        const std::unique_lock lock(m_textureTypesMutex);
        m_textureTypes[path] = details;
    }

    // Add shader types to mod given certain types.
    if (type == PGEnums::TextureType::Height) {
        // Parallax.
        if (PGGlobals::isPGMMSet())
            PGGlobals::pgmm()->addShaderToModByFile(path, PGEnums::ShapeShader::VanillaParallax);
    } else if (type == PGEnums::TextureType::ComplexMaterial) {
        // PBR parallax.
        if (PGGlobals::isPGMMSet())
            PGGlobals::pgmm()->addShaderToModByFile(path, PGEnums::ShapeShader::ComplexMaterial);
    } else {
        // Default shader for all other types.
        if (PGGlobals::isPGMMSet())
            PGGlobals::pgmm()->addShaderToModByFile(path, PGEnums::ShapeShader::None);
    }
}

void PGDirectory::updateNifCache(const std::filesystem::path& path,
                                 const std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                             PGPlugin::MeshUseAttributes>>& meshUses)
{
    const std::unique_lock lock(m_meshesMutex);

    if (!m_meshes.contains(path))
        m_meshes[path] = NifCache { };

    m_meshes.at(path).meshUses = meshUses;
}

std::map<std::wstring,
         std::unordered_set<PGTypes::PGTexture,
                            PGTypes::PGTextureHasher>>&
PGDirectory::textureMap(const PGEnums::TextureSlots& slot)
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

const std::map<std::wstring,
               std::unordered_set<PGTypes::PGTexture,
                                  PGTypes::PGTextureHasher>>&
PGDirectory::textureMapConst(const PGEnums::TextureSlots& slot) const
{
    return m_textureMaps.at(static_cast<size_t>(slot));
}

auto PGDirectory::meshes() const -> const std::unordered_map<std::filesystem::path,
                                                             NifCache>&
{
    return m_meshes;
}

const std::unordered_set<std::filesystem::path>& PGDirectory::textures() const { return m_textures; }

const std::vector<std::filesystem::path>& PGDirectory::pbrJSONs() const { return m_pbrJSONs; }

const std::vector<std::filesystem::path>& PGDirectory::lightPlacerJSONs() const { return m_lightPlacerJSONs; }

bool PGDirectory::addTextureAttribute(const std::filesystem::path& path,
                                      const PGEnums::TextureAttribute& attribute)
{
    const std::unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path))
        return m_textureTypes.at(path).attributes.insert(attribute).second;

    return false;
}

bool PGDirectory::removeTextureAttribute(const std::filesystem::path& path,
                                         const PGEnums::TextureAttribute& attribute)
{
    const std::unique_lock lock(m_textureTypesMutex);

    if (m_textureTypes.contains(path))
        return m_textureTypes.at(path).attributes.erase(attribute) > 0;

    return false;
}

bool PGDirectory::hasTextureAttribute(const std::filesystem::path& path,
                                      const PGEnums::TextureAttribute& attribute)
{
    bool result = false;
    {
        const std::shared_lock lock(m_textureTypesMutex);
        if (m_textureTypes.contains(path))
            result = m_textureTypes.at(path).attributes.contains(attribute);
    }

    // Record lookup for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordTextureAttribute(path, attribute, result);

    return result;
}

std::unordered_set<PGEnums::TextureAttribute> PGDirectory::textureAttributes(const std::filesystem::path& path)
{
    std::unordered_set<PGEnums::TextureAttribute> result;
    {
        const std::shared_lock lock(m_textureTypesMutex);
        if (m_textureTypes.contains(path))
            result = m_textureTypes.at(path).attributes;
    }

    // Record lookup for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordTextureAttributes(path, result);

    return result;
}

void PGDirectory::setTextureType(const std::filesystem::path& path,
                                 const PGEnums::TextureType& type)
{
    const std::unique_lock lock(m_textureTypesMutex);
    m_textureTypes[path].type = type;
}

PGEnums::TextureType PGDirectory::textureType(const std::filesystem::path& path)
{
    auto result = PGEnums::TextureType::Unknown;
    {
        const std::shared_lock lock(m_textureTypesMutex);
        if (m_textureTypes.contains(path))
            result = m_textureTypes.at(path).type;
    }

    // Record lookup for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordTextureType(path, result);

    return result;
}
