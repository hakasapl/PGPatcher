#include "util/MeshTracker.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <algorithm>
#include <fstream>

#include "PGGlobals.hpp"
#include "util/CRC32OStream.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"

using namespace std;

MeshTracker::MeshTracker(const std::filesystem::path& origMeshPath)
    : m_origMeshPath(origMeshPath)
    , m_stagedMeshPtr(nullptr)
{
    // Check if file exists
    auto* pgd = PGGlobals::getPGD();
    if (!pgd->isFile(origMeshPath)) {
        throw std::runtime_error("Original mesh path does not exist: " + origMeshPath.string());
    }

    // Load original NIF file
    const vector<std::byte> nifFileData = pgd->getFile(origMeshPath);

    // Calculate original CRC32
    boost::crc_32_type crcBeforeResult {};
    crcBeforeResult.process_bytes(nifFileData.data(), nifFileData.size());
    m_origCrc32 = crcBeforeResult.checksum();

    // Load original NIF
    m_origNifFile = NIFUtil::loadNIFFromBytes(nifFileData, false);
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

auto MeshTracker::commitBaseMesh() -> bool
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

    if (compareMesh(m_stagedMesh, m_origNifFile)) {
        // Mesh was not modified from the base mesh, do nothing
        // Clear staged mesh
        m_stagedMeshPtr = nullptr;
        m_stagedMesh.Clear();

        return false;
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

auto MeshTracker::commitDupMesh(
    const FormKey& formKey, const std::unordered_map<unsigned int, NIFUtil::TextureSet>& altTexResults) -> bool
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
        if (compareMesh(m_stagedMesh, outputMesh.second)) {
            // Mesh is identical to an existing output mesh, do not add
            outputMesh.first.altTexResults.emplace_back(formKey, altTexResults);
            // Clear staged mesh
            m_stagedMeshPtr = nullptr;
            m_stagedMesh.Clear();

            // No need to continue
            return false;
        }
    }

    if (!m_baseMeshExists && compareMesh(m_stagedMesh, m_origNifFile)) {
        // Mesh was not modified from the base mesh, do nothing
        // We only do nothing IF a base mesh doesn't exist, because otherwise it will use the base mesh incorrectly
        // IF a base mesh does exist, then a new mesh that is a duplicate of the original will be created regardless
        // Clear staged mesh
        m_stagedMeshPtr = nullptr;
        m_stagedMesh.Clear();

        return false;
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
        const auto shapes = NIFUtil::getShapesWithBlockIDs(&mesh);
        mesh.PrettySortBlocks();
        const auto newShapes = NIFUtil::getShapesWithBlockIDs(&mesh);

        for (const auto& [nifShape, oldIndex3D] : shapes) {
            if (!newShapes.contains(nifShape)) {
                throw runtime_error("Shape not found in new NIF after patching");
            }

            const auto newIndex3D = newShapes.at(nifShape);
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
        bool saveSuccess = false;
        if (i == 0) {
            // process crc for base mesh only
            ofstream outStream(meshFilename, ios::binary);
            CRC32OStream crcStream(outStream);
            saveSuccess = (mesh.Save(crcStream, { .optimize = false, .sortBlocks = false }) == 0);
            baseCrc32 = crcStream.checksum();
        } else {
            saveSuccess = (mesh.Save(meshFilename, { .optimize = false, .sortBlocks = false }) == 0);
        }

        if (saveSuccess) {
            Logger::debug(L"Saving patched NIF to output");
        } else {
            Logger::error(L"Unable to save NIF file {}", meshFilename.wstring());
            continue;
        }

        // tell PGD that this is a generated file
        pgd->addGeneratedFile(meshRelPath, nullptr);

        output.push_back(meshResult);
    }

    if (m_baseMeshExists) {
        return { output, { m_origCrc32, baseCrc32 } };
    }

    return { output, { m_origCrc32, 0 } };
}

//
// ANY changes in patchers that involve WRITING new properties must be included in the equality operators below
//

auto MeshTracker::compareMesh(const nifly::NifFile& meshA, const nifly::NifFile& meshB) -> bool
{
    // This should be compared before sorting blocks (sorting blocks should happen last)
    // TODO this currently only processes blocks attached to shapes, which isn't always the case, but should be fine for
    // now

    vector<NiShape*> shapesA = NIFUtil::getShapes(&meshA);
    vector<NiShape*> shapesB = NIFUtil::getShapes(&meshB);

    if (shapesA.size() != shapesB.size()) {
        // Different number of shapes
        return false;
    }

    const size_t numBlocks = shapesA.size();
    for (size_t i = 0; i < numBlocks; i++) {
        auto* const shapeA = shapesA.at(i);
        auto* const shapeB = shapesB.at(i);

        // We only check certain blocks that PG will actually modify

        // BSTriShape
        auto* const bstrishapeA = dynamic_cast<nifly::BSTriShape*>(shapeA);
        auto* const bstrishapeB = dynamic_cast<nifly::BSTriShape*>(shapeB);
        if ((bstrishapeA == nullptr && bstrishapeB != nullptr) || (bstrishapeA != nullptr && bstrishapeB == nullptr)) {
            // One is a trishape, the other is not (block mismatch)
            return false;
        }
        if (bstrishapeA != nullptr && bstrishapeB != nullptr) {
            // compare trishape helper
            if (!compareBSTriShape(*bstrishapeA, *bstrishapeB)) {
                return false;
            }
        }

        // NiShape
        auto* const nishapeA = dynamic_cast<nifly::NiShape*>(shapeA);
        auto* const nishapeB = dynamic_cast<nifly::NiShape*>(shapeB);
        if ((nishapeA == nullptr && nishapeB != nullptr) || (nishapeA != nullptr && nishapeB == nullptr)) {
            // One is a shape, the other is not (block mismatch)
            return false;
        }
        if (nishapeA != nullptr && nishapeB != nullptr) {
            // compare nishape helper
            if (!compareNiShape(*nishapeA, *nishapeB)) {
                return false;
            }
        }

        // Get shader properties
        auto* const shaderA = meshA.GetShader(shapeA);
        auto* const shaderB = meshB.GetShader(shapeB);

        // BSLightingShaderProperty
        auto* const bslightingA = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderA);
        auto* const bslightingB = dynamic_cast<nifly::BSLightingShaderProperty*>(shaderB);
        if ((bslightingA == nullptr && bslightingB != nullptr) || (bslightingA != nullptr && bslightingB == nullptr)) {
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
    }

    return true;
}

auto MeshTracker::compareBSTriShape(const nifly::BSTriShape& shapeA, const nifly::BSTriShape& shapeB) -> bool
{
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
