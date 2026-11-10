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
     * A config that cannot be opened or parsed is reported as an error and skipped. An entry that is not an object, or
     * whose texture, match, path_contains, nif_filter or rename key holds something other than a string, is reported as
     * a warning and skipped.
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

    /**
     * @class AppliedAttributes
     * @brief Collects the material attributes the entries applyPatch() applies to each shape on the calling thread, a
     * file's defaults included, for material(): by name, with the value of the last entry that applied it. Several
     * entries apply to a shape when more than one entry of the winning config matches it; the attributes then say
     * what the sequence did, where one set of attributes can:
     * - Only a value the patch applies where the entry has it counts: one of a type the patch ignores, a coat
     *   attribute of an entry that is not multilayer, glint under multilayer, fuzz under multilayer or glint, a
     *   cubemap or parallax whose slot the entry locks, and an alpha property value of a shape that has none and gets
     *   none are left out, and so is "alphaprop_create" or "alphaprop_delete" while off, which does nothing.
     * - The attributes every entry decides again whether it has them or not (multilayer, hair, glint, fuzz, cubemap,
     *   the locks of the slots every entry fills, and subsurface_foliage) are the last entry's alone; the emissive and
     *   parallax locks go with an entry that sets emissive or parallax.
     * - "alphaprop_delete" drops the alpha attributes of the entries before it, and a property "alphaprop_create" adds
     *   after it drops the delete.
     * - The attributes that write one shader field (subsurface_color and coat_color, subsurface_opacity and
     *   coat_strength) hold the value the field has: setting one sets the other where an earlier entry set it.
     * - The vertex color multipliers compound, so theirs is the product of the numbers the entries set, counted where
     *   the patch changed the colors: a shape with vertex colors in its vertex data.
     * - The UV scale is the last one applied: "uv_scale", or "auto_uv" when it came after the last "uv_scale" (an
     *   entry applies its "uv_scale" after its "auto_uv").
     *
     * What the attributes cannot say: a shader field one shader wrote that a later entry of another shader reads under
     * another name (a coat value once the shape is no longer multilayer, a glint or fuzz value once it is), and the
     * shader flags an entry sets that no later entry clears (glint's and coat_diffuse's are never cleared). It
     * collects while it lives; one made while another lives takes over until it goes.
     */
    class AppliedAttributes {
    public:
        AppliedAttributes();
        ~AppliedAttributes();
        AppliedAttributes(const AppliedAttributes&) = delete;
        AppliedAttributes& operator=(const AppliedAttributes&) = delete;
        AppliedAttributes(AppliedAttributes&&) = delete;
        AppliedAttributes& operator=(AppliedAttributes&&) = delete;

        /// @brief The attributes of the entries applied to a shape, or nullptr for a shape no entry was applied to,
        /// such as one that is PBR on disk and matched by no entry.
        [[nodiscard]] const nlohmann::json* of(const nifly::NiShape& shape) const;

    private:
        friend class PatcherMeshShaderTruePBR;

        /// @brief Filled by applyPatch() through the thread's pointer to the collector, which may be a const one.
        mutable std::unordered_map<const nifly::NiShape*, nlohmann::json> m_attributes;
        /// @brief The collector this one took over from, which collects again when this one goes.
        AppliedAttributes* m_previous;
    };

    /**
     * @brief The PBR material of a shape as the entries applied to it define it; whatever they leave out is the mesh's
     * own. "shader_type" is "pbr", and the textures are "slot1" ... "slot8" as the shape ends up with them, relative to
     * the textures folder, for the slots that are not empty: both are always written. The material attributes of the
     * entries follow as the configs give them (see AppliedAttributes), those of a type the patch applies: the shader
     * attributes "multilayer", "hair", "glint" and "fuzz", the shader parameters, the slot locks and the attributes
     * that fill slots, "vertex_colors", "zbuffer_write", the "alphaprop_*" attributes, and the geometry attributes
     * "smooth_angle", "vertex_color_lum_mult" and "vertex_color_sat_mult", which leave only their result in the mesh
     * (a product of multipliers past the range of a float is left out). "uv_scale" is one number for both
     * directions, as the configs give it; for "auto_uv", which computes the scale
     * from the geometry, it is the number auto_uv computed (left out if that is not finite). The attributes that pick
     * the shapes and the prefix are left out, and so is "delete": a deleted shape has no material.
     *
     * @param nif The NIF the shape belongs to
     * @param nifShape The shape
     * @param slots The textures the shape is drawn with: its texture set, or the alternate texture of a record
     * @param attributes The attributes of the entries applied to the shape, see AppliedAttributes
     * @return The material, or a null JSON when the shape is not PBR
     */
    [[nodiscard]] static nlohmann::json material(const nifly::NifFile& nif,
                                                 nifly::NiShape& nifShape,
                                                 const PGTypes::TextureSet& slots,
                                                 const nlohmann::json& attributes);

private:
    /// @brief The collector of the calling thread, see AppliedAttributes.
    static inline thread_local AppliedAttributes* s_appliedAttributes = nullptr;

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
