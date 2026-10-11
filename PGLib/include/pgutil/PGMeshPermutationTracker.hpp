#pragma once

#include "pgutil/PGTypes.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Collects the output meshes of one input mesh.
 *
 * Every plugin use of a mesh is patched on its own staged copy. commitMesh() keeps only the distinct results: a result
 * equal to an earlier output is merged into it, a result equal to the original mesh means the use keeps the unpatched
 * file. saveMeshes() writes the base mesh and its duplicates (under _pgpatcher_dups) and reports, per output, which
 * plugin uses it serves. Weighted (_0/_1) armor meshes are additionally checked to produce matching outputs.
 */
class PGMeshPermutationTracker {
public:
    /// @brief Identifies one plugin use of a mesh: a model of a record.
    struct FormKey {
        /// @brief Name of the mod file (plugin) that owns this form (e.g., L"Skyrim.esm").
        std::wstring modKey;
        /// @brief Numeric form ID of the record referencing this mesh.
        unsigned formID = 0;
        /// @brief Sub-model path within the record (empty for the primary model).
        std::string subMODL;

        bool operator==(const FormKey& other) const
        {
            return formID == other.formID && modKey == other.modKey && subMODL == other.subMODL;
        }

        bool operator<(const FormKey& other) const
        {
            if (modKey != other.modKey)
                return modKey < other.modKey;
            if (formID != other.formID)
                return formID < other.formID;
            return subMODL < other.subMODL;
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
        std::size_t operator()(const FormKey& key) const
        {
            const size_t h1 = std::hash<std::wstring> { }(key.modKey);
            const size_t h2 = std::hash<unsigned> { }(key.formID);
            const size_t h3 = std::hash<std::string> { }(key.subMODL);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    /// @brief One saved output mesh and the plugin uses it serves.
    struct MeshResult {
        /// @brief Relative path (within the data directory) of the saved output mesh file.
        std::filesystem::path meshPath;
        /// @brief Alternate texture results keyed by FormKey; each maps the original 3D index of a shape to its
        /// patched TextureSet.
        std::vector<std::pair<FormKey, std::unordered_map<unsigned, PGTypes::TextureSet>>> altTexResults;
        /// @brief Original 3D index of each shape mapped to its 3D index in the saved file (-1 if it was deleted).
        std::unordered_map<int, int> idxCorrections;
        /// @brief 3D index of each shape after patching mapped back to its original 3D index, so shapes deleted by
        /// patchers do not shift the indices alternate textures refer to.
        std::unordered_map<int, int> inverseIdxCorrectionsPatching;
        /// @brief Bounding box of the saved mesh in model space, computed across all shapes with vertices. Empty if the
        /// mesh has none, or if bounds were not requested when saving.
        std::optional<PGTypes::ObjectBounds> objectBounds;
    };

private:
    std::filesystem::path m_origMeshPath;
    nifly::NifFile m_origNifFile;
    std::unordered_set<int> m_origShapeIndices;
    unsigned long long m_origCrc32 { 0 };
    bool m_ignoreBaseMesh = false;

    std::vector<std::pair<MeshResult, nifly::NifFile>> m_outputMeshes;
    std::unordered_set<FormKey, FormKeyHash> m_processedFormKeys;
    /// @brief Indices into m_outputMeshes that already had their _0/_1 counterpart validated.
    std::unordered_set<std::size_t> m_weightProcessedOutputs;

    nifly::NifFile m_stagedMesh;
    nifly::NifFile* m_stagedMeshPtr { nullptr };
    std::unordered_map<nifly::NiObject*, int> m_stagedMeshOriginal3DIdx;

    using AltTex3DIndices = std::unordered_set<unsigned>;

    struct PathSizeHash {
        size_t operator()(const std::pair<std::filesystem::path,
                                          size_t>& key) const noexcept
        {
            const size_t h1 = std::hash<std::wstring> { }(key.first.wstring());
            const size_t h2 = std::hash<size_t> { }(key.second);

            // Standard hash combine.
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };
    static inline std::mutex s_otherWeightVariantsMutex;
    static inline std::unordered_map<std::pair<std::filesystem::path, size_t>, nifly::NifFile, PathSizeHash>
        s_otherWeightVariants;
    /// @brief Mesh paths that went through weighted variant processing (used weighted in plugins and patched).
    /// Guarded by s_otherWeightVariantsMutex.
    static inline std::unordered_set<std::wstring> s_weightVariantProcessedPaths;

public:
    /**
     * @brief Constructs a tracker for the given original mesh path.
     *
     * @param origMeshPath Relative path (within the data directory) to the source NIF file.
     * @throws std::runtime_error if the file is not in the file map of PGGlobals::pgd().
     */
    explicit PGMeshPermutationTracker(const std::filesystem::path& origMeshPath);

    // Plugin mesh staging.
    /**
     * @brief Reads the original NIF through PGGlobals::pgd(), computes the CRC32 of its bytes and loads it.
     *
     * @throws std::runtime_error if the NIF cannot be read or loaded.
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
    nifly::NifFile* stageMesh();

    /**
     * @brief Marks that the unmodified base mesh should not be written to disk even when no patches are needed.
     */
    void ignoreBaseMesh();

    /// @brief What became of a staged mesh in commitMesh().
    enum class CommitResult : uint8_t {
        Added, ///< The staged mesh is a new output mesh.
        MergedIntoOutput, ///< The staged mesh equals an existing output mesh, which now serves this form key too.
        UnchangedFromOriginal, ///< The staged mesh equals the original mesh, which stays in use unpatched.
        AlreadyProcessed, ///< The form key was committed before.
    };

    /**
     * @brief Commits the current staged mesh as the output of the given form key and clears the staging area.
     *
     * The staged mesh is compared with every existing output mesh and, when there is none yet, with the original
     * mesh. Texture sets of shapes without an alternate texture take part in the comparison; the others are supplied
     * by the plugin and so do not have to match.
     *
     * @param formKey The plugin use that was patched.
     * @param isWeighted Whether the use is a weighted armor model, whose _0/_1 counterpart has to produce the same
     * output (see validateWeightedVariants()).
     * @param altTexResults Patched alternate textures of the use by original 3D shape index.
     * @param nonAltTexShapes Original 3D indices of the shapes that had no alternate texture.
     * @return What became of the staged mesh. Only CommitResult::Added means a new output mesh was created.
     * @throws std::runtime_error if no mesh is staged.
     */
    CommitResult commitMesh(const FormKey& formKey,
                            bool isWeighted,
                            const std::unordered_map<unsigned,
                                                     PGTypes::TextureSet>& altTexResults,
                            const std::unordered_set<unsigned>& nonAltTexShapes);

    /**
     * @brief Sorts the blocks of every output mesh, queues the files for writing through PGGlobals::fileSaver() and
     * registers them as generated files.
     *
     * The first output takes the original path unless ignoreBaseMesh() was called, the others go under
     * _pgpatcher_dups. Each result carries the shape index corrections the plugin needs.
     *
     * @param shouldComputeObjectBounds whether to compute the bounding box of each saved mesh
     * (MeshResult::objectBounds)
     * @return The results, and a pair of the original mesh's CRC32 and the CRC32 of the saved base mesh (0 when the
     * base mesh was not written). An empty result list with an all-zero pair means a mesh could not be serialized.
     * @throws std::runtime_error if an output file already exists or the shape bookkeeping is inconsistent.
     */
    std::pair<std::vector<MeshResult>,
              std::pair<unsigned long long,
                        unsigned long long>>
    saveMeshes(const bool& shouldComputeObjectBounds = false);

    /**
     * @brief Computes the bounding box of the original (unpatched) mesh. Patchers never move vertices, so this matches
     * the bounding box of an output mesh unless a patcher deleted a shape (e.g. the TruePBR delete attribute). It does
     * not require an output mesh to be produced.
     *
     * @return The bounding box, or std::nullopt if the mesh has no shapes with vertices.
     */
    std::optional<PGTypes::ObjectBounds> originalObjectBounds();

    /**
     * @brief Reports every weighted mesh whose _0/_1 counterpart was also patched as weighted but produced a different
     * result, then clears the state shared between trackers. Call once per run after all meshes were committed.
     *
     * @return Errors that were logged, as (mesh path the error is about, message) pairs.
     */
    static std::vector<std::pair<std::filesystem::path,
                                 std::wstring>>
    validateWeightedVariants();

    /**
     * @brief Resolves the path of the corresponding weighted variant (_0/_1) for a given NIF.
     *
     * @param nifPath Path of the current NIF (e.g., the _1 variant).
     * @return Path of the other weight variant (e.g., the _0 variant), or the same path if it is not a weight variant.
     */
    static std::filesystem::path otherWeightVariant(const std::filesystem::path& nifPath);

private:
    /**
     * @brief Pairs an output mesh with the same output of its _0/_1 counterpart: the first of the two is kept in a
     * cache shared between trackers, the second compares itself against it and logs an error if they differ.
     *
     * @param mesh The output mesh serving the weighted plugin use.
     * @param dupIdx Index of the output mesh within this tracker.
     */
    void processWeightVariant(const nifly::NifFile& mesh,
                              std::size_t dupIdx);

    // Helpers.
    /**
     * @brief Compares the shapes and particle systems of two NIFs in 3D index order: block types, vertex colors,
     * lighting and effect shader fields and, where requested, texture sets.
     *
     * @param meshA First NIF file.
     * @param meshB Second NIF file.
     * @param enforceCheckShapeTXSTA Original 3D indices of the shapes of meshA whose texture sets must be compared.
     * @param compareAllTXST If true, compare all texture sets regardless of enforceCheckShapeTXSTA.
     * @param checkOnlyWeighted If true, only compare the block types (used for _0/_1 counterparts, whose geometry
     * legitimately differs).
     * @param meshAInverseIdxCorrectionsPatching Maps the current 3D indices of meshA to the original ones, so
     * enforceCheckShapeTXSTA still applies after patchers deleted shapes. Optional.
     * @return true if the meshes are considered equivalent.
     */
    static bool compareMesh(const nifly::NifFile& meshA,
                            const nifly::NifFile& meshB,
                            const std::unordered_set<unsigned>& enforceCheckShapeTXSTA,
                            bool compareAllTXST = false,
                            bool checkOnlyWeighted = false,
                            const std::unordered_map<int,
                                                     int>* meshAInverseIdxCorrectionsPatching = nullptr);

    /**
     * @brief Compares the vertex colors of two BSTriShape blocks (the only geometry patchers change).
     *
     * @param shapeA First shape.
     * @param shapeB Second shape.
     * @return true if both have no vertex colors, or both have the same vertex colors.
     */
    static bool compareBSTriShape(const nifly::BSTriShape& shapeA,
                                  const nifly::BSTriShape& shapeB);

    /**
     * @brief Compares whether two NiShape blocks both have vertex colors.
     *
     * @param shapeA First shape.
     * @param shapeB Second shape.
     * @return true if the vertex color flags are equal.
     */
    static bool compareNiShape(const nifly::NiShape& shapeA,
                               const nifly::NiShape& shapeB);

    /**
     * @brief Compares two BSLightingShaderProperty blocks for equivalence (every field of the SSE mesh format).
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the shader properties are equivalent.
     */
    static bool compareBSLightingShaderProperty(const nifly::BSLightingShaderProperty& shaderA,
                                                const nifly::BSLightingShaderProperty& shaderB);

    /**
     * @brief Compares the texture clamp mode of two BSEffectShaderProperty blocks (the only field patchers change).
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the shader properties are equivalent.
     */
    static bool compareBSEffectShaderProperty(const nifly::BSEffectShaderProperty& shaderA,
                                              const nifly::BSEffectShaderProperty& shaderB);

    /**
     * @brief Compares common BSShaderProperty fields for equivalence.
     *
     * @param shaderA First shader property.
     * @param shaderB Second shader property.
     * @return true if the base shader properties are equivalent.
     */
    static bool compareBSShaderProperty(const nifly::BSShaderProperty& shaderA,
                                        const nifly::BSShaderProperty& shaderB);

    /**
     * @brief Compares two BSShaderTextureSet blocks slot by slot without regard to case (see
     * PGMaterialState::isTextureSetEqual()).
     *
     * @param texSetA First texture set.
     * @param texSetB Second texture set.
     * @return true if all texture slots are equal.
     */
    static bool compareBSShaderTextureSet(nifly::BSShaderTextureSet& texSetA,
                                          nifly::BSShaderTextureSet& texSetB);

    /**
     * @brief Computes the output path of a mesh permutation.
     *
     * @param nifPath Original relative NIF path.
     * @param index Permutation index. 0 is the original path, every other index inserts "_pgpatcher_dups\<index>"
     * after the first path component (e.g. meshes\_pgpatcher_dups\1\...).
     * @return Output relative path for the permutation.
     */
    static std::filesystem::path meshPath(const std::filesystem::path& nifPath,
                                          const size_t& index);

    /**
     * @brief Computes the bounding box of a NIF in model space, across every shape with vertices (not only the ones
     * PG can patch), accounting for each shape's transform relative to the NIF root. Truncated toward zero like the
     * OBND subrecord the Creation Kit stores for statics, trees and grass.
     *
     * @param nif The NIF file to measure.
     * @return The bounding box, or std::nullopt if the NIF has no shapes with vertices.
     */
    static std::optional<PGTypes::ObjectBounds> computeObjectBounds(nifly::NifFile& nif);

    /**
     * @brief Returns the shapes and particle systems of a NIF in 3D index order.
     *
     * @param nif Pointer to the NIF file.
     * @return The blocks, sorted by 3D index.
     * @throws std::runtime_error if nif is null.
     */
    static std::vector<nifly::NiObject*> comparableBlocks(const nifly::NifFile* nif);

    /**
     * @brief Maps every shape and particle system of a NIF to its 3D index, the index plugins use for alternate
     * textures (shapes and particle systems counted in block tree order).
     *
     * @param nif Pointer to the NIF file.
     * @return Map from block to 3D index.
     * @throws std::runtime_error if nif is null.
     */
    static std::unordered_map<nifly::NiObject*,
                              int>
    get3dIndices(const nifly::NifFile* nif);

    /**
     * @brief Returns the 3D indices of every shape and particle system of a NIF (see get3dIndices()).
     *
     * @throws std::runtime_error if nif is null.
     */
    static std::unordered_set<int> get3dIndicesSet(const nifly::NifFile* nif);

    /**
     * @brief Maps the current 3D index of every block that still exists to its original 3D index.
     *
     * @param current3DIndices 3D indices after patching, see get3dIndices().
     * @param original3DIndices 3D indices before patching.
     */
    static std::unordered_map<int,
                              int>
    buildInverseIdxCorrections(const std::unordered_map<nifly::NiObject*,
                                                        int>& current3DIndices,
                               const std::unordered_map<nifly::NiObject*,
                                                        int>& original3DIndices);
};
