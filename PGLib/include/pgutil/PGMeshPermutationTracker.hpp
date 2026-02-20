#pragma once

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include "pgutil/PGNIFUtil.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Class responsible for tracking all output mesh permutations from a given input mesh
 */
class PGMeshPermutationTracker {
public:
    struct FormKey {
        /// @brief Name of the mod file (plugin) that owns this form (e.g., L"Skyrim.esm").
        std::wstring modKey;
        /// @brief Numeric form ID of the record referencing this mesh.
        unsigned int formID;
        /// @brief Sub-model path within the record (empty for the primary model).
        std::string subMODL;

        auto operator==(const FormKey& other) const -> bool
        {
            return formID == other.formID && modKey == other.modKey && subMODL == other.subMODL;
        }
    };

    /**
     * @brief Hash functor for FormKey, enabling use as an unordered_map/unordered_set key.
     */
    struct FormKeyHash {
        /**
         * @brief Computes a combined hash from the mod key, form ID, and sub-model path.
         *
         * @param key The FormKey to hash.
         * @return Combined hash value.
         */
        auto operator()(const FormKey& key) const -> std::size_t
        {
            const size_t h1 = std::hash<std::wstring> {}(key.modKey);
            const size_t h2 = std::hash<unsigned int> {}(key.formID);
            const size_t h3 = std::hash<std::string> {}(key.subMODL);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct MeshResult {
        /// @brief Relative path (within the data directory) of the saved output mesh file.
        std::filesystem::path meshPath;
        /// @brief Alternate texture results keyed by FormKey; each maps shape index to a TextureSet.
        std::vector<std::pair<FormKey, std::unordered_map<unsigned int, PGTypes::TextureSet>>>
            altTexResults;
        /// @brief Index corrections mapping old 3D block indices to new indices after sorting.
        std::unordered_map<int, int> idxCorrections;
    };

private:
    std::filesystem::path m_origMeshPath;
    nifly::NifFile m_origNifFile;
    unsigned long long m_origCrc32;
    bool m_ignoreBaseMesh = false;

    std::vector<std::pair<MeshResult, nifly::NifFile>> m_outputMeshes;
    std::unordered_set<FormKey, FormKeyHash> m_processedFormKeys;

    nifly::NifFile m_stagedMesh;
    nifly::NifFile* m_stagedMeshPtr;

    using AltTex3DIndices = std::unordered_set<unsigned int>;

    struct PathSizeHash {
        auto operator()(const std::pair<std::filesystem::path,
                                        size_t>& key) const noexcept -> size_t
        {
            const size_t h1 = std::hash<std::wstring> {}(key.first.wstring());
            const size_t h2 = std::hash<size_t> {}(key.second);

            // standard hash combine
            return h1
                ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2)); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
        }
    };
    static inline std::mutex s_otherWeightVariantsMutex;
    static inline std::unordered_map<std::pair<std::filesystem::path, size_t>, nifly::NifFile, PathSizeHash>
        s_otherWeightVariants;

public:
    /**
     * @brief Constructs a tracker for the given original mesh path.
     *
     * @param origMeshPath Relative path (within the data directory) to the source NIF file.
     * @throws std::runtime_error if the file does not exist in the directory.
     */
    PGMeshPermutationTracker(const std::filesystem::path& origMeshPath);

    // Plugin mesh staging
    /**
     * @brief Loads the original NIF file and computes its CRC32 from the game directory.
     */
    void load();

    /**
     * @brief Loads the original NIF from an already-opened shared NifFile and a pre-computed CRC32.
     *
     * @param origNifFile Shared pointer to the pre-loaded NifFile.
     * @param origCrc32 CRC32 checksum of the original NIF file bytes.
     * @throws std::runtime_error if origNifFile is null.
     */
    void load(const std::shared_ptr<nifly::NifFile>& origNifFile,
              const unsigned long long& origCrc32);

    /**
     * @brief Creates a fresh copy of the original NIF as the working staged mesh.
     *
     * @return Pointer to the staged NifFile ready for modification.
     */
    auto stageMesh() -> nifly::NifFile*;

    /**
     * @brief Marks that the unmodified base mesh should not be written to disk even when no patches are needed.
     */
    void ignoreBaseMesh();

    /**
     * @brief Commits the current staged mesh as a new output permutation for the given form key.
     *
     * Compares the staged mesh against all existing output meshes (and the original) to avoid
     * duplicates. If an identical mesh already exists, the form key is appended to it instead.
     *
     * @param formKey The plugin form key that references this mesh permutation.
     * @param isWeighted Whether the mesh uses a weighted (body) variant requiring _1/_0 counterpart handling.
     * @param altTexResults Map from shape index to TextureSet for alternate texture overrides.
     * @param nonAltTexShapes Set of shape indices whose texture sets must match exactly.
     * @return true if a new unique output mesh was added; false if deduplicated or already processed.
     */
    auto commitMesh(const FormKey& formKey,
                    bool isWeighted,
                    const std::unordered_map<unsigned int,
                                             PGTypes::TextureSet>& altTexResults,
                    const std::unordered_set<unsigned int>& nonAltTexShapes) -> bool;

    /**
     * @brief Saves all committed output meshes to disk and returns their results with CRC statistics.
     *
     * @return Pair of (list of MeshResult, pair of (base CRC32, total bytes written)).
     */
    auto saveMeshes() -> std::pair<std::vector<MeshResult>,
                                   std::pair<unsigned long long,
                                             unsigned long long>>;

    /**
     * @brief Validates all weighted mesh variants across all trackers, ensuring _0/_1 pairs are consistent.
     */
    static void validateWeightedVariants();

private:
    /**
     * @brief Handles weighted variant logic for the staged mesh (locates and validates the _0/_1 counterpart).
     */
    void processWeightVariant();

    // Helpers
    /**
     * @brief Compares two NIF files for equivalence, optionally restricting to specific shape texture sets.
     *
     * @param meshA First NIF file.
     * @param meshB Second NIF file.
     * @param enforceCheckShapeTXSTA Shape indices whose texture sets must be compared.
     * @param compareAllTXST If true, compare all texture sets regardless of enforceCheckShapeTXSTA.
     * @param checkOnlyWeighted If true, only compare weighted shapes.
     * @return true if the meshes are considered equivalent.
     */
    static auto compareMesh(const nifly::NifFile& meshA,
                            const nifly::NifFile& meshB,
                            const std::unordered_set<unsigned int>& enforceCheckShapeTXSTA,
                            bool compareAllTXST = false,
                            bool checkOnlyWeighted = false) -> bool;

    /**
     * @brief Compares two BSTriShape blocks for geometric equivalence.
     *
     * @param shapeA First shape.
     * @param shapeB Second shape.
     * @return true if the shapes are equivalent.
     */
    static auto compareBSTriShape(const nifly::BSTriShape& shapeA,
                                  const nifly::BSTriShape& shapeB) -> bool;

    /**
     * @brief Compares two NiShape blocks (name and type).
     *
     * @param shapeA First shape.
     * @param shapeB Second shape.
     * @return true if the shapes are equivalent.
     */
    static auto compareNiShape(const nifly::NiShape& shapeA,
                               const nifly::NiShape& shapeB) -> bool;

    /**
     * @brief Compares two BSLightingShaderProperty blocks for equivalence.
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the shader properties are equivalent.
     */
    static auto compareBSLightingShaderProperty(const nifly::BSLightingShaderProperty& shaderA,
                                                const nifly::BSLightingShaderProperty& shaderB) -> bool;

    /**
     * @brief Compares two BSEffectShaderProperty blocks for equivalence.
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the shader properties are equivalent.
     */
    static auto compareBSEffectShaderProperty(const nifly::BSEffectShaderProperty& shaderA,
                                              const nifly::BSEffectShaderProperty& shaderB) -> bool;

    /**
     * @brief Compares common BSShaderProperty fields for equivalence.
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the base shader properties are equivalent.
     */
    static auto compareBSShaderProperty(const nifly::BSShaderProperty& shaderA,
                                        const nifly::BSShaderProperty& shaderB) -> bool;

    /**
     * @brief Compares two BSShaderTextureSet blocks for texture path equivalence.
     *
     * @param texSetA First texture set.
     * @param texSetB Second texture set.
     * @return true if all texture slots are identical.
     */
    static auto compareBSShaderTextureSet(nifly::BSShaderTextureSet& texSetA,
                                          nifly::BSShaderTextureSet& texSetB) -> bool;

    /**
     * @brief Computes the output file path for a mesh permutation.
     *
     * @param nifPath Original relative NIF path.
     * @param index Permutation index (0 = base name, >0 appends index suffix).
     * @return Output relative path for the permutation.
     */
    static auto getMeshPath(const std::filesystem::path& nifPath,
                            const size_t& index) -> std::filesystem::path;

    /**
     * @brief Returns all NiObject blocks from a NIF that are relevant for comparison.
     *
     * @param nif Pointer to the NIF file.
     * @return Vector of pointers to comparable NiObject blocks.
     */
    static auto getComparableBlocks(const nifly::NifFile* nif) -> std::vector<nifly::NiObject*>;

    /**
     * @brief Builds a map from each 3D shape NiObject to its block index within the NIF.
     *
     * @param nif Pointer to the NIF file.
     * @return Map from NiObject pointer to integer 3D index.
     */
    static auto get3dIndices(const nifly::NifFile* nif) -> std::unordered_map<nifly::NiObject*,
                                                                              int>;

    /**
     * @brief Resolves the path of the corresponding weighted variant (_0/_1) for a given NIF.
     *
     * @param nifPath Path of the current NIF (e.g., the _1 variant).
     * @return Path of the other weight variant (e.g., the _0 variant).
     */
    static auto getOtherWeightVariant(const std::filesystem::path& nifPath) -> std::filesystem::path;
};
