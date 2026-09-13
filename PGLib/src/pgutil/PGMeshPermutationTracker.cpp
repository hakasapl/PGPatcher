#include "pgutil/PGMeshPermutationTracker.hpp"

#include "PGGlobals.hpp"
#include "PGRunCache.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <fmt/xchar.h>

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

PGMeshPermutationTracker::PGMeshPermutationTracker(const std::filesystem::path& origMeshPath)
    : m_origMeshPath(origMeshPath)

{
    // Check if file exists.
    auto* pgd = PGGlobals::pgd();
    if (!pgd->isFile(origMeshPath))
        throw std::runtime_error("Original mesh path does not exist: " + origMeshPath.string());
}

void PGMeshPermutationTracker::load()
{
    // Load original NIF file.
    const std::vector<std::byte> nifFileData = PGGlobals::pgd()->file(m_origMeshPath);

    // Calculate original CRC32.
    boost::crc_32_type crcBeforeResult { };
    crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
    m_origCrc32 = crcBeforeResult.checksum();

    // Load original NIF.
    m_origNifFile = PGNIFUtil::loadNIFFromBytes(nifFileData, false);

    // Store original shape indices.
    m_origShapeIndices = get3dIndicesSet(&m_origNifFile);
}

void PGMeshPermutationTracker::load(const std::shared_ptr<nifly::NifFile>& origNifFile,
                                    const unsigned long long& origCrc32)
{
    if (origNifFile == nullptr)
        throw std::runtime_error("Original NIF file pointer is null");

    m_origNifFile = *origNifFile;
    m_origCrc32 = origCrc32;

    // Store original shape indices.
    m_origShapeIndices = get3dIndicesSet(&m_origNifFile);
}

nifly::NifFile* PGMeshPermutationTracker::stageMesh()
{
    // Clear any existing staged mesh.
    m_stagedMeshPtr = nullptr;

    // Copy original NIF to staged mesh.
    m_stagedMesh.CopyFrom(m_origNifFile);
    m_stagedMeshPtr = &m_stagedMesh;

    // Store original 3D indices for the staged mesh.
    m_stagedMeshOriginal3DIdx = get3dIndices(m_stagedMeshPtr);

    return m_stagedMeshPtr;
}

void PGMeshPermutationTracker::ignoreBaseMesh() { m_ignoreBaseMesh = true; }

bool PGMeshPermutationTracker::commitMesh(const FormKey& formKey,
                                          bool isWeighted,
                                          const std::unordered_map<unsigned,
                                                                   PGTypes::TextureSet>& altTexResults,
                                          const std::unordered_set<unsigned>& nonAltTexShapes)
{
    if (m_stagedMeshPtr == nullptr) {
        // No staged mesh to commit.
        throw std::runtime_error("No staged mesh to commit");
    }

    // Check if this form key already exists.
    if (m_processedFormKeys.contains(formKey)) {
        // Already exists.
        return false;
    }

    // Add to processed form keys.
    m_processedFormKeys.insert(formKey);

    // Build current->original 3D index map for the staged mesh so comparisons remain stable if
    // patchers deleted shapes and shifted current indices.
    const auto stagedCurrent3DIndices = get3dIndices(m_stagedMeshPtr);
    const auto stagedInverseIdxCorrectionsPatching
        = buildInverseIdxCorrections(stagedCurrent3DIndices, m_stagedMeshOriginal3DIdx);

    // Check if staged mesh is different from all existing output meshes.
    for (size_t outputIdx = 0; outputIdx < m_outputMeshes.size(); outputIdx++) {
        auto& outputMesh = m_outputMeshes.at(outputIdx);
        if (compareMesh(
                m_stagedMesh, outputMesh.second, nonAltTexShapes, false, false, &stagedInverseIdxCorrectionsPatching)) {
            // Mesh is identical to an existing output mesh, do not add.
            outputMesh.first.altTexResults.emplace_back(formKey, altTexResults);

            if (isWeighted && !m_weightProcessedOutputs.contains(outputIdx)) {
                // A weighted plugin use resolved to an output mesh that was created by a non-weighted use, so the.
                // Output still needs its _0/_1 counterpart validated.
                processWeightVariant(outputMesh.second, outputIdx);
                m_weightProcessedOutputs.insert(outputIdx);
            }

            // Clear staged mesh.
            m_stagedMeshPtr = nullptr;
            m_stagedMesh.Clear();

            // No need to continue.
            return false;
        }
    }

    if (m_outputMeshes.empty() && compareMesh(m_stagedMesh, m_origNifFile, nonAltTexShapes)) {
        // Compare with base mesh to make sure we actually made changes.
        // If we are here there is a case where a record requires an unpatched base mesh. To avoid breaking this in the.
        // Future, we ignore base mesh which enforces that the base mesh is not patched.
        ignoreBaseMesh();
        m_stagedMeshPtr = nullptr;
        m_stagedMesh.Clear();

        return false;
    }

    if (isWeighted) {
        // Process weighted variant.
        processWeightVariant(m_stagedMesh, m_outputMeshes.size());
        m_weightProcessedOutputs.insert(m_outputMeshes.size());
    }

    // Add new mesh.
    nifly::NifFile newMesh;
    newMesh.CopyFrom(*m_stagedMeshPtr);

    const MeshResult meshResult = {
        .meshPath = { },
        .altTexResults = { { formKey, altTexResults } },
        .idxCorrections = { },
        .inverseIdxCorrectionsPatching = stagedInverseIdxCorrectionsPatching,
    };

    m_outputMeshes.emplace_back(meshResult, newMesh);

    // Clear staged mesh.
    m_stagedMeshPtr = nullptr;
    m_stagedMesh.Clear();
    m_stagedMeshOriginal3DIdx.clear();

    return true;
}

auto PGMeshPermutationTracker::saveMeshes() -> std::pair<std::vector<MeshResult>,
                                                         std::pair<unsigned long long,
                                                                   unsigned long long>>
{
    auto* pgd = PGGlobals::pgd();

    std::vector<MeshResult> output;
    unsigned long long baseCrc32 = 0;

    // Loop through output meshes.
    for (size_t i = 0; i < m_outputMeshes.size(); i++) {
        size_t curIndex = i;
        if (m_ignoreBaseMesh)
            curIndex++;

        // Get mesh object.
        auto& meshResult = m_outputMeshes.at(i).first;
        auto& mesh = m_outputMeshes.at(i).second;

        // Find new shape indices.
        const auto blocks = get3dIndices(&mesh);
        mesh.PrettySortBlocks();
        const auto newBlocks = get3dIndices(&mesh);

        auto originalShapesFoundTracker = m_origShapeIndices;

        for (const auto& [nifObject, oldIndex3D] : blocks) {
            if (!newBlocks.contains(nifObject)) {
                throw std::runtime_error(
                    "Sorted blocks mesh does not contain an object that was in the unsorted version. "
                    "This should not happen.");
            }

            // Exists after patching, set new index 3d.
            const auto newIndex3D = newBlocks.at(nifObject);

            auto correctedOldIndex3D = oldIndex3D;
            if (meshResult.inverseIdxCorrectionsPatching.contains(oldIndex3D)) {
                correctedOldIndex3D = meshResult.inverseIdxCorrectionsPatching.at(oldIndex3D);
            } else {
                throw std::runtime_error("Staged mesh does not contain an object that was in the ephemeral mesh. "
                                         "This should not happen.");
            }

            if (originalShapesFoundTracker.contains(correctedOldIndex3D)) {
                originalShapesFoundTracker.erase(correctedOldIndex3D);
            } else {
                throw std::runtime_error("Staged mesh does not contain an object that was in the ephemeral mesh. "
                                         "This should not happen.");
            }

            meshResult.idxCorrections[correctedOldIndex3D] = newIndex3D;
        }

        for (const auto& missingShape : originalShapesFoundTracker)
            meshResult.idxCorrections[missingShape] = -1; // shape was removed

        // Get filename of mesh.
        const auto meshRelPath = meshPath(m_origMeshPath, curIndex);
        meshResult.meshPath = meshRelPath;
        const auto meshFilename = pgd->generatedPath() / meshRelPath;
        if (std::filesystem::exists(meshFilename))
            throw std::runtime_error("Output mesh file already exists: " + meshFilename.string());

        // Create directories if required.
        std::filesystem::create_directories(meshFilename.parent_path());

        // Save Mesh file.

        // Write to memory buffer.
        bool saveSuccess = false;
        std::ostringstream buffer(std::ios::binary);
        saveSuccess = (mesh.Save(buffer, { .optimize = false, .sortBlocks = false }) == 0);
        const std::string& data = buffer.str();

        if (curIndex == 0) {
            // Get CRC32.
            boost::crc_32_type crc;
            crc.process_bytes(data.data(), data.size());
            baseCrc32 = crc.checksum();
        }

        // Queue save to file saver.
        PGGlobals::fileSaver().queueTask([data, meshFilename] {
            std::ofstream file(meshFilename, std::ios::binary);
            if (file.is_open()) {
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                file.close();
            }
        });

        if (saveSuccess) {
            if (curIndex == 0)
                Logger::debug("Saved patched base mesh");
            else
                Logger::debug("Saved patched duplicate mesh {}", std::to_string(curIndex));
        } else {
            // A mesh that we were able to open but cannot save will cause issues in-game because it might have.
            // Partially saved.
            Logger::critical(L"Unable to save NIF file {}", meshFilename.wstring());
            return { };
        }

        // Tell PGD that this is a generated file.
        pgd->addGeneratedFile(meshRelPath);

        // Record the output for incremental runs.
        PGRunCache::recordOutputFile(meshRelPath, data.size());

        output.push_back(meshResult);
    }

    if (m_ignoreBaseMesh)
        return { output, { m_origCrc32, 0 } };

    return { output, { m_origCrc32, baseCrc32 } };
}

std::vector<std::pair<std::filesystem::path,
                      std::wstring>>
PGMeshPermutationTracker::validateWeightedVariants()
{
    std::vector<std::pair<std::filesystem::path, std::wstring>> errors;

    const std::scoped_lock lock(s_otherWeightVariantsMutex);
    for (const auto& [key, nifFile] : s_otherWeightVariants) {
        // A mesh being used weighted in one place while its counterpart is never patched as weighted (not used.
        // Weighted in plugins, no changes needed, or file absent) is a valid state. Only error when the counterpart.
        // Was also patched as weighted, meaning the _0/_1 outputs actually diverged.
        const auto otherVariantPath = otherWeightVariant(key.first);
        if (!s_weightVariantProcessedPaths.contains(otherVariantPath.wstring())) {
            Logger::debug(L"Skipping weight variant check for '{}': counterpart '{}' was not patched as weighted",
                          key.first.wstring(),
                          otherVariantPath.wstring());
            continue;
        }

        const auto message
            = fmt::format(L"Weighted mesh variant for '{}' not created. Weight variants (_0 and _1) do not match.",
                          key.first.wstring());
        Logger::error(L"{}", message);
        errors.emplace_back(key.first, message);
    }
    s_otherWeightVariants.clear();
    s_weightVariantProcessedPaths.clear();

    return errors;
}

void PGMeshPermutationTracker::processWeightVariant(const nifly::NifFile& mesh,
                                                    const std::size_t dupIdx)
{
    // Check other weight variant cache.
    const std::scoped_lock lock(s_otherWeightVariantsMutex);
    s_weightVariantProcessedPaths.insert(m_origMeshPath.wstring());

    // Check if other variant exists.
    const auto otherVariantPath = otherWeightVariant(m_origMeshPath);
    if (s_otherWeightVariants.contains({ otherVariantPath, dupIdx })) {
        if (!compareMesh(mesh, s_otherWeightVariants[{ otherVariantPath, dupIdx }], { }, true, true)) {
            // Different from each other, post error.
            Logger::error(L"Weighted mesh variants '{}' and '{}' do not match.",
                          m_origMeshPath.wstring(),
                          otherVariantPath.wstring());
        }

        // Delete from cache to free memory.
        s_otherWeightVariants.erase({ otherVariantPath, dupIdx });
    } else {
        // Add to cache.
        s_otherWeightVariants[{ m_origMeshPath, dupIdx }] = nifly::NifFile();
        s_otherWeightVariants[{ m_origMeshPath, dupIdx }].CopyFrom(mesh);
    }
}

//
// ANY changes in patchers that involve WRITING new properties must be included in the equality operators below.
//

bool PGMeshPermutationTracker::compareMesh(const nifly::NifFile& meshA,
                                           const nifly::NifFile& meshB,
                                           const std::unordered_set<unsigned>& enforceCheckShapeTXSTA,
                                           bool compareAllTXST,
                                           bool checkOnlyWeighted,
                                           const std::unordered_map<int,
                                                                    int>* meshAInverseIdxCorrectionsPatching)
{
    // This should be compared before sorting blocks (sorting blocks should happen last).
    const auto blocksA = comparableBlocks(&meshA);
    const auto blocksB = comparableBlocks(&meshB);

    if (blocksA.size() != blocksB.size()) {
        // Different number of shapes.
        return false;
    }

    const size_t numBlocks = blocksA.size();
    for (size_t i = 0; i < numBlocks; i++) {
        // Check ifthis is a NiParticleSystem.
        const auto* const particleA = dynamic_cast<nifly::NiParticleSystem*>(blocksA.at(i));
        const auto* const particleB = dynamic_cast<nifly::NiParticleSystem*>(blocksB.at(i));
        if ((particleA == nullptr && particleB != nullptr) || (particleA != nullptr && particleB == nullptr))
            return false;

        auto* const shapeA = dynamic_cast<nifly::NiShape*>(blocksA.at(i));
        auto* const shapeB = dynamic_cast<nifly::NiShape*>(blocksB.at(i));
        if ((shapeA == nullptr && shapeB != nullptr) || (shapeA != nullptr && shapeB == nullptr))
            return false;

        if (particleA != nullptr && particleB != nullptr) {
            if (checkOnlyWeighted) {
                // Skip non-weighted checks.
                continue;
            }

            if ((particleA->shaderPropertyRef.IsEmpty() && !particleB->shaderPropertyRef.IsEmpty())
                || (!particleA->shaderPropertyRef.IsEmpty() && particleB->shaderPropertyRef.IsEmpty())) {
                // One has a shader property, the other doesn't.
                return false;
            }
            if (particleA->shaderPropertyRef.IsEmpty() && particleB->shaderPropertyRef.IsEmpty()) {
                // Both don't have a shader property, continue.
                continue;
            }

            // Both are particle systems, get effect shader property if it exists.
            auto* const shaderPropA = meshA.GetHeader().GetBlock(particleA->shaderPropertyRef);
            auto* const shaderPropB = meshB.GetHeader().GetBlock(particleB->shaderPropertyRef);

            const auto* const lightingShaderA = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderPropA);
            const auto* const lightingShaderB = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderPropB);
            if ((lightingShaderA == nullptr && lightingShaderB != nullptr)
                || (lightingShaderA != nullptr && lightingShaderB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch).
                return false;
            }
            // Compare bslightingshader helper.
            if ((lightingShaderA != nullptr && lightingShaderB != nullptr)
                && (!compareBSLightingShaderProperty(*lightingShaderA, *lightingShaderB))) {
                return false;
            }

            const auto* const effectShaderA = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderPropA);
            const auto* const effectShaderB = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderPropB);
            if ((effectShaderA == nullptr && effectShaderB != nullptr)
                || (effectShaderA != nullptr && effectShaderB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch).
                return false;
            }
            // Compare bseffectshader helper.
            if ((effectShaderA != nullptr && effectShaderB != nullptr)
                && (!compareBSEffectShaderProperty(*effectShaderA, *effectShaderB))) {
                return false;
            }

            const auto* const shaderA = dynamic_cast<nifly::BSShaderProperty*>(shaderPropA);
            const auto* const shaderB = dynamic_cast<nifly::BSShaderProperty*>(shaderPropB);
            if ((shaderA == nullptr && shaderB != nullptr) || (shaderA != nullptr && shaderB == nullptr)) {
                // One is a shader, the other is not (block mismatch).
                return false;
            }
            // Compare nishader helper.
            if ((shaderA != nullptr && shaderB != nullptr) && (!compareBSShaderProperty(*shaderA, *shaderB)))
                return false;

        } else if (shapeA != nullptr && shapeB != nullptr) {
            // BSTriShape.
            const auto* const bstrishapeA = dynamic_cast<nifly::BSTriShape*>(shapeA);
            const auto* const bstrishapeB = dynamic_cast<nifly::BSTriShape*>(shapeB);
            if ((bstrishapeA == nullptr && bstrishapeB != nullptr)
                || (bstrishapeA != nullptr && bstrishapeB == nullptr)) {
                // One is a trishape, the other is not (block mismatch).
                return false;
            }

            if (checkOnlyWeighted) {
                // Skip non-weighted checks.
                continue;
            }

            // Compare trishape helper.
            if ((bstrishapeA != nullptr && bstrishapeB != nullptr) && (!compareBSTriShape(*bstrishapeA, *bstrishapeB)))
                return false;

            // NiShape.
            // Compare nishape helper.
            if (!compareNiShape(*shapeA, *shapeB))
                return false;

            // Get shader properties.
            auto* const shaderA = meshA.GetShader(shapeA);
            auto* const shaderB = meshB.GetShader(shapeB);

            // BSLightingShaderProperty.
            const auto* const bslightingA = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderA);
            const auto* const bslightingB = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderB);
            if ((bslightingA == nullptr && bslightingB != nullptr)
                || (bslightingA != nullptr && bslightingB == nullptr)) {
                // One is a lighting shader, the other is not (block mismatch).
                return false;
            }
            // Compare bslightingshader helper.
            if ((bslightingA != nullptr && bslightingB != nullptr)
                && (!compareBSLightingShaderProperty(*bslightingA, *bslightingB))) {
                return false;
            }

            // BSEffectShaderProperty.
            const auto* const bseffectA = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderA);
            const auto* const bseffectB = dynamic_cast<nifly::BSEffectShaderProperty*>(shaderB);
            if ((bseffectA == nullptr && bseffectB != nullptr) || (bseffectA != nullptr && bseffectB == nullptr)) {
                // One is an effect shader, the other is not (block mismatch).
                return false;
            }
            // Compare bseffectshader helper.
            if ((bseffectA != nullptr && bseffectB != nullptr)
                && (!compareBSEffectShaderProperty(*bseffectA, *bseffectB))) {
                return false;
            }

            // NiShader.
            auto* const nishaderA = dynamic_cast<nifly::BSShaderProperty*>(shaderA);
            auto* const nishaderB = dynamic_cast<nifly::BSShaderProperty*>(shaderB);
            if ((nishaderA == nullptr && nishaderB != nullptr) || (nishaderA != nullptr && nishaderB == nullptr)) {
                // One is a shader, the other is not (block mismatch).
                return false;
            }
            // Compare nishader helper.
            if ((nishaderA != nullptr && nishaderB != nullptr) && (!compareBSShaderProperty(*nishaderA, *nishaderB)))
                return false;

            if (nishaderA == nullptr || nishaderB == nullptr)
                continue;

            // BSShaderTextureSet.
            auto* const texSetA = meshA.GetHeader().GetBlock(nishaderA->TextureSetRef());
            auto* const texSetB = meshB.GetHeader().GetBlock(nishaderB->TextureSetRef());
            auto* const bsshsTexSetA = dynamic_cast<nifly::BSShaderTextureSet*>(texSetA);
            auto* const bsshsTexSetB = dynamic_cast<nifly::BSShaderTextureSet*>(texSetB);
            if ((bsshsTexSetA == nullptr && bsshsTexSetB != nullptr)
                || (bsshsTexSetA != nullptr && bsshsTexSetB == nullptr)) {
                // One is a texture set, the other is not (block mismatch).
                return false;
            }

            // Resolve the original 3D index (before patch-time deletions/reordering) for stable.
            // Alternate-texture enforcement.
            auto enforceIdxA = static_cast<unsigned>(i);
            if (meshAInverseIdxCorrectionsPatching != nullptr) {
                const auto foundA = meshAInverseIdxCorrectionsPatching->find(static_cast<int>(i));
                if (foundA != meshAInverseIdxCorrectionsPatching->end() && foundA->second >= 0)
                    enforceIdxA = static_cast<unsigned>(foundA->second);
            }

            const bool enforceTxstCheck = enforceCheckShapeTXSTA.contains(enforceIdxA);
            if (!enforceTxstCheck && !compareAllTXST)
                continue;

            // Compare bsshadertextureset helper.
            if ((bsshsTexSetA != nullptr && bsshsTexSetB != nullptr)
                && (!compareBSShaderTextureSet(*bsshsTexSetA, *bsshsTexSetB))) {
                return false;
            }
        }
    }

    return true;
}

bool PGMeshPermutationTracker::compareBSTriShape(const nifly::BSTriShape& shapeA,
                                                 const nifly::BSTriShape& shapeB)
{
    if (!shapeA.HasVertexColors() && !shapeB.HasVertexColors()) {
        // Nothing to check.
        return true;
    }

    if (shapeA.HasVertexColors() != shapeB.HasVertexColors()) {
        // Only one shape has vertex colors.
        return false;
    }

    const auto vertdataA = shapeA.vertData;
    const auto vertdataB = shapeB.vertData;
    if (vertdataA.size() != vertdataB.size())
        return false;

    const auto numVerts = vertdataA.size();

    for (size_t i = 0; i < numVerts; i++) {
        const auto& vertA = vertdataA.at(i);
        const auto& vertB = vertdataB.at(i);

        if (!std::ranges::equal(vertA.colorData, vertB.colorData))
            return false;
    }

    return true;
}

bool PGMeshPermutationTracker::compareNiShape(const nifly::NiShape& shapeA,
                                              const nifly::NiShape& shapeB)
{
    return shapeA.HasVertexColors() == shapeB.HasVertexColors();
}

bool PGMeshPermutationTracker::compareBSLightingShaderProperty(const nifly::BSLightingShaderProperty& shaderA,
                                                               const nifly::BSLightingShaderProperty& shaderB)
{
    if (shaderA.emissiveColor != shaderB.emissiveColor)
        return false;

    if (shaderA.emissiveMultiple != shaderB.emissiveMultiple)
        return false;

    if (shaderA.alpha != shaderB.alpha)
        return false;

    if (shaderA.glossiness != shaderB.glossiness)
        return false;

    if (shaderA.specularColor != shaderB.specularColor)
        return false;

    if (shaderA.specularStrength != shaderB.specularStrength)
        return false;

    if (shaderA.softlighting != shaderB.softlighting)
        return false;

    if (shaderA.rimlightPower != shaderB.rimlightPower)
        return false;

    if (shaderA.subsurfaceColor.b != shaderB.subsurfaceColor.b || shaderA.subsurfaceColor.g != shaderB.subsurfaceColor.g
        || shaderA.subsurfaceColor.r != shaderB.subsurfaceColor.r) {
        return false;
    }

    if (shaderA.parallaxInnerLayerThickness != shaderB.parallaxInnerLayerThickness)
        return false;

    if (shaderA.parallaxRefractionScale != shaderB.parallaxRefractionScale)
        return false;

    if (shaderA.parallaxInnerLayerTextureScale.u != shaderB.parallaxInnerLayerTextureScale.u
        || shaderA.parallaxInnerLayerTextureScale.v != shaderB.parallaxInnerLayerTextureScale.v) {
        return false;
    }

    return true;
}

bool PGMeshPermutationTracker::compareBSEffectShaderProperty(const nifly::BSEffectShaderProperty& shaderA,
                                                             const nifly::BSEffectShaderProperty& shaderB)
{
    return shaderA.textureClampMode == shaderB.textureClampMode;
}

bool PGMeshPermutationTracker::compareBSShaderProperty(const nifly::BSShaderProperty& shaderA,
                                                       const nifly::BSShaderProperty& shaderB)
{
    if (shaderA.shaderType != shaderB.shaderType)
        return false;

    if (shaderA.shaderFlags1 != shaderB.shaderFlags1)
        return false;

    if (shaderA.shaderFlags2 != shaderB.shaderFlags2)
        return false;

    if (shaderA.environmentMapScale != shaderB.environmentMapScale)
        return false;

    if (shaderA.uvOffset.u != shaderB.uvOffset.u || shaderA.uvOffset.v != shaderB.uvOffset.v)
        return false;

    if (shaderA.uvScale.u != shaderB.uvScale.u || shaderA.uvScale.v != shaderB.uvScale.v)
        return false;

    return true;
}

bool PGMeshPermutationTracker::compareBSShaderTextureSet(nifly::BSShaderTextureSet& texSetA,
                                                         nifly::BSShaderTextureSet& texSetB)
{
    auto texturesA = texSetA.textures;
    auto texturesB = texSetB.textures;
    const auto maxSize = std::max(texturesA.size(), texturesB.size());

    for (uint32_t i = 0; i < maxSize; i++) {
        const bool hasA = i < texturesA.size();
        const bool hasB = i < texturesB.size();

        if (hasA && hasB) {
            if (!StringUtil::asciiFastIEquals(texturesA[i].get(), texturesB[i].get()))
                return false;
        } else if (hasA) {
            if (!texturesA[i].get().empty())
                return false;
        } else { // hasB
            if (!texturesB[i].get().empty())
                return false;
        }
    }
    return true;
}

std::filesystem::path PGMeshPermutationTracker::meshPath(const std::filesystem::path& nifPath,
                                                         const size_t& index)
{
    if (index == 0)
        return nifPath;

    // Different from mesh which means duplicate is needed.
    std::filesystem::path newNIFPath;
    auto it = nifPath.begin();
    newNIFPath /= *it++ / "_pgpatcher_dups" / std::to_wstring(index);
    while (it != nifPath.end())
        newNIFPath /= *it++;

    return newNIFPath;
}

std::vector<nifly::NiObject*> PGMeshPermutationTracker::comparableBlocks(const nifly::NifFile* nif)
{
    if (nif == nullptr)
        throw std::runtime_error("NIF is null");

    // Get 3d indices.
    const auto blocks = get3dIndices(nif);
    std::vector<std::pair<nifly::NiObject*, int>> out;
    out.reserve(blocks.size());
    for (const auto& [nifObject, idx] : blocks)
        out.emplace_back(nifObject, idx);

    // Sort by index3d.
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.second < b.second; });

    // Drop index3d.
    std::vector<nifly::NiObject*> outBlocks;
    outBlocks.reserve(out.size());
    for (const auto& [nifObject, idx] : out)
        outBlocks.push_back(nifObject);

    return outBlocks;
}

std::unordered_map<nifly::NiObject*,
                   int>
PGMeshPermutationTracker::get3dIndices(const nifly::NifFile* nif)
{
    if (nif == nullptr)
        throw std::runtime_error("NIF is null");

    std::vector<nifly::NiObject*> tree;
    nif->GetTree(tree);
    std::unordered_map<nifly::NiObject*, int> blocks;
    int oldIndex3D = 0;
    for (auto& obj : tree) {
        if (dynamic_cast<nifly::NiShape*>(obj) != nullptr) {
            blocks[obj] = oldIndex3D++;
            continue;
        }

        // Other stuff that should increment oldIndex3D.
        if (dynamic_cast<nifly::NiParticleSystem*>(obj) != nullptr) {
            // Particle system, increment index3d.
            blocks[obj] = oldIndex3D;
            oldIndex3D++;
        }
    }

    return blocks;
}

std::unordered_set<int> PGMeshPermutationTracker::get3dIndicesSet(const nifly::NifFile* nif)
{
    if (nif == nullptr)
        throw std::runtime_error("NIF is null");

    std::vector<nifly::NiObject*> tree;
    nif->GetTree(tree);
    std::unordered_set<int> blocks;
    int oldIndex3D = 0;
    for (auto& obj : tree) {
        if (dynamic_cast<nifly::NiShape*>(obj) != nullptr) {
            blocks.insert(oldIndex3D++);
            continue;
        }

        // Other stuff that should increment oldIndex3D.
        if (dynamic_cast<nifly::NiParticleSystem*>(obj) != nullptr) {
            // Particle system, increment index3d.
            blocks.insert(oldIndex3D);
            oldIndex3D++;
        }
    }

    return blocks;
}

std::unordered_map<int,
                   int>
PGMeshPermutationTracker::buildInverseIdxCorrections(const std::unordered_map<nifly::NiObject*,
                                                                              int>& current3DIndices,
                                                     const std::unordered_map<nifly::NiObject*,
                                                                              int>& original3DIndices)
{
    std::unordered_map<int, int> inverseIdxCorrections;
    for (const auto& [nifObject, oldIndex3D] : original3DIndices) {
        if (!current3DIndices.contains(nifObject))
            continue;

        const auto newIndex3D = current3DIndices.at(nifObject);
        inverseIdxCorrections[newIndex3D] = oldIndex3D;
    }

    return inverseIdxCorrections;
}

std::filesystem::path PGMeshPermutationTracker::otherWeightVariant(const std::filesystem::path& nifPath)
{
    // Convert m_origMeshPath to weight slider variant.
    std::filesystem::path weightVariant = nifPath;

    const static std::wstring oneWeightVariant = L"_1.nif";
    const static std::wstring zeroWeightVariant = L"_0.nif";

    const std::wstring nifPathStr = nifPath.wstring();
    if (nifPathStr.ends_with(oneWeightVariant))
        weightVariant = nifPathStr.substr(0, nifPathStr.size() - oneWeightVariant.size()) + zeroWeightVariant;
    else if (nifPathStr.ends_with(zeroWeightVariant))
        weightVariant = nifPathStr.substr(0, nifPathStr.size() - zeroWeightVariant.size()) + oneWeightVariant;

    return weightVariant;
}
