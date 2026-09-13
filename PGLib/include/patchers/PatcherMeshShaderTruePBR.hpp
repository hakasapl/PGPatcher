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

constexpr unsigned textureStrLength = 9;

/**
 * @class PatcherMeshShaderTruePBR
 * @brief Patcher for True PBR
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
     * @brief Load statics from a list of PBRJSONs
     *
     * @param pbrJSONs PBR jsons files to load as PBR configs
     */
    static void loadStatics(const std::vector<std::filesystem::path>& pbrJSONs);

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
     * @brief Check if shape can accomodate truepbr (without slots)
     *
     * @param nifShape Shape to check
     * @return true Can accomodate
     * @return false Cannot accomodate
     */
    bool canApply(nifly::NiShape& nifShape,
                  bool singlepassMATO,
                  const PGPlugin::ModelRecordType& modelRecordType) override;

    /**
     * @brief Check if shape can accomodate truepbr (with slots)
     *
     * @param nifShape Shape to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false Didn't find matches
     */
    bool shouldApply(nifly::NiShape& nifShape,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Check if slots can accomodate truepbr
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false Didn't find matches
     */
    bool shouldApply(const PGTypes::TextureSet& oldSlots,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Applies a match to a shape
     *
     * @param nifShape Shape to patch
     * @param match Match to apply
     * @return PGTypes::TextureSet New slots of shape
     */
    void applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape,
                    const PatcherMatch& match) override;

    /**
     * @brief Apply a match to slots
     *
     * @param oldSlots Slots to patch
     * @param[out] match Match to apply
     * @return PGTypes::TextureSet New slots
     */
    void applyPatchSlots(PGTypes::TextureSet& slots,
                         const PatcherMatch& match) override;

    /**
     * @brief Apply pbr shader to a shape
     *
     * @param nifShape Shape to apply shader to
     */
    void applyShader(nifly::NiShape& nifShape) override;

    /**
     * @brief Hash of the PBR config entries and matched prefixes attached to a match (in application order)
     */
    [[nodiscard]] uint64_t matchExtraDataHash(const PatcherMatch& match) const override;

    /**
     * @brief Load PBR options string
     *
     * @param optionsStr string to load
     */
    static void loadOptions(std::unordered_map<std::string,
                                               std::string>& optionsStr);

    static void loadOptions(const bool& checkPaths,
                            const bool& printNonExistentPaths);

private:
    /**
     * @brief Applies a single JSON config to a shape
     *
     * @param nifShape Shape to patch
     * @param truePBRData Data to patch
     * @param matchedPath Matched path (PBR prefix)
     * @param[out] newSlots New slots of shape
     */
    bool applyOnePatch(nifly::NiShape* nifShape,
                       nlohmann::json& truePBRData,
                       const std::wstring& matchedPath,
                       PGTypes::TextureSet& newSlots);

    /**
     * @brief Applies a single JSON config to slots
     *
     * @param oldSlots Slots to patch
     * @param truePBRData Data to patch
     * @param matchedPath Matched path (PBR prefix)
     * @return PGTypes::TextureSet New slots after patch
     */
    static void applyOnePatchSlots(PGTypes::TextureSet& slots,
                                   const nlohmann::json& truePBRData,
                                   const std::wstring& matchedPath);

    /**
     * @brief Enables truepbr on a shape (always applied for a matched JSON entry)
     *
     * @param nifShader Shader of shape
     * @param nifShaderBSLSP Properties of shader
     * @param truePBRData Data to enable truepbr with
     * @param matchedPath Matched path (PBR prefix)
     * @param[out] newSlots New slots of shape
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
     * @brief Math that calculates auto UV scale for a shape
     *
     * @param uvs UVs of shape
     * @param verts Vertices of shape
     * @param tris Triangles of shape
     * @return nifly::Vector2
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
     * @param nifPath NIF path to use
     */
    static void getSlotMatch(std::map<size_t,
                                      std::tuple<nlohmann::json,
                                                 std::wstring>>& truePBRData,
                             const std::wstring& texName,
                             const std::map<std::wstring,
                                            std::vector<size_t>>& lookup,
                             const std::wstring& nifPath);

    /**
     * @brief Get path contains match for diffuse
     *
     * @param[out] truePBRData Data that matched
     * @param[out] diffuse Texture name to patch
     * @param nifPath NIF path to use
     */
    static void getPathContainsMatch(std::map<size_t,
                                              std::tuple<nlohmann::json,
                                                         std::wstring>>& truePBRData,
                                     const std::wstring& diffuse,
                                     const std::wstring& nifPath);

    /**
     * @brief Get matchX match for a given lookup
     *
     * @param[out] truePBRData Data that matched
     * @param oldSlots Old slots to match
     * @param nifPath NIF path to use
     */
    static void getMatchXMatch(std::map<size_t,
                                        std::tuple<nlohmann::json,
                                                   std::wstring>>& truePBRData,
                               const PGTypes::TextureSet& oldSlots,
                               const std::wstring& nifPath);

    /**
     * @brief Inserts truepbr data if criteria is met
     *
     * @param[out] truePBRData Data to update
     * @param texName Texture name to insert
     * @param cfg Config ID
     * @param nifPath NIF path to use
     */
    static void insertTruePBRData(std::map<size_t,
                                           std::tuple<nlohmann::json,
                                                      std::wstring>>& truePBRData,
                                  const std::wstring& texName,
                                  size_t cfg,
                                  const std::wstring& nifPath);
};
