#pragma once

#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

/// @brief Length of the "textures\" prefix that is replaced with "textures\pbr\" in matched paths
constexpr unsigned textureStrLength = 9;

/**
 * @class PatcherMeshShaderTruePBR
 * @brief Shader patcher for True PBR, driven by the entries of the PBR JSON configs (PBRNifPatcher folder)
 *
 * An entry matches a shape by its normal or diffuse texture (match_normal, match_diffuse, path_contains) or by the
 * exact content of a slot (match1 ... match8). Every matching entry is applied in config order, each one setting the
 * shader flags, fields and texture slots its attributes ask for. The configs are loaded once with loadStatics().
 */
class PatcherMeshShaderTruePBR : public PatcherMeshShader {
private:
    // Static caches.

    /**
     * @struct TupleStrHash
     * @brief Key hash for storing a tuple of two strings
     */
    struct TupleStrHash {
        std::size_t operator()(const std::tuple<std::wstring,
                                                std::wstring>& t) const
        {
            const std::size_t hash1 = std::hash<std::wstring> { }(std::get<0>(t));
            const std::size_t hash2 = std::hash<std::wstring> { }(std::get<1>(t));

            // Combine the two hash values.
            return hash1 ^ (hash2 << 1);
        }
    };

    // Options.
    inline static bool s_checkPaths = true;
    inline static bool s_printNonExistentPaths = false;

public:
    /**
     * @brief Get the True PBR Configs
     *
     * @return std::map<size_t, nlohmann::json>& JSON objects
     */
    static std::map<size_t,
                    nlohmann::json>&
    truePBRConfigs();

    /**
     * @brief Get the Path Lookup JSONs objects
     *
     * @return std::map<size_t, nlohmann::json>& JSON objects
     */
    static std::map<size_t,
                    nlohmann::json>&
    pathLookupJSONs();

    /**
     * @brief Get the Path Lookup Cache object
     *
     * @return std::unordered_map<std::tuple<std::wstring, std::wstring>, bool, TupleStrHash>& Cache results for path
     * lookups
     */
    static std::unordered_map<std::tuple<std::wstring,
                                         std::wstring>,
                              bool,
                              TupleStrHash>&
    pathLookupCache();

    /**
     * @brief Get the mutex for protecting the Path Lookup Cache
     *
     * @return std::mutex& Mutex for cache protection
     */
    static std::mutex& pathLookupCacheMutex();

    /**
     * @brief Get the True PBR Diffuse Inverse lookup table
     *
     * @return std::map<std::wstring, std::vector<size_t>>& Lookup
     */
    static std::map<std::wstring,
                    std::vector<size_t>>&
    truePBRDiffuseInverse();

    /**
     * @brief Get the True PBR Normal Inverse lookup table
     *
     * @return std::map<std::wstring, std::vector<size_t>>& Lookup
     */
    static std::map<std::wstring,
                    std::vector<size_t>>&
    truePBRNormalInverse();

    /**
     * @brief Get the True PBR Match X Map
     *
     * @return std::unordered_map<PGEnums::TextureSlots, std::unordered_map<std::wstring, std::vector<size_t>>>& Lookup
     */
    static std::unordered_map<PGEnums::TextureSlots,
                              std::unordered_map<std::wstring,
                                                 std::vector<size_t>>>&
    truePBRMatchXMap();

    /**
     * @brief Get the True PBR Config Filename Fields (fields that have paths)
     *
     * @return std::vector<std::string> Filename fields
     */
    static std::vector<std::string> truePBRConfigFilenameFields();

    /**
     * @brief Get the Factory object for this patcher
     *
     * @return PatcherShader::PatcherShaderFactory Factory object
     */
    static PatcherMeshShader::PatcherMeshShaderFactory factory();

    /**
     * @brief Load the PBR configs and build the lookup tables. Call once per process, before patching
     *
     * @param pbrJSONs PBR json files to load as PBR configs, in load order
     * @param shouldMultithread Read and parse the files on the task pool (entries keep the order of pbrJSONs).
     * @throws std::runtime_error on the calling thread if a config is missing from the file map or from its archive.
     * A config that cannot be opened or parsed is reported as an error and skipped.
     */
    static void loadStatics(const std::vector<std::filesystem::path>& pbrJSONs,
                            const bool& shouldMultithread = true);

    /**
     * @brief Get the Shader Type object (TruePBR)
     *
     * @return PGEnums::ShapeShader Shader type (TruePBR)
     */
    static PGEnums::ShapeShader shaderType();

    /**
     * @brief Construct a new Patcher True PBR patcher
     *
     * @param nifPath NIF Path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshShaderTruePBR(std::filesystem::path nifPath,
                             nifly::NifFile* nif);

    /**
     * @brief Every shape can take True PBR
     *
     * @return true always
     */
    bool canApply(nifly::NiShape& nifShape,
                  bool isSinglepassMATO,
                  const PGPlugin::ModelRecordType& modelRecordType) override;

    /**
     * @brief Find the matches for a shape's texture set. A shape that already has the PBR flag and an existing RMAOS
     * texture gets a match without extra data first, which keeps it as it is
     *
     * @param nifShape Shape to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false Didn't find matches
     */
    bool shouldApply(nifly::NiShape& nifShape,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Find the PBR config entries that match the slots, grouped by config file
     *
     * Each match stands for one config file and carries its matching entries (in config order) as extra data. A match
     * whose resulting textures do not all exist is dropped unless path checks are disabled. Slots that already hold an
     * RMAOS texture without any matching entry get a match without extra data.
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches found, sorted so that the earliest config wins
     * @return true Found matches
     * @return false Didn't find matches
     */
    bool shouldApply(const PGTypes::TextureSet& oldSlots,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Apply every entry of a match to a shape, in config order. A match without extra data leaves the shape
     * alone (pre-patched mesh)
     *
     * @param[in,out] slots Slots of the shape, updated by the entries
     * @param nifShape Shape to patch
     * @param match Match to apply
     */
    void applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape,
                    const PatcherMatch& match) override;

    /**
     * @brief Apply the slot changes of every entry of a match, in config order
     *
     * @param[in,out] slots Slots to patch
     * @param match Match to apply
     */
    void applyPatchSlots(PGTypes::TextureSet& slots,
                         const PatcherMatch& match) override;

    /**
     * @brief Set the plain PBR shader (default type with the PBR flag) on a shape. Only used when a plugin use needs
     * the generic PBR shader; entries apply their own shader through applyPatch()
     *
     * @param nifShape Shape to apply shader to
     */
    void applyShader(nifly::NiShape& nifShape) override;

    /**
     * @brief Hash of the PBR config entries and matched prefixes attached to a match (in application order)
     */
    [[nodiscard]] uint64_t matchExtraDataHash(const PatcherMatch& match) const override;

    /**
     * @brief Load options for this patcher from pgtools option strings
     *
     * @param optionsStr Options by name; "no_path_check" disables the texture existence check of matches,
     * "print_nonexistent_paths" logs a warning for every texture a match refers to that does not exist
     */
    static void loadOptions(std::unordered_map<std::string,
                                               std::string>& optionsStr);

    /**
     * @brief Load options for this patcher
     *
     * @param checkPaths Drop matches whose textures do not exist
     * @param shouldPrintNonExistentPaths Log a warning for every texture a match refers to that does not exist
     */
    static void loadOptions(const bool& checkPaths,
                            const bool& shouldPrintNonExistentPaths);

private:
    /**
     * @brief Applies a single config entry to a shape: geometry attributes (delete, smooth_angle, auto_uv, vertex
     * colors), shader fields, then enableTruePBROnShape()
     *
     * @param nifShape Shape to patch
     * @param truePBRData Entry to apply
     * @param matchedPath Matched path (PBR prefix)
     * @param[in,out] newSlots Slots of the shape, updated by the entry
     * @return true if the shape changed
     */
    bool applyOnePatch(nifly::NiShape* nifShape,
                       nlohmann::json& truePBRData,
                       const std::wstring& matchedPath,
                       PGTypes::TextureSet& newSlots);

    /**
     * @brief Applies the slot changes of a single config entry: the PBR textures derived from the matched path unless
     * locked, then the explicit slot1 ... slot8 attributes
     *
     * @param[in,out] slots Slots to patch
     * @param truePBRData Entry to apply
     * @param matchedPath Matched path (PBR prefix)
     */
    static void applyOnePatchSlots(PGTypes::TextureSet& slots,
                                   const nlohmann::json& truePBRData,
                                   const std::wstring& matchedPath);

    /**
     * @brief Enables True PBR on a shape for a config entry: applies its slots, sets the PBR flag, clears the flags of
     * other shaders and sets the shader type and fields of the entry's multilayer, glint or fuzz attributes
     *
     * @param nifShader Shader of shape
     * @param nifShaderBSLSP The same shader as a BSLightingShaderProperty
     * @param truePBRData Entry to apply
     * @param matchedPath Matched path (PBR prefix)
     * @param[in,out] newSlots Slots of the shape, updated by the entry
     * @return true if the shader changed
     */
    static bool enableTruePBROnShape(nifly::NiShader* nifShader,
                                     nifly::BSLightingShaderProperty* nifShaderBSLSP,
                                     nlohmann::json& truePBRData,
                                     const std::wstring& matchedPath,
                                     PGTypes::TextureSet& newSlots);

    // TruePBR Helpers.

    /**
     * @brief Calculate ABS of 2-element vector
     *
     * @param v vector to calculate abs of
     * @return nifly::Vector2 ABS of vector
     */
    static nifly::Vector2 abs2(nifly::Vector2 v);

    /**
     * @brief Calculates the UV scale that gives a shape a uniform texel density (the "auto_uv" attribute)
     *
     * @param uvs UVs of shape
     * @param verts Vertices of shape
     * @param tris Triangles of shape
     * @return nifly::Vector2 UV scale, the same in both directions
     */
    static nifly::Vector2 autoUVScale(const std::vector<nifly::Vector2>* uvs,
                                      const std::vector<nifly::Vector3>* verts,
                                      std::vector<nifly::Triangle>& tris);

    /**
     * @brief Get the Slot Match for a given lookup (diffuse or normal)
     *
     * @param[out] truePBRData Data that matched
     * @param texName Texture name to match
     * @param lookup Lookup table to use
     * @param nifFilterPath NIF path the "nif_filter" attribute is matched against, empty if the filter does not apply
     */
    static void getSlotMatch(std::map<size_t,
                                      std::tuple<nlohmann::json,
                                                 std::wstring>>& truePBRData,
                             const std::wstring& texName,
                             const std::map<std::wstring,
                                            std::vector<size_t>>& lookup,
                             const std::wstring& nifFilterPath);

    /**
     * @brief Get path contains match for diffuse
     *
     * @param[out] truePBRData Data that matched
     * @param[out] diffuse Texture name to patch
     * @param nifFilterPath NIF path the "nif_filter" attribute is matched against, empty if the filter does not apply
     */
    static void getPathContainsMatch(std::map<size_t,
                                              std::tuple<nlohmann::json,
                                                         std::wstring>>& truePBRData,
                                     const std::wstring& diffuse,
                                     const std::wstring& nifFilterPath);

    /**
     * @brief Get matchX match for a given lookup
     *
     * @param[out] truePBRData Data that matched
     * @param oldSlots Old slots to match
     * @param nifFilterPath NIF path the "nif_filter" attribute is matched against, empty if the filter does not apply
     */
    static void getMatchXMatch(std::map<size_t,
                                        std::tuple<nlohmann::json,
                                                   std::wstring>>& truePBRData,
                               const PGTypes::TextureSet& oldSlots,
                               const std::wstring& nifFilterPath);

    /**
     * @brief Inserts truepbr data if criteria is met
     *
     * @param[out] truePBRData Data to update
     * @param texName Texture name to insert
     * @param cfg Config ID
     * @param nifFilterPath NIF path the "nif_filter" attribute is matched against. Empty when the mesh is not subject
     * to the filter (facegen and BodySlide ShapeData meshes), in which case every entry applies.
     */
    static void insertTruePBRData(std::map<size_t,
                                           std::tuple<nlohmann::json,
                                                      std::wstring>>& truePBRData,
                                  const std::wstring& texName,
                                  size_t cfg,
                                  const std::wstring& nifFilterPath);
};
