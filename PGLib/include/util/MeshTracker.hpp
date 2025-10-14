#pragma once

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NIFUtil.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Class responsible for tracking all output mesh permutations from a given input mesh
 */
class MeshTracker {
public:
    struct FormKey {
        std::wstring modKey;
        unsigned int formID;
        std::string subMODL;

        auto operator==(const FormKey& other) const -> bool
        {
            return formID == other.formID && modKey == other.modKey && subMODL == other.subMODL;
        }
    };

    struct FormKeyHash {
        auto operator()(const FormKey& key) const -> std::size_t
        {
            const size_t h1 = std::hash<std::wstring> {}(key.modKey);
            const size_t h2 = std::hash<unsigned int> {}(key.formID);
            const size_t h3 = std::hash<std::string> {}(key.subMODL);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct MeshResult {
        std::filesystem::path meshPath; // Path of output mesh for this particular file
        std::vector<std::pair<FormKey, std::unordered_map<unsigned int, NIFUtil::TextureSet>>>
            altTexResults; // Alternate texture results for this mesh
        std::unordered_map<int, int> idxCorrections; // List of index corrections (old, new, shape name)
    };

private:
    std::filesystem::path m_origMeshPath;
    nifly::NifFile m_origNifFile;
    unsigned long long m_origCrc32;
    bool m_baseMeshExists = false;
    bool m_baseMeshAttempted = false;

    std::vector<std::pair<MeshResult, nifly::NifFile>> m_outputMeshes;
    std::unordered_set<FormKey, FormKeyHash> m_processedFormKeys;

    nifly::NifFile m_stagedMesh;
    nifly::NifFile* m_stagedMeshPtr;

    using AltTex3DIndices = std::unordered_set<unsigned int>;

public:
    MeshTracker(const std::filesystem::path& origMeshPath);

    // Plugin mesh staging
    void load();
    void load(const std::shared_ptr<nifly::NifFile>& origNifFile, const unsigned long long& origCrc32);
    auto stageMesh() -> nifly::NifFile*;
    auto commitBaseMesh() -> bool;
    auto commitDupMesh(
        const FormKey& formKey, const std::unordered_map<unsigned int, NIFUtil::TextureSet>& altTexResults) -> bool;

    // Used in cases where no alternate textures exist but the form key should still be tracked
    void addFormKeyForBaseMesh(const FormKey& formKey);

    auto saveMeshes() -> std::pair<std::vector<MeshResult>, std::pair<unsigned long long, unsigned long long>>;

private:
    // Helpers
    static auto compareMesh(const nifly::NifFile& meshA, const nifly::NifFile& meshB, bool compareTXST = false) -> bool;

    static auto compareBSTriShape(const nifly::BSTriShape& shapeA, const nifly::BSTriShape& shapeB) -> bool;

    static auto compareNiShape(const nifly::NiShape& shapeA, const nifly::NiShape& shapeB) -> bool;

    static auto compareBSLightingShaderProperty(
        const nifly::BSLightingShaderProperty& shaderA, const nifly::BSLightingShaderProperty& shaderB) -> bool;

    static auto compareBSEffectShaderProperty(
        const nifly::BSEffectShaderProperty& shaderA, const nifly::BSEffectShaderProperty& shaderB) -> bool;

    static auto compareBSShaderProperty(const nifly::BSShaderProperty& shaderA, const nifly::BSShaderProperty& shaderB)
        -> bool;

    static auto compareBSShaderTextureSet(nifly::BSShaderTextureSet& texSetA, nifly::BSShaderTextureSet& texSetB)
        -> bool;

    static auto getMeshPath(const std::filesystem::path& nifPath, const size_t& index) -> std::filesystem::path;

    static auto getComparableBlocks(const nifly::NifFile* nif) -> std::vector<nifly::NiObject*>;

    static auto get3dIndices(const nifly::NifFile* nif) -> std::unordered_map<nifly::NiObject*, int>;
};
