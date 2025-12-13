#include "util/MeshTracker.hpp"

#include "PGGlobals.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/crc.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

MeshTracker::MeshTracker(const std::filesystem::path& origMeshPath)
    : m_origMeshPath(origMeshPath)
    , m_origCrc32(0)
    , m_stagedMeshPtr(nullptr)
{
    // Check if file exists
    auto* pgd = PGGlobals::getPGD();
    if (!pgd->isFile(origMeshPath)) {
        throw std::runtime_error("Original mesh path does not exist: " + origMeshPath.string());
    }
}

void MeshTracker::load()
{
    // Load original NIF file
    const vector<std::byte> nifFileData = PGGlobals::getPGD()->getFile(m_origMeshPath);

    // Calculate original CRC32
    boost::crc_32_type crcBeforeResult {};
    crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
    m_origCrc32 = crcBeforeResult.checksum();

    // Load original NIF
    m_origNifFile = NIFUtil::loadNIFFromBytes(nifFileData, false);
}

void MeshTracker::load(const std::shared_ptr<nifly::NifFile>& origNifFile, const unsigned long long& origCrc32)
{
    if (origNifFile == nullptr) {
        throw std::runtime_error("Original NIF file pointer is null");
    }

    m_origNifFile = *origNifFile;
    m_origCrc32 = origCrc32;
}

auto MeshTracker::stageMesh() -> nifly::NifFile*
{
    // Clear any existing staged mesh
    m_stagedMeshPtr = nullptr;

    // Copy original NIF to staged mesh
    m_stagedMesh.CopyFrom(m_origNifFile);
    m_stagedMeshPtr = &m_stagedMesh;

    return m_stagedMeshPtr;
}

auto MeshTracker::commitBaseMesh(bool isWeighted) -> bool
{
    if (!m_outputMeshes.empty()) {
        // Base mesh already committed
        throw std::runtime_error("Base mesh already committed, cannot commit again");
    }

    if (m_stagedMeshPtr == nullptr) {
        // No staged mesh to commit
        throw std::runtime_error("No staged mesh to commit as base mesh");
    }

    m_baseMeshAttempted = true;

    if (compareMesh(m_stagedMesh, m_origNifFile, {}, true)) {
        // Mesh was not modified from the base mesh, do nothing
        // Clear staged mesh
        m_stagedMeshPtr = nullptr;
        m_stagedMesh.Clear();

        return false;
    }

    if (isWeighted) {
        // Process weighted variant
        processWeightVariant();
    }

    const MeshResult meshResult = { .meshPath = {}, .altTexResults = {}, .idxCorrections = {} };

    nifly::NifFile newMesh;
    newMesh.CopyFrom(*m_stagedMeshPtr);

    if (m_outputMeshes.empty()) {
        m_outputMeshes.emplace_back(meshResult, newMesh);
    } else {
        m_outputMeshes.at(0) = { meshResult, newMesh };
    }

    // Clear staged mesh
    m_stagedMeshPtr = nullptr;
    m_stagedMesh.Clear();
    m_baseMeshExists = true;

    return true;
}

auto MeshTracker::commitDupMesh(const FormKey& formKey, bool isWeighted,
    const std::unordered_map<unsigned int, NIFUtil::TextureSet>& altTexResults,
    const std::unordered_set<unsigned int>& nonAltTexShapes) -> bool
{
    if (m_stagedMeshPtr == nullptr) {
        // No staged mesh to commit
        throw std::runtime_error("No staged mesh to commit as duplicate mesh");
    }

    if (!m_baseMeshAttempted) {
        throw std::runtime_error("Base mesh must be committed before committing duplicate meshes");
    }

    // Check if this form key already exists
    if (m_processedFormKeys.contains(formKey)) {
        // Already exists
        return false;
    }

    // Add to processed form keys
    m_processedFormKeys.insert(formKey);

    // Check if staged mesh is different from all existing output meshes
    for (auto& outputMesh : m_outputMeshes) {
        if (compareMesh(m_stagedMesh, outputMesh.second, nonAltTexShapes)) {
            // Mesh is identical to an existing output mesh, do not add
            outputMesh.first.altTexResults.emplace_back(formKey, altTexResults);
            // Clear staged mesh
            m_stagedMeshPtr = nullptr;
            m_stagedMesh.Clear();

            // No need to continue
            return false;
        }
    }

    if (!m_baseMeshExists && compareMesh(m_stagedMesh, m_origNifFile, nonAltTexShapes)) {
        // Mesh was not modified from the base mesh, do nothing
        // We only do nothing IF a base mesh doesn't exist, because otherwise it will use the base mesh incorrectly
        // IF a base mesh does exist, then a new mesh that is a duplicate of the original will be created regardless
        // Clear staged mesh
        m_stagedMeshPtr = nullptr;
        m_stagedMesh.Clear();

        return false;
    }

    if (isWeighted) {
        // Process weighted variant
        processWeightVariant();
    }

    // Add new mesh
    nifly::NifFile newMesh;
    newMesh.CopyFrom(*m_stagedMeshPtr);

    const MeshResult meshResult
        = { .meshPath = {}, .altTexResults = { { formKey, altTexResults } }, .idxCorrections = {} };

    if (m_outputMeshes.empty()) {
        // fill base mesh with blank
        m_outputMeshes.emplace_back(MeshResult {}, m_origNifFile);
    }

    m_outputMeshes.emplace_back(meshResult, newMesh);

    // Clear staged mesh
    m_stagedMeshPtr = nullptr;
    m_stagedMesh.Clear();

    return true;
}

void MeshTracker::addFormKeyForBaseMesh(const FormKey& formKey)
{
    if (!m_baseMeshExists) {
        // We don't care about tracking this if and only if no base mesh exists
        return;
    }

    // Add form key for base mesh
    m_outputMeshes.at(0).first.altTexResults.emplace_back(
        formKey, std::unordered_map<unsigned int, NIFUtil::TextureSet> {});
}

auto MeshTracker::saveMeshes() -> pair<vector<MeshResult>, pair<unsigned long long, unsigned long long>>
{
    auto* pgd = PGGlobals::getPGD();

    vector<MeshResult> output;
    unsigned long long baseCrc32 = 0;

    // loop through output meshes
    for (size_t i = 0; i < m_outputMeshes.size(); i++) {
        // Skip base mesh if it doesn't exist
        if (i == 0 && !m_baseMeshExists) {
            continue;
        }

        // Get mesh object
        auto& meshResult = m_outputMeshes.at(i).first;
        auto& mesh = m_outputMeshes.at(i).second;

        // Find new shape indices
        const auto blocks = get3dIndices(&mesh);
        mesh.PrettySortBlocks();
        const auto newBlocks = get3dIndices(&mesh);

        for (const auto& [nifObject, oldIndex3D] : blocks) {
            int newIndex3D = -1;
            if (newBlocks.contains(nifObject)) {
                // exists after patching, set new index 3d
                newIndex3D = newBlocks.at(nifObject);
            }

            meshResult.idxCorrections[oldIndex3D] = newIndex3D;
        }

        // Get filename of mesh
        const auto meshRelPath = getMeshPath(m_origMeshPath, i);
        meshResult.meshPath = meshRelPath;
        const auto meshFilename = pgd->getGeneratedPath() / meshRelPath;
        if (filesystem::exists(meshFilename)) {
            throw std::runtime_error("Output mesh file already exists: " + meshFilename.string());
        }

        // Create directories if required
        filesystem::create_directories(meshFilename.parent_path());

        // Save Mesh file

        // Write to memory buffer
        bool saveSuccess = false;
        ostringstream buffer(std::ios::binary);
        saveSuccess = (mesh.Save(buffer, { .optimize = false, .sortBlocks = false }) == 0);
        const string& data = buffer.str();

        if (i == 0) {
            // get CRC32
            boost::crc_32_type crc;
            crc.process_bytes(data.data(), data.size());
            baseCrc32 = crc.checksum();
        }

        // queue save to file saver
        PGGlobals::getFileSaver().queueTask([data, meshFilename]() -> void {
            std::ofstream file(meshFilename, std::ios::binary);
            if (file.is_open()) {
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                file.close();
            }
        });

        if (saveSuccess) {
            if (i == 0) {
                Logger::debug("Saved patched base mesh");
            } else {
                Logger::debug("Saved patched duplicate mesh {}", to_string(i));
            }
        } else {
            // A mesh that we were able to open but cannot save will cause issues in-game because it might have
            // partially saved
            Logger::critical(L"Unable to save NIF file {}", meshFilename.wstring());
            return {};
        }

        // tell PGD that this is a generated file
        pgd->addGeneratedFile(meshRelPath);

        output.push_back(meshResult);
    }

    if (m_baseMeshExists) {
        return { output, { m_origCrc32, baseCrc32 } };
    }

    return { output, { m_origCrc32, 0 } };
}

void MeshTracker::validateWeightedVariants()
{
    const std::scoped_lock lock(s_otherWeightVariantsMutex);
    for (const auto& [key, nifFile] : s_otherWeightVariants) {
        Logger::error(L"Weighted mesh variant for '{}' was not created. This is an issue with the original plugins and "
                      L"can cause CTDs.",
            key.first.wstring());
    }
    s_otherWeightVariants.clear();
}

void MeshTracker::processWeightVariant()
{
    // Check other weight variant cache
    const std::scoped_lock lock(s_otherWeightVariantsMutex);
    const auto dupIdx = m_outputMeshes.size();
    // check if other variant exists
    const auto otherVariantPath = getOtherWeightVariant(m_origMeshPath);
    if (s_otherWeightVariants.contains({ otherVariantPath, dupIdx })) {
        if (!compareMesh(m_stagedMesh, s_otherWeightVariants[{ otherVariantPath, dupIdx }], {}, true, true)) {
            // different from each other, post error
            Logger::error(L"Weighted mesh variant for '{}' differs from other weight variant '{}'. This is an "
                          L"issue with the original models or bad pbr json definitions and can cause CTDs.",
                m_origMeshPath.wstring(), otherVariantPath.wstring());
        }

        // delete from cache to free memory
        s_otherWeightVariants.erase({ otherVariantPath, dupIdx });
    } else {
        // add to cache
        s_otherWeightVariants[{ m_origMeshPath, dupIdx }] = nifly::NifFile();
        s_otherWeightVariants[{ m_origMeshPath, dupIdx }].CopyFrom(m_stagedMesh);
    }
}

//
// ANY changes in patchers that involve WRITING new properties must be included in the equality operators below
//

auto MeshTracker::compareMesh(const nifly::NifFile& meshA, const nifly::NifFile& meshB,
    const std::unordered_set<unsigned int>& enforceCheckShapeTXSTA, bool compareAllTXST, bool skipVertCheck) -> bool
{
    // This should be compared before sorting blocks (sorting blocks should happen last)
    const auto blocksA = getComparableBlocks(&meshA);
    const auto blocksB = getComparableBlocks(&meshB);

    if (blocksA.size() != blocksB.size()) {
        // Different number of shapes
        return false;
    }

    const size_t numBlocks = blocksA.size();
    for (size_t i = 0; i < numBlocks; i++) {
        // Check ifthis is a NiParticleSystem
        auto* const particleA = dynamic_cast<nifly::NiParticleSystem*>(blocksA.at(i));
        auto* const particleB = dynamic_cast<nifly::NiParticleSystem*>(blocksB.at(i));
        if ((particleA == nullptr && particleB != nullptr) || (particleA != nullptr && particleB == nullptr)) {
            return false;
        }

        auto* const shapeA = dynamic_cast<NiShape*>(blocksA.at(i));
        auto* const shapeB = dynamic_cast<NiShape*>(blocksB.at(i));
        if ((shapeA == nullptr && shapeB != nullptr) || (shapeA != nullptr && shapeB == nullptr)) {
            return false;
        }

        if (particleA != nullptr && particleB != nullptr) {
            if ((particleA->shaderPropertyRef.IsEmpty() && !particleB->shaderPropertyRef.IsEmpty())
                || (!particleA->shaderPropertyRef.IsEmpty() && particleB->shaderPropertyRef.IsEmpty())) {
                // One has a shader property, the other doesn't
                return false;
            }
            if (particleA->shaderPropertyRef.IsEmpty() && particleB->shaderPropertyRef.IsEmpty()) {
                // Both don't have a shader property, continue
                continue;
            }

            // Both are particle systems, get effect shader property if it exists
            auto* const shaderPropA = meshA.GetHeader().GetBlock(particleA->shaderPropertyRef);
            auto* const shaderPropB = meshB.GetHeader().GetBlock(particleB->shaderPropertyRef);

            auto* const lightingShaderA = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderPropA);
            auto* const lightingShaderB = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderPropB);
            if ((lightingShaderA == nullptr && lightingShaderB != nullptr)
                || (lightingShaderA != nullptr && lightingShaderB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch)
                return false;
            }
            if (lightingShaderA != nullptr && lightingShaderB != nullptr) {
                // compare bslightingshader helper
                if (!compareBSLightingShaderProperty(*lightingShaderA, *lightingShaderB)) {
                    return false;
                }
            }

            auto* const effectShaderA = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderPropA);
            auto* const effectShaderB = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderPropB);
            if ((effectShaderA == nullptr && effectShaderB != nullptr)
                || (effectShaderA != nullptr && effectShaderB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch)
                return false;
            }
            if (effectShaderA != nullptr && effectShaderB != nullptr) {
                // compare bseffectshader helper
                if (!compareBSEffectShaderProperty(*effectShaderA, *effectShaderB)) {
                    return false;
                }
            }

            auto* const shaderA = dynamic_cast<nifly::BSShaderProperty*>(shaderPropA);
            auto* const shaderB = dynamic_cast<nifly::BSShaderProperty*>(shaderPropB);
            if ((shaderA == nullptr && shaderB != nullptr) || (shaderA != nullptr && shaderB == nullptr)) {
                // One is a shader, the other is not (block mismatch)
                return false;
            }
            if (shaderA != nullptr && shaderB != nullptr) {
                // compare nishader helper
                if (!compareBSShaderProperty(*shaderA, *shaderB)) {
                    return false;
                }
            }
        } else if (shapeA != nullptr && shapeB != nullptr) {
            // BSTriShape
            auto* const bstrishapeA = dynamic_cast<nifly::BSTriShape*>(shapeA);
            auto* const bstrishapeB = dynamic_cast<nifly::BSTriShape*>(shapeB);
            if ((bstrishapeA == nullptr && bstrishapeB != nullptr)
                || (bstrishapeA != nullptr && bstrishapeB == nullptr)) {
                // One is a trishape, the other is not (block mismatch)
                return false;
            }
            if (bstrishapeA != nullptr && bstrishapeB != nullptr) {
                // compare trishape helper
                if (!skipVertCheck && !compareBSTriShape(*bstrishapeA, *bstrishapeB)) {
                    return false;
                }
            }

            // NiShape
            // compare nishape helper
            if (!compareNiShape(*shapeA, *shapeB)) {
                return false;
            }

            // Get shader properties
            auto* const shaderA = meshA.GetShader(shapeA);
            auto* const shaderB = meshB.GetShader(shapeB);

            // BSLightingShaderProperty
            auto* const bslightingA = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderA);
            auto* const bslightingB = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderB);
            if ((bslightingA == nullptr && bslightingB != nullptr)
                || (bslightingA != nullptr && bslightingB == nullptr)) {
                // One is a lighting shader, the other is not (block mismatch)
                return false;
            }
            if (bslightingA != nullptr && bslightingB != nullptr) {
                // compare bslightingshader helper
                if (!compareBSLightingShaderProperty(*bslightingA, *bslightingB)) {
                    return false;
                }
            }

            // BSEffectShaderProperty
            auto* const bseffectA = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderA);
            auto* const bseffectB = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderB);
            if ((bseffectA == nullptr && bseffectB != nullptr) || (bseffectA != nullptr && bseffectB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch)
                return false;
            }
            if (bseffectA != nullptr && bseffectB != nullptr) {
                // compare bseffectshader helper
                if (!compareBSEffectShaderProperty(*bseffectA, *bseffectB)) {
                    return false;
                }
            }

            // NiShader
            auto* const nishaderA = dynamic_cast<nifly::BSShaderProperty*>(shaderA);
            auto* const nishaderB = dynamic_cast<nifly::BSShaderProperty*>(shaderB);
            if ((nishaderA == nullptr && nishaderB != nullptr) || (nishaderA != nullptr && nishaderB == nullptr)) {
                // One is a shader, the other is not (block mismatch)
                return false;
            }
            if (nishaderA != nullptr && nishaderB != nullptr) {
                // compare nishader helper
                if (!compareBSShaderProperty(*nishaderA, *nishaderB)) {
                    return false;
                }
            }

            if (nishaderA == nullptr || nishaderB == nullptr) {
                continue;
            }

            // BSShaderTextureSet
            auto* const texSetA = meshA.GetHeader().GetBlock(nishaderA->TextureSetRef());
            auto* const texSetB = meshB.GetHeader().GetBlock(nishaderB->TextureSetRef());
            auto* const bsshsTexSetA = dynamic_cast<nifly::BSShaderTextureSet*>(texSetA);
            auto* const bsshsTexSetB = dynamic_cast<nifly::BSShaderTextureSet*>(texSetB);
            if ((bsshsTexSetA == nullptr && bsshsTexSetB != nullptr)
                || (bsshsTexSetA != nullptr && bsshsTexSetB == nullptr)) {
                // One is a texture set, the other is not (block mismatch)
                return false;
            }

            // get txst block id
            const auto shapeBlockIdA = meshA.GetBlockID(shapeA);
            const bool enforceTxstCheck = enforceCheckShapeTXSTA.contains(shapeBlockIdA);
            if (!enforceTxstCheck && !compareAllTXST) {
                continue;
            }

            if (bsshsTexSetA != nullptr && bsshsTexSetB != nullptr) {
                // compare bsshadertextureset helper
                if (!compareBSShaderTextureSet(*bsshsTexSetA, *bsshsTexSetB)) {
                    return false;
                }
            }
        }
    }

    return true;
}

auto MeshTracker::compareBSTriShape(const nifly::BSTriShape& shapeA, const nifly::BSTriShape& shapeB) -> bool
{
    if (!shapeA.HasVertexColors() && !shapeB.HasVertexColors()) {
        // nothing to check
        return true;
    } else if (shapeA.HasVertexColors() != shapeB.HasVertexColors()) {
        // only one shape has vertex colors
        return false;
    }

    const auto vertdataA = shapeA.vertData;
    const auto vertdataB = shapeB.vertData;
    if (vertdataA.size() != vertdataB.size()) {
        return false;
    }

    const auto numVerts = vertdataA.size();

    for (size_t i = 0; i < numVerts; i++) {
        const auto& vertA = vertdataA.at(i);
        const auto& vertB = vertdataB.at(i);

        if (!std::ranges::equal(vertA.colorData, vertB.colorData)) {
            return false;
        }
    }

    return true;
}

auto MeshTracker::compareNiShape(const nifly::NiShape& shapeA, const nifly::NiShape& shapeB) -> bool
{
    return shapeA.HasVertexColors() == shapeB.HasVertexColors();
}

auto MeshTracker::compareBSLightingShaderProperty(
    const nifly::BSLightingShaderProperty& shaderA, const nifly::BSLightingShaderProperty& shaderB) -> bool
{
    if (shaderA.emissiveColor != shaderB.emissiveColor) {
        return false;
    }

    if (shaderA.emissiveMultiple != shaderB.emissiveMultiple) {
        return false;
    }

    if (shaderA.alpha != shaderB.alpha) {
        return false;
    }

    if (shaderA.glossiness != shaderB.glossiness) {
        return false;
    }

    if (shaderA.specularColor != shaderB.specularColor) {
        return false;
    }

    if (shaderA.specularStrength != shaderB.specularStrength) {
        return false;
    }

    if (shaderA.softlighting != shaderB.softlighting) {
        return false;
    }

    if (shaderA.rimlightPower != shaderB.rimlightPower) {
        return false;
    }

    if (shaderA.subsurfaceColor.b != shaderB.subsurfaceColor.b || shaderA.subsurfaceColor.g != shaderB.subsurfaceColor.g
        || shaderA.subsurfaceColor.r != shaderB.subsurfaceColor.r) {
        return false;
    }

    if (shaderA.parallaxInnerLayerThickness != shaderB.parallaxInnerLayerThickness) {
        return false;
    }

    if (shaderA.parallaxRefractionScale != shaderB.parallaxRefractionScale) {
        return false;
    }

    if (shaderA.parallaxInnerLayerTextureScale.u != shaderB.parallaxInnerLayerTextureScale.u
        || shaderA.parallaxInnerLayerTextureScale.v != shaderB.parallaxInnerLayerTextureScale.v) {
        return false;
    }

    return true;
}

auto MeshTracker::compareBSEffectShaderProperty(
    const nifly::BSEffectShaderProperty& shaderA, const nifly::BSEffectShaderProperty& shaderB) -> bool
{
    return shaderA.textureClampMode == shaderB.textureClampMode;
}

auto MeshTracker::compareBSShaderProperty(
    const nifly::BSShaderProperty& shaderA, const nifly::BSShaderProperty& shaderB) -> bool
{
    if (shaderA.shaderType != shaderB.shaderType) {
        return false;
    }

    if (shaderA.shaderFlags1 != shaderB.shaderFlags1) {
        return false;
    }

    if (shaderA.shaderFlags2 != shaderB.shaderFlags2) {
        return false;
    }

    if (shaderA.environmentMapScale != shaderB.environmentMapScale) {
        return false;
    }

    if (shaderA.uvOffset.u != shaderB.uvOffset.u || shaderA.uvOffset.v != shaderB.uvOffset.v) {
        return false;
    }

    if (shaderA.uvScale.u != shaderB.uvScale.u || shaderA.uvScale.v != shaderB.uvScale.v) {
        return false;
    }

    return true;
}

auto MeshTracker::compareBSShaderTextureSet(nifly::BSShaderTextureSet& texSetA, nifly::BSShaderTextureSet& texSetB)
    -> bool
{
    auto texturesA = texSetA.textures;
    auto texturesB = texSetB.textures;
    const auto maxSize = std::max(texturesA.size(), texturesB.size());

    for (uint32_t i = 0; i < maxSize; i++) {
        const bool hasA = i < texturesA.size();
        const bool hasB = i < texturesB.size();

        if (hasA && hasB) {
            if (!ParallaxGenUtil::asciiFastIEquals(texturesA[i].get(), texturesB[i].get())) {
                return false;
            }
        } else if (hasA) {
            if (!texturesA[i].get().empty()) {
                return false;
            }
        } else { // hasB
            if (!texturesB[i].get().empty()) {
                return false;
            }
        }
    }
    return true;
}

auto MeshTracker::getMeshPath(const std::filesystem::path& nifPath, const size_t& index) -> std::filesystem::path
{
    if (index == 0) {
        return nifPath;
    }

    // Different from mesh which means duplicate is needed
    filesystem::path newNIFPath;
    auto it = nifPath.begin();
    newNIFPath /= *it++ / "_pgpatcher_dups" / to_wstring(index);
    while (it != nifPath.end()) {
        newNIFPath /= *it++;
    }

    return newNIFPath;
}

auto MeshTracker::getComparableBlocks(const nifly::NifFile* nif) -> vector<nifly::NiObject*>
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    // get 3d indices
    const auto blocks = get3dIndices(nif);
    vector<pair<NiObject*, int>> out;
    out.reserve(blocks.size());
    for (const auto& [nifObject, idx] : blocks) {
        out.emplace_back(nifObject, idx);
    }

    // sort by index3d
    std::ranges::sort(out, [](const auto& a, const auto& b) -> auto { return a.second < b.second; });

    // drop index3d
    vector<NiObject*> outBlocks;
    outBlocks.reserve(out.size());
    for (const auto& [nifObject, idx] : out) {
        outBlocks.push_back(nifObject);
    }

    return outBlocks;
}

auto MeshTracker::get3dIndices(const nifly::NifFile* nif) -> unordered_map<nifly::NiObject*, int>
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    vector<NiObject*> tree;
    nif->GetTree(tree);
    unordered_map<NiObject*, int> blocks;
    int oldIndex3D = 0;
    for (auto& obj : tree) {
        if (dynamic_cast<NiShape*>(obj) != nullptr) {
            blocks[obj] = oldIndex3D++;
            continue;
        }

        // other stuff that should increment oldIndex3D
        if (dynamic_cast<NiParticleSystem*>(obj) != nullptr) {
            // Particle system, increment index3d
            blocks[obj] = oldIndex3D;
            oldIndex3D++;
        }
    }

    return blocks;
}

auto MeshTracker::getOtherWeightVariant(const std::filesystem::path& nifPath) -> std::filesystem::path
{
    // convert m_origMeshPath to weight slider variant
    std::filesystem::path weightVariant = nifPath;

    const static wstring oneWeightVariant = L"_1.nif";
    const static wstring zeroWeightVariant = L"_0.nif";

    const std::wstring nifPathStr = nifPath.wstring();
    if (nifPathStr.ends_with(oneWeightVariant)) {
        weightVariant = nifPathStr.substr(0, nifPathStr.size() - oneWeightVariant.size())
            + zeroWeightVariant;
    } else if (nifPathStr.ends_with(zeroWeightVariant)) {
        weightVariant = nifPathStr.substr(0, nifPathStr.size() - zeroWeightVariant.size())
            + oneWeightVariant;
    }

    return weightVariant;
}
