#pragma once

#include "NifFile.hpp"
#include <boost/crc.hpp>
#include <filesystem>
#include <string>

/**
 * @brief Class responsible for tracking all output mesh permutations from a given input mesh
 */
class MeshTracker {
private:
    std::filesystem::path m_origMeshPath;
    nifly::NifFile m_origNifFile;
    unsigned long long m_origCrc32;

    struct FormKey {
        std::wstring modKey;
        unsigned int formID;

        auto operator==(const FormKey& other) const -> bool { return formID == other.formID && modKey == other.modKey; }
    };

    struct FormKeyHash {
        auto operator()(const FormKey& key) const -> std::size_t
        {
            const size_t h1 = std::hash<std::wstring> {}(key.modKey);
            const size_t h2 = std::hash<unsigned int> {}(key.formID);
            return h1 ^ (h2 << 1);
        }
    };

    std::vector<nifly::NifFile> m_outputMeshes;
    std::unordered_map<FormKey, size_t, FormKeyHash> m_formKeyToMeshIdx;

    nifly::NifFile m_stagedMesh;
    nifly::NifFile* m_stagedMeshPtr;

    using AltTex3DIndices = std::unordered_set<unsigned int>;

public:
    MeshTracker(const std::filesystem::path& origMeshPath);

    // Plugin mesh staging
    auto stageMesh() -> nifly::NifFile*;
    auto commitBaseMesh() -> bool;
    auto commitDupMesh(const FormKey& formKey) -> bool;

    // Used in cases where no alternate textures exist but the form key should still be tracked
    void addFormKeyForBaseMesh(const FormKey& formKey);

    struct MeshInfo {
        std::filesystem::path meshPath;
        std::unordered_set<FormKey, FormKeyHash> formKeys;
        std::vector<std::tuple<int, int, std::string>> idxCorrections;
    };

    auto saveMeshes() -> std::pair<std::vector<MeshInfo>, std::pair<unsigned long long, unsigned long long>>;

private:
    // Helpers
    static auto compareMesh(const nifly::NifFile& meshA, const nifly::NifFile& meshB) -> bool;

    static auto compareBSTriShape(const nifly::BSTriShape& shapeA, const nifly::BSTriShape& shapeB) -> bool;

    static auto compareNiShape(const nifly::NiShape& shapeA, const nifly::NiShape& shapeB) -> bool;

    static auto compareBSLightingShaderProperty(
        const nifly::BSLightingShaderProperty& shaderA, const nifly::BSLightingShaderProperty& shaderB) -> bool;

    static auto compareBSEffectShaderProperty(
        const nifly::BSEffectShaderProperty& shaderA, const nifly::BSEffectShaderProperty& shaderB) -> bool;

    static auto compareBSShaderProperty(const nifly::BSShaderProperty& shaderA, const nifly::BSShaderProperty& shaderB)
        -> bool;

    static auto getMeshPath(const std::filesystem::path& nifPath, const size_t& index) -> std::filesystem::path;
};
