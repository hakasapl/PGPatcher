#include "patchers/PatcherMeshShaderTruePBR.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMaterialState.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include "VertexData.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/gil.hpp>
#include <boost/gil/color_convert.hpp>
#include <boost/gil/extension/toolbox/color_converters.hpp>
#include <boost/gil/extension/toolbox/color_spaces/hsl.hpp>
#include <boost/gil/typedefs.hpp>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

/// @brief Checks whether a PBR entry turns a boolean attribute on. A value of another type counts as off.
bool isEnabled(const nlohmann::json& truePBRData,
               const char* key)
{
    const auto it = truePBRData.find(key);
    return it != truePBRData.end() && it->is_boolean() && it->get<bool>();
}

/// @brief A float as a JSON number with the shortest decimal that reads back as the same float, so that 0.04F is
/// written as 0.04 and not as the 17 digits of the double it widens to.
double jsonNumber(float value)
{
    std::array<char, 32> buffer { };
    auto number = static_cast<double>(value);
    const auto [end, ec] = std::to_chars(buffer.data(), std::to_address(buffer.end()), value);
    if (ec == std::errc())
        std::ignore = std::from_chars(buffer.data(), end, number);

    return number;
}

/// @brief What a material attribute is applied for. A value of another kind is ignored, as the patch ignores it.
enum class AttributeType : uint8_t { Number, Boolean, String, Color3, Color4, AlphaFlags, AlphaThreshold, Glint, Fuzz };

/**
 * @brief The attributes of an entry that describe the material, see PatcherMeshShaderTruePBR::material(). Left out
 * are the attributes that pick the shapes and the prefix (texture, match_*, match1 ... match8, path_contains,
 * nif_filter, rename, and the json the configs are read from), delete, whose shape is gone, the legacy ones the patch
 * ignores (pbr, env_*), and those whose result the material holds instead: auto_uv, as uv_scale, and slot1 ... slot8.
 */
const std::unordered_map<std::string,
                         AttributeType>&
materialAttributeTypes()
{
    static const std::unordered_map<std::string, AttributeType> types = {
        { "uv_scale", AttributeType::Number },
        { "specular_level", AttributeType::Number },
        { "roughness_scale", AttributeType::Number },
        { "displacement_scale", AttributeType::Number },
        { "subsurface_opacity", AttributeType::Number },
        { "emissive_scale", AttributeType::Number },
        { "coat_specular_level", AttributeType::Number },
        { "coat_roughness", AttributeType::Number },
        { "coat_strength", AttributeType::Number },
        { "inner_uv_scale", AttributeType::Number },
        { "smooth_angle", AttributeType::Number },
        { "vertex_color_lum_mult", AttributeType::Number },
        { "vertex_color_sat_mult", AttributeType::Number },
        { "alphaprop_flags", AttributeType::AlphaFlags },
        { "alphaprop_thresh", AttributeType::AlphaThreshold },
        { "emissive", AttributeType::Boolean },
        { "parallax", AttributeType::Boolean },
        { "subsurface", AttributeType::Boolean },
        { "subsurface_foliage", AttributeType::Boolean },
        { "hair", AttributeType::Boolean },
        { "multilayer", AttributeType::Boolean },
        { "coat_diffuse", AttributeType::Boolean },
        { "coat_normal", AttributeType::Boolean },
        { "coat_parallax", AttributeType::Boolean },
        { "vertex_colors", AttributeType::Boolean },
        { "zbuffer_write", AttributeType::Boolean },
        { "alphaprop_create", AttributeType::Boolean },
        { "alphaprop_delete", AttributeType::Boolean },
        { "lock_diffuse", AttributeType::Boolean },
        { "lock_normal", AttributeType::Boolean },
        { "lock_emissive", AttributeType::Boolean },
        { "lock_parallax", AttributeType::Boolean },
        { "lock_cubemap", AttributeType::Boolean },
        { "lock_rmaos", AttributeType::Boolean },
        { "lock_cnr", AttributeType::Boolean },
        { "lock_subsurface", AttributeType::Boolean },
        { "cubemap", AttributeType::String },
        { "subsurface_color", AttributeType::Color3 },
        { "coat_color", AttributeType::Color3 },
        { "emissive_color", AttributeType::Color4 },
        { "glint", AttributeType::Glint },
        { "fuzz", AttributeType::Fuzz },
    };

    return types;
}

/// @brief The first numbers of a color as the patch reads them, or nothing when the color has fewer numbers.
std::optional<nlohmann::json> colorOf(const nlohmann::json& value,
                                      std::size_t count)
{
    if (!value.is_array() || value.size() < count)
        return std::nullopt;

    auto color = nlohmann::json::array();
    for (std::size_t i = 0; i < count; i++) {
        if (!value.at(i).is_number())
            return std::nullopt;

        color.push_back(value.at(i));
    }

    return color;
}

/// @brief The members of an object attribute the patch applies, of the kinds it applies them for; the object itself
/// counts even when none does (an empty glint or fuzz object still asks for its shader).
std::optional<nlohmann::json> membersOf(const nlohmann::json& value,
                                        const std::vector<std::pair<const char*,
                                                                    AttributeType>>& members);

/// @brief A value of a material attribute as the patch applies it, or nothing for a value the patch ignores.
std::optional<nlohmann::json> appliedValue(const nlohmann::json& value,
                                           AttributeType type)
{
    switch (type) {
    case AttributeType::Number:
        return value.is_number() ? std::optional(value) : std::nullopt;
    case AttributeType::Boolean:
        return value.is_boolean() ? std::optional(value) : std::nullopt;
    case AttributeType::String:
        return value.is_string() ? std::optional(value) : std::nullopt;
    case AttributeType::Color3:
        return colorOf(value, 3);
    case AttributeType::Color4:
        return colorOf(value, 4);
    case AttributeType::Glint:
        return membersOf(value,
                         {
                             { "screen_space_scale", AttributeType::Number },
                             { "log_microfacet_density", AttributeType::Number },
                             { "microfacet_roughness", AttributeType::Number },
                             { "density_randomization", AttributeType::Number },
                         });
    case AttributeType::Fuzz: {
        // The color counts only with exactly three numbers.
        auto fuzz = membersOf(value, { { "texture", AttributeType::Boolean }, { "weight", AttributeType::Number } });
        if (fuzz && value.contains("color") && value.at("color").is_array() && value.at("color").size() == 3) {
            if (auto color = colorOf(value.at("color"), 3))
                (*fuzz)["color"] = std::move(*color);
        }
        return fuzz;
    }
    case AttributeType::AlphaFlags: {
        uint16_t flags = 0;
        return PGMaterialState::unsignedFromJSON(value, flags) ? std::optional(nlohmann::json(flags)) : std::nullopt;
    }
    case AttributeType::AlphaThreshold: {
        uint8_t threshold = 0;
        return PGMaterialState::unsignedFromJSON(value, threshold) ? std::optional(nlohmann::json(threshold))
                                                                   : std::nullopt;
    }
    }

    return std::nullopt;
}

std::optional<nlohmann::json> membersOf(const nlohmann::json& value,
                                        const std::vector<std::pair<const char*,
                                                                    AttributeType>>& members)
{
    if (!value.is_object())
        return std::nullopt;

    auto result = nlohmann::json::object();
    for (const auto& [key, type] : members) {
        if (!value.contains(key))
            continue;

        if (auto applied = appliedValue(value.at(key), type))
            result[key] = std::move(*applied);
    }

    return result;
}

/// @brief The attributes every entry decides again, whether it has them or not: the shader it asks for, the cubemap
/// and the slots it locks. What an earlier entry said about them is gone once a later one applies.
constexpr std::array<const char*, 12> entryDecidedAttributes = {
    "multilayer",   "hair",        "glint",      "fuzz",     "cubemap",         "lock_cubemap",
    "lock_diffuse", "lock_normal", "lock_rmaos", "lock_cnr", "lock_subsurface", "subsurface_foliage",
};

/// @brief The slot locks an entry decides again when it sets the slot's attribute.
constexpr std::array<std::pair<const char*, const char*>, 2> attributeLocks = {
    std::pair { "emissive", "lock_emissive" },
    std::pair { "parallax", "lock_parallax" },
};

/// @brief The attributes only a multilayer entry applies; their textures, if any, are in the slots either way.
constexpr std::array<const char*, 8> multilayerAttributes = {
    "coat_color",   "coat_specular_level", "coat_roughness", "coat_strength",
    "coat_diffuse", "coat_parallax",       "coat_normal",    "inner_uv_scale",
};

/// @brief The pairs of attributes that write one shader field: the first one for every entry, the second one for a
/// multilayer entry, after the first.
constexpr std::array<std::pair<const char*, const char*>, 2> sharedFieldAttributes = {
    std::pair { "subsurface_color", "coat_color" },
    std::pair { "subsurface_opacity", "coat_strength" },
};

constexpr std::array<const char*, 2> vertexColorMultipliers = { "vertex_color_lum_mult", "vertex_color_sat_mult" };

/// @brief Whether an entry applies one of its material attributes where it has it, beyond the type of its value (the
/// precedence of the shaders and the slot locks). The alpha property, the vertex color multipliers and the UV scales
/// have rules of their own, see recordAppliedAttributes().
bool isAppliedByEntry(const std::string& key,
                      const nlohmann::json& truePBRData)
{
    const bool isMultilayer = isEnabled(truePBRData, "multilayer");
    if (std::ranges::find(multilayerAttributes, key) != multilayerAttributes.end())
        return isMultilayer;

    // The patch applies one of multilayer, glint and fuzz, in that order.
    if (key == "glint")
        return !isMultilayer;

    if (key == "fuzz") {
        const auto glint = truePBRData.find("glint");
        return !isMultilayer && (glint == truePBRData.end() || !glint->is_object());
    }

    if (key == "cubemap")
        return !isEnabled(truePBRData, "lock_cubemap");

    if (key == "parallax")
        return !isEnabled(truePBRData, "lock_parallax");

    return true;
}

/**
 * @brief Adds the attributes one entry applies to a shape to those of the entries applied to it before, see
 * PatcherMeshShaderTruePBR::AppliedAttributes. Before the entry is applied: the shape is as the earlier entries left
 * it.
 */
void recordAppliedAttributes(nlohmann::json& attributes,
                             const nlohmann::json& truePBRData,
                             const nifly::NifFile& nif,
                             nifly::NiShape& shape)
{
    // A shape's first entry finds nothing collected yet.
    if (!attributes.is_object())
        attributes = nlohmann::json::object();

    for (const auto* const key : entryDecidedAttributes)
        attributes.erase(key);

    const auto& types = materialAttributeTypes();
    const auto applied = [&truePBRData, &types](const char* key) -> std::optional<nlohmann::json> {
        const auto value = truePBRData.find(key);
        if (value == truePBRData.end() || !isAppliedByEntry(key, truePBRData))
            return std::nullopt;

        return appliedValue(*value, types.at(key));
    };

    for (const auto& [attribute, lock] : attributeLocks)
        if (applied(attribute))
            attributes.erase(lock);

    // The material attributes with the value the patch applies, those with rules of their own below.
    for (const auto& [key, type] : types) {
        if (key.starts_with("alphaprop_") || key == "uv_scale"
            || std::ranges::find(vertexColorMultipliers, key) != vertexColorMultipliers.end()) {
            continue;
        }

        if (auto value = applied(key.c_str()))
            attributes[key] = std::move(*value);
    }

    // An attribute of a pair an earlier entry set says what the field holds after this entry.
    for (const auto& [first, second] : sharedFieldAttributes) {
        const bool didWriteFirst = applied(first).has_value();
        const bool didWriteSecond = applied(second).has_value();
        if (didWriteSecond && !didWriteFirst && attributes.contains(first))
            attributes[first] = attributes[second];
        else if (didWriteFirst && !didWriteSecond && attributes.contains(second))
            attributes[second] = attributes[first];
    }

    // The alpha property. alphaprop_delete removes it, and the entry's other alpha attributes do nothing then; both
    // booleans act only when on, and a shape without a property gets one only with alphaprop_create.
    if (isEnabled(truePBRData, "alphaprop_delete")) {
        for (const auto* const key : { "alphaprop_create", "alphaprop_flags", "alphaprop_thresh" })
            attributes.erase(key);
        attributes["alphaprop_delete"] = true;
    } else if (shape.AlphaPropertyRef()) {
        const bool hasProperty = nif.GetAlphaProperty(&shape) != nullptr;
        if (!hasProperty && isEnabled(truePBRData, "alphaprop_create")) {
            attributes.erase("alphaprop_delete");
            attributes["alphaprop_create"] = true;
        }

        if (hasProperty || isEnabled(truePBRData, "alphaprop_create")) {
            for (const auto* const key : { "alphaprop_flags", "alphaprop_thresh" })
                if (auto value = applied(key))
                    attributes[key] = std::move(*value);
        }
    }

    // Each vertex color multiplier applies to the colors the previous one left, on a shape that has vertex colors once
    // the entry's vertex_colors applied and keeps them in its vertex data.
    const auto vertexColors = truePBRData.find("vertex_colors");
    const bool hasVertexColors = vertexColors != truePBRData.end() && vertexColors->is_boolean()
        ? vertexColors->get<bool>()
        : shape.HasVertexColors();
    if (hasVertexColors && dynamic_cast<const nifly::BSTriShape*>(&shape)) {
        for (const auto* const key : vertexColorMultipliers) {
            const auto value = truePBRData.find(key);
            if (value == truePBRData.end() || !value->is_number())
                continue;

            const auto previous = attributes.find(key);
            attributes[key] = previous != attributes.end()
                ? nlohmann::json(previous->get<double>() * value->get<double>())
                : *value;
        }
    }

    // An entry's UV scale replaces the one before it; the patch applies its uv_scale after its auto_uv.
    for (const auto* const key : { "auto_uv", "uv_scale" }) {
        const auto value = truePBRData.find(key);
        if (value == truePBRData.end() || !value->is_number())
            continue;

        attributes.erase("auto_uv");
        attributes.erase("uv_scale");
        attributes[key] = *value;
    }
}

} // namespace

PatcherMeshShaderTruePBR::PatcherMeshShaderTruePBR(std::filesystem::path nifPath,
                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "TruePBR")
{
}

std::map<size_t,
         nlohmann::json>&
PatcherMeshShaderTruePBR::truePBRConfigs()
{
    static std::map<size_t, nlohmann::json> truePBRConfigs = { };
    return truePBRConfigs;
}

std::map<size_t,
         nlohmann::json>&
PatcherMeshShaderTruePBR::pathLookupJSONs()
{
    static std::map<size_t, nlohmann::json> pathLookupJSONs = { };
    return pathLookupJSONs;
}

std::map<std::wstring,
         std::vector<size_t>>&
PatcherMeshShaderTruePBR::truePBRDiffuseInverse()
{
    static std::map<std::wstring, std::vector<size_t>> truePBRDiffuseInverse = { };
    return truePBRDiffuseInverse;
}

std::map<std::wstring,
         std::vector<size_t>>&
PatcherMeshShaderTruePBR::truePBRNormalInverse()
{
    static std::map<std::wstring, std::vector<size_t>> truePBRNormalInverse = { };
    return truePBRNormalInverse;
}

std::unordered_map<PGEnums::TextureSlots,
                   std::unordered_map<std::wstring,
                                      std::vector<size_t>>>&
PatcherMeshShaderTruePBR::truePBRMatchXMap()
{
    static std::unordered_map<PGEnums::TextureSlots, std::unordered_map<std::wstring, std::vector<size_t>>>
        truePBRMatchXMap = { };
    return truePBRMatchXMap;
}

auto PatcherMeshShaderTruePBR::pathLookupCache() -> std::unordered_map<std::tuple<std::wstring,
                                                                                  std::wstring>,
                                                                       bool,
                                                                       TupleStrHash>&
{
    static std::unordered_map<std::tuple<std::wstring, std::wstring>, bool, TupleStrHash> pathLookupCache = { };
    return pathLookupCache;
}

std::mutex& PatcherMeshShaderTruePBR::pathLookupCacheMutex()
{
    static std::mutex cacheMutex;
    return cacheMutex;
}

std::vector<std::string> PatcherMeshShaderTruePBR::truePBRConfigFilenameFields()
{
    static const std::vector<std::string> pgConfigFilenameFields = { "match_normal", "match_diffuse", "rename" };
    return pgConfigFilenameFields;
}

// Statics.
void PatcherMeshShaderTruePBR::loadStatics(const std::vector<std::filesystem::path>& pbrJSONs,
                                           const bool& shouldMultithread)
{
    auto* pgd = PGGlobals::pgd();

    // The configs replace whatever an earlier call loaded.
    truePBRConfigs().clear();
    pathLookupJSONs().clear();
    truePBRDiffuseInverse().clear();
    truePBRNormalInverse().clear();
    truePBRMatchXMap().clear();
    {
        const std::scoped_lock lock(pathLookupCacheMutex());
        pathLookupCache().clear();
    }

    // Reading and parsing the files is independent work, and every file has to be opened on its own, so that part runs
    // on the task pool. Everything that depends on the order of the configs happens afterwards, in the order of
    // pbrJSONs.
    struct ParsedConfig {
        nlohmann::json json;
        bool isParsed = false;
        std::string parseError;
        std::exception_ptr exception;
    };
    std::vector<ParsedConfig> parsedConfigs(pbrJSONs.size());

    TaskPoolRunner runner(shouldMultithread);
    for (size_t i = 0; i < pbrJSONs.size(); i++) {
        runner.addTask([pgd, &pbrJSONs, &parsedConfigs, i] {
            auto& parsedConfig = parsedConfigs[i];
            try {
                const auto configFileBytes = pgd->file(pbrJSONs[i]);
                std::string configFileStr(configFileBytes.size(), '\0');
                std::ranges::transform(
                    configFileBytes, configFileStr.begin(), [](std::byte b) { return static_cast<char>(b); });

                parsedConfig.json = nlohmann::json::parse(configFileStr);
                parsedConfig.isParsed = true;
            } catch (const nlohmann::json::parse_error& e) {
                parsedConfig.parseError = e.what();
            } catch (...) {
                // Thrown again below on the calling thread. The runner would only record it and let the caller carry
                // on with the configs that did load.
                parsedConfig.exception = std::current_exception();
            }
        });
    }

    // Blocks until all tasks are done.
    runner.runTasks();

    if (ExceptionHandler::hasException()) {
        // Something else in the run failed in the meantime. The runner skips its remaining tasks from then on, so
        // configs may be missing.
        throw std::runtime_error("TruePBR configs were not loaded because the run is being aborted");
    }

    size_t configOrder = 0;
    for (size_t i = 0; i < pbrJSONs.size(); i++) {
        const auto& config = pbrJSONs[i];
        auto& parsedConfig = parsedConfigs[i];

        // file() throws for a config that is missing from the file map or from its archive. That stops the run here, on
        // the calling thread, like it did when the configs were read one by one. A config that cannot be opened is not
        // an exception: it comes back empty and is reported as a parse error below, which was the case before as well.
        if (parsedConfig.exception)
            std::rethrow_exception(parsedConfig.exception);

        // Check if Config is valid.
        if (!parsedConfig.isParsed) {
            Logger::debug(L"Failed to parse TruePBR config JSON: {}. Error: {}",
                          config.wstring(),
                          StringUtil::utf8toUTF16(parsedConfig.parseError));
            Logger::error(L"Failed to parse JSON: {}", config.wstring());
            continue;
        }

        nlohmann::json& j = parsedConfig.json;
        nlohmann::json jDefaults;
        nlohmann::json jEntries;

        // Check if j is a json object.
        if (j.is_object()) {
            if (!j.contains("default") || !j.contains("entries"))
                continue;

            jDefaults = j["default"];
            jEntries = j["entries"];
        } else {
            jDefaults = nlohmann::json::object();
            jEntries = j;
        }

        // Loop through each Element.
        for (auto& element : jEntries) {
            // Merge defaults with element.
            for (const auto& [key, value] : jDefaults.items())
                if (!element.contains(key))
                    element[key] = value;

            // Preprocessing steps here.
            if (element.contains("texture"))
                element["match_diffuse"] = element["texture"];

            element["json"] = StringUtil::utf16toUTF8(config.wstring());

            // Loop through filename Fields.
            for (const auto& field : truePBRConfigFilenameFields())
                if (element.contains(field) && !boost::istarts_with(element[field].get<std::string>(), "\\"))
                    element[field] = element[field].get<std::string>().insert(0, 1, '\\');

            Logger::trace(L"TruePBR Config {} Loaded: {}", configOrder, StringUtil::utf8toUTF16(element.dump()));
            truePBRConfigs()[configOrder++] = element;
        }
    }

    Logger::info(L"Found {} TruePBR entries", truePBRConfigs().size());

    // Create helper vectors.
    for (const auto& config : truePBRConfigs()) {
        // "match_normal" attribute.
        if (config.second.contains("match_normal")) {
            auto revNormal = StringUtil::utf8toUTF16(config.second["match_normal"].get<std::string>());
            revNormal = PGNIFUtil::texBase(revNormal);
            std::ranges::reverse(revNormal);

            truePBRNormalInverse()[StringUtil::toLowerASCIIFast(revNormal)].push_back(config.first);
        }

        // "match_diffuse" attribute.
        if (config.second.contains("match_diffuse")) {
            auto revDiffuse = StringUtil::utf8toUTF16(config.second["match_diffuse"].get<std::string>());
            revDiffuse = PGNIFUtil::texBase(revDiffuse);
            std::ranges::reverse(revDiffuse);

            truePBRDiffuseInverse()[StringUtil::toLowerASCIIFast(revDiffuse)].push_back(config.first);
        }

        // "path_contains" attribute.
        if (config.second.contains("path_contains"))
            pathLookupJSONs()[config.first] = config.second;

        // "matchX" attribute.
        for (int i = 0; i < numTextureSlots - 1; i++) {
            const std::string matchXStr = "match" + std::to_string(i + 1);
            if (config.second.contains(matchXStr)) {
                auto matchStr = StringUtil::utf8toUTF16(config.second.at(matchXStr).get<std::string>());

                // Prepend "textures\\" if it's not already there.
                if (!matchStr.empty() && !matchStr.starts_with(L"textures\\"))
                    matchStr.insert(0, L"textures\\");

                truePBRMatchXMap()[static_cast<PGEnums::TextureSlots>(i)][StringUtil::toLowerASCIIFast(matchStr)]
                    .push_back(config.first);
            }
        }
    }
}

auto PatcherMeshShaderTruePBR::factory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshShader> {
        return std::make_unique<PatcherMeshShaderTruePBR>(nifPath, nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderTruePBR::shaderType() { return PGEnums::ShapeShader::TruePBR; }

bool PatcherMeshShaderTruePBR::canApply([[maybe_unused]] nifly::NiShape& nifShape,
                                        [[maybe_unused]] bool isSinglepassMATO,
                                        [[maybe_unused]] const PGPlugin::ModelRecordType& modelRecordType)
{
    return true;
}

bool PatcherMeshShaderTruePBR::shouldApply(nifly::NiShape& nifShape,
                                           std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();

    // Prep.
    auto* nifShader = nif()->GetShader(&nifShape);
    const auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    matches.clear();

    // Find Old Slots.
    auto oldSlots = PGNIFUtil::textureSlots(nif(), &nifShape);

    shouldApply(oldSlots, matches);

    if (nifShaderBSLSP && PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01)) {
        // Check if RMAOS exists.
        const auto& rmaosPath = oldSlots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)];
        if (!rmaosPath.empty() && pgd->isFile(rmaosPath)) {
            PatcherMatch match;
            match.matchedPath = nifPath().wstring();
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

bool PatcherMeshShaderTruePBR::shouldApply(const PGTypes::TextureSet& oldSlots,
                                           std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();

    // Get search prefixes.
    auto searchPrefixes = PGNIFUtil::searchPrefixes(oldSlots, false);
    // Only normal map gets _n part removed to match properly.
    searchPrefixes[1] = PGNIFUtil::texBase(oldSlots[1], PGEnums::TextureSlots::Normal);

    // Remove "pbr" part if starts with "textures\\pbr" for each search prefix.
    static constexpr size_t texturePBRStrLength = 13; // length of "textures\pbr\"
    for (auto& prefix : searchPrefixes)
        if (StringUtil::toLowerASCIIFast(prefix).starts_with(L"textures\\pbr\\"))
            prefix.replace(0, texturePBRStrLength, L"textures\\");

    // The "nif_filter" attribute is written for the paths of regular meshes. Facegen and BodySlide ShapeData meshes
    // live elsewhere, so the filter does not apply to them: with an empty filter path every entry applies.
    const auto& use = meshUse();
    const std::wstring nifFilterPath
        = (use.isFacegen || use.isBodySlideShapeData) ? std::wstring() : nifPath().wstring();

    std::map<size_t, std::tuple<nlohmann::json, std::wstring>> truePBRData;
    // "match_normal" attribute: Binary search for normal map.
    getSlotMatch(truePBRData, searchPrefixes[1], truePBRNormalInverse(), nifFilterPath);

    // "match_diffuse" attribute: Binary search for diffuse map.
    getSlotMatch(truePBRData, searchPrefixes[0], truePBRDiffuseInverse(), nifFilterPath);

    // "path_contains" attribute: Linear search for path_contains.
    getPathContainsMatch(truePBRData, searchPrefixes[0], nifFilterPath);

    // "matchX" attribute: search exact match for each slot.
    getMatchXMatch(truePBRData, oldSlots, nifFilterPath);

    // Split data into individual JSONs.
    std::unordered_map<std::wstring, std::map<size_t, std::tuple<nlohmann::json, std::wstring>>> truePBROutputData;
    for (const auto& [sequence, data] : truePBRData) {
        // Get current JSON.
        const auto matchedPath = StringUtil::utf8toUTF16(std::get<0>(data)["json"].get<std::string>());

        // Add to output.
        if (!truePBROutputData.contains(matchedPath)) {
            // If MatchedPath doesn't exist, insert it with an empty map and then add Sequence, Data.
            truePBROutputData.emplace(matchedPath, std::map<size_t, std::tuple<nlohmann::json, std::wstring>> { });
        }
        truePBROutputData[matchedPath][sequence] = data;
    }

    // Convert output to vectors.
    for (auto& [json, jsonData] : truePBROutputData) {
        PatcherMatch match;
        match.matchedPath = json;
        match.extraData = std::make_shared<decltype(jsonData)>(jsonData);

        // Loop through json data.
        bool deleteShape = false;
        for (const auto& [sequence, data] : jsonData) {
            if (std::get<0>(data).contains("delete") && std::get<0>(data)["delete"].is_boolean()
                && std::get<0>(data)["delete"].get<bool>()) {
                // Marked for deletion, skip slot checks.
                deleteShape = true;
                break;
            }
        }

        // Check paths.
        bool valid = true;

        if (!deleteShape) {
            PGTypes::TextureSet newSlots = oldSlots;
            applyPatchSlots(newSlots, match);
            for (size_t i = 0; i < numTextureSlots; i++) {
                if (!newSlots.at(i).empty() && !pgd->isFile(newSlots.at(i))) {
                    // Slot does not exist.
                    if (s_printNonExistentPaths) {
                        Logger::warn(
                            L"Texture \"{}\" does not exist from PBR json \"{}\" when patching mesh \"{}\" (Skipping)",
                            newSlots.at(i),
                            match.matchedPath,
                            nifPath().wstring());
                    }

                    // Only invalidate if checkpaths is false.
                    if (s_checkPaths)
                        valid = false;
                }
            }
        }

        if (!valid)
            continue;

        matches.push_back(match);
    }

    // Sort matches by ExtraData key minimum value (this preserves order of JSONs to be 0 having priority if mod order
    // does not exist).
    std::ranges::sort(matches, [](const PatcherMatch& a, const PatcherMatch& b) {
        return std::get<0>(
                   *std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(a.extraData)
                        ->begin())
            > std::get<0>(
                   *std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(b.extraData)
                        ->begin());
    });

    // Check for pre-patch case.
    if (truePBRData.empty()) {
        const auto& rmaosPath = oldSlots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)];
        // If not start with PBR add it for the check.
        if (pgd->textureType(rmaosPath) == PGEnums::TextureType::RMAOS) {
            // Found RMAOS without json.
            PatcherMatch match;
            match.matchedPath = rmaosPath;
            match.extraData = nullptr;
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

void PatcherMeshShaderTruePBR::getSlotMatch(std::map<size_t,
                                                     std::tuple<nlohmann::json,
                                                                std::wstring>>& truePBRData,
                                            const std::wstring& texName,
                                            const std::map<std::wstring,
                                                           std::vector<size_t>>& lookup,
                                            const std::wstring& nifFilterPath)
{
    // Binary search for map.
    auto mapReverse = StringUtil::toLowerASCIIFast(texName);
    std::ranges::reverse(mapReverse);
    auto it = lookup.lower_bound(mapReverse);

    // Get the first element of the reverse path.
    auto reverseFile = mapReverse;
    const auto pos = reverseFile.find_first_of(L'\\');
    if (pos != std::wstring::npos)
        reverseFile = reverseFile.substr(0, pos);

    // Check if match is 1 back.
    if (it != lookup.begin() && boost::starts_with(prev(it)->first, reverseFile)) {
        it = prev(it);
    } else if (it != lookup.end() && boost::starts_with(it->first, reverseFile)) {
        // Check if match is current iterator, just continue here.
    } else {
        // No match found.
        return;
    }

    auto beginIt = it;
    while (beginIt != lookup.begin())
        if (boost::starts_with(prev(beginIt)->first, reverseFile))
            beginIt = prev(beginIt);
        else
            break;

    // Initialize CFG set.
    std::set<size_t> cfgs;

    // Create vector of all matches based on beginIt and It.
    while (beginIt != next(it)) {
        if (!boost::starts_with(mapReverse, beginIt->first)) {
            // Not a valid match.
            beginIt = next(beginIt);
            continue;
        }

        cfgs.insert(beginIt->second.begin(), beginIt->second.end());
        beginIt = next(beginIt);
    }

    if (cfgs.empty())
        return;

    // Loop through all matches.
    for (const auto& cfg : cfgs)
        insertTruePBRData(truePBRData, texName, cfg, nifFilterPath);
}

void PatcherMeshShaderTruePBR::getPathContainsMatch(std::map<size_t,
                                                             std::tuple<nlohmann::json,
                                                                        std::wstring>>& truePBRData,
                                                    const std::wstring& diffuse,
                                                    const std::wstring& nifFilterPath)
{
    // "patch_contains" attribute: Linear search for path_contains.
    auto& cache = pathLookupCache();
    auto& cacheMutex = pathLookupCacheMutex();

    // Check for path_contains only if no name match because it's a O(n) operation
    for (const auto& config : pathLookupJSONs()) {
        // Check if in cache.
        auto cacheKey
            = std::make_tuple(StringUtil::utf8toUTF16(config.second["path_contains"].get<std::string>()), diffuse);

        bool pathMatch = false;
        {
            const std::scoped_lock lock(cacheMutex);
            if (!cache.contains(cacheKey)) {
                // Not in cache, update it.
                cache[cacheKey] = boost::icontains(diffuse, std::get<0>(cacheKey));
            }
            pathMatch = cache[cacheKey];
        }

        if (pathMatch)
            insertTruePBRData(truePBRData, diffuse, config.first, nifFilterPath);
    }
}

void PatcherMeshShaderTruePBR::getMatchXMatch(std::map<size_t,
                                                       std::tuple<nlohmann::json,
                                                                  std::wstring>>& truePBRData,
                                              const PGTypes::TextureSet& oldSlots,
                                              const std::wstring& nifFilterPath)
{
    const auto& truePBRMatchXMap = PatcherMeshShaderTruePBR::truePBRMatchXMap();
    for (size_t i = 0; i < numTextureSlots - 1; i++) {
        const auto curSlot = static_cast<PGEnums::TextureSlots>(i);

        // Check if this slot was ever cached.
        if (!truePBRMatchXMap.contains(curSlot))
            continue;

        // Get texture slot str.
        const auto& lookupWStr = oldSlots.at(i);
        if (lookupWStr.empty())
            continue;
        const auto lookupStr = StringUtil::toLowerASCIIFast(lookupWStr);

        // Lookup.
        const auto& matchXMap = truePBRMatchXMap.at(curSlot);
        if (!matchXMap.contains(lookupStr))
            continue;

        // Add to truePBRData.
        for (const auto& cfg : matchXMap.at(lookupStr))
            insertTruePBRData(truePBRData, PGNIFUtil::texBase(lookupStr, curSlot), cfg, nifFilterPath);
    }
}

void PatcherMeshShaderTruePBR::insertTruePBRData(std::map<size_t,
                                                          std::tuple<nlohmann::json,
                                                                     std::wstring>>& truePBRData,
                                                 const std::wstring& texName,
                                                 size_t cfg,
                                                 const std::wstring& nifFilterPath)
{
    auto curCfg = truePBRConfigs()[cfg];

    // Check if we should skip this due to nif filter (this is expensive, so we do it last). An empty filter path means
    // the mesh is not subject to the filter.
    if (!nifFilterPath.empty() && curCfg.contains("nif_filter")
        && !boost::icontains(nifFilterPath, curCfg["nif_filter"].get<std::string>())) {
        return;
    }

    // Find and check prefix value.
    // Add the PBR part to the texture path.
    auto texPath = texName;
    const auto texPathLower = StringUtil::toLowerASCIIFast(texPath);
    if (texPathLower.starts_with(L"textures\\") && !texPathLower.starts_with(L"textures\\pbr\\"))
        texPath.replace(0, textureStrLength, L"textures\\pbr\\");

    // Get PBR path, which is the path without the matched field.
    std::wstring matchedField;
    if (curCfg.contains("match_normal") || curCfg.contains("match_diffuse")) {
        matchedField = curCfg.contains("match_normal") ? PGNIFUtil::texBase(curCfg["match_normal"].get<std::string>())
                                                       : PGNIFUtil::texBase(curCfg["match_diffuse"].get<std::string>());
    } else {
        // This is a "matchX" entry, so we can just use the whole texture path as is.
        matchedField = texPath;
    }
    texPath.erase(texPath.length() - matchedField.length(), matchedField.length());

    // "rename" attribute.
    if (curCfg.contains("rename")) {
        const auto renameField = curCfg["rename"].get<std::string>();
        if (!StringUtil::asciiFastIEquals(renameField, matchedField))
            matchedField = StringUtil::utf8toUTF16(renameField);
    }

    // PBR prefix path for the shape. PBR is always enabled for a matched entry: the legacy "pbr" JSON field is
    // ignored if present.
    const std::wstring matchedPath = StringUtil::toLowerASCIIFast(texPath + matchedField);

    truePBRData.insert({ cfg, { curCfg, matchedPath } });
}

void PatcherMeshShaderTruePBR::applyPatch(PGTypes::TextureSet& slots,
                                          nifly::NiShape& nifShape,
                                          const PatcherMatch& match)
{
    if (!match.extraData) {
        // No extra data, so this is a pre-patched mesh, do nothing.
        return;
    }

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);
    for (const auto& [sequence, data] : *extraData) {
        // Apply one patch.
        auto truePBRData = std::get<0>(data);
        const auto matchedPath = std::get<1>(data);
        if (s_appliedAttributes) {
            // Recorded first: an entry that deletes the shape is still an entry applied to it.
            recordAppliedAttributes(s_appliedAttributes->m_attributes[&nifShape], truePBRData, *nif(), nifShape);
        }
        applyOnePatch(&nifShape, truePBRData, matchedPath, slots);

        if (nif()->GetBlockID(&nifShape) == nifly::NIF_NPOS) {
            // A "delete" entry freed the shape, so the later entries have nothing to apply to.
            return;
        }
    }
}

void PatcherMeshShaderTruePBR::applyPatchSlots(PGTypes::TextureSet& slots,
                                               const PatcherMatch& match)
{
    if (!match.extraData)
        return;

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);
    for (const auto& [sequence, data] : *extraData) {
        const auto truePBRData = std::get<0>(data);
        const auto matchedPath = std::get<1>(data);
        applyOnePatchSlots(slots, truePBRData, matchedPath);
    }
}

void PatcherMeshShaderTruePBR::applyShader(nifly::NiShape& nifShape)
{
    // Contrary to the other patchers, this one is generic and is not called normally other than setting for plugins,
    // later material swaps in CS are used.

    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
    if (!nifShaderBSLSP) {
        // Only lighting shaders carry the PBR flags.
        return;
    }

    // Set default PBR shader type.
    PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_DEFAULT);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);

    // Clear unused flags.
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_HAIR_SOFT_LIGHTING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_DETAIL_MAP);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_RGB_TINT);
}

uint64_t PatcherMeshShaderTruePBR::matchExtraDataHash(const PatcherMatch& match) const
{
    if (!match.extraData)
        return 0;

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);

    // Config indices depend on the global order of PBR JSONs, which can shift when JSONs are added or removed
    // without changing how this shape is patched. Only the content and the relative order matter, so hash the entries
    // in map (application) order without their indices.
    HashUtil::Fnv1a64 hasher;
    hasher.add(static_cast<uint64_t>(extraData->size()));
    for (const auto& [sequence, data] : *extraData) {
        hasher.add(std::get<0>(data).dump());
        hasher.add(std::get<1>(data));
    }

    return hasher.value();
}

void PatcherMeshShaderTruePBR::loadOptions(std::unordered_map<std::string,
                                                              std::string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "no_path_check")
            s_checkPaths = false;

        if (option == "print_nonexistent_paths")
            s_printNonExistentPaths = true;
    }
}

void PatcherMeshShaderTruePBR::loadOptions(const bool& checkPaths,
                                           const bool& shouldPrintNonExistentPaths)
{
    s_checkPaths = checkPaths;
    s_printNonExistentPaths = shouldPrintNonExistentPaths;
}

bool PatcherMeshShaderTruePBR::applyOnePatch(nifly::NiShape* nifShape,
                                             nlohmann::json& truePBRData,
                                             const std::wstring& matchedPath,
                                             PGTypes::TextureSet& newSlots)
{
    bool isChanged = false;

    if (!nifShape)
        return isChanged;

    // Prep.
    auto* nifShader = nif()->GetShader(nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // "delete" attribute.
    if (truePBRData.contains("delete") && truePBRData["delete"].is_boolean() && truePBRData["delete"]) {
        // nifly deletes the alpha property of a deleted shape even when another shape, maybe an ignored one, uses it.
        PGNIFUtil::deleteAlphaProperty(*nif(), *nifShape);
        nif()->DeleteShape(nifShape);
        isChanged = true;
        return isChanged;
    }

    if (!nifShaderBSLSP) {
        // Only lighting shader shapes carry PBR data.
        return isChanged;
    }

    // "smooth_angle" attribute.
    if (truePBRData.contains("smooth_angle") && truePBRData["smooth_angle"].is_number()) {
        nif()->CalcNormalsForShape(nifShape, true, true, truePBRData["smooth_angle"]);
        nif()->CalcTangentsForShape(nifShape);
        isChanged = true;
    }

    // "auto_uv" attribute.
    if (truePBRData.contains("auto_uv") && truePBRData["auto_uv"].is_number()) {
        std::vector<nifly::Triangle> tris;
        nifShape->GetTriangles(tris);
        const auto newUVScale = autoUVScale(nif()->GetUvsForShape(nifShape), nif()->GetVertsForShape(nifShape), tris)
            / truePBRData["auto_uv"];
        isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // "vertex_colors" attribute.
    if (truePBRData.contains("vertex_colors") && truePBRData["vertex_colors"].is_boolean()) {
        const auto newVertexColors = truePBRData["vertex_colors"].get<bool>();
        if (nifShape->HasVertexColors() != newVertexColors) {
            nifShape->SetVertexColors(newVertexColors);
            isChanged = true;
        }

        if (nifShader->HasVertexColors() != newVertexColors) {
            nifShader->SetVertexColors(newVertexColors);
            isChanged = true;
        }
    }

    // "vertex_color_lum_mult" and "vertex_color_sat_mult" attribute.
    if (nifShape->HasVertexColors()
        && ((truePBRData.contains("vertex_color_lum_mult") && truePBRData["vertex_color_lum_mult"].is_number())
            || truePBRData.contains("vertex_color_sat_mult") && truePBRData["vertex_color_sat_mult"].is_number())) {
        std::vector<nifly::BSVertexData>* vertData = nullptr;
        if (dynamic_cast<nifly::BSTriShape*>(nifShape))
            vertData = &dynamic_cast<nifly::BSTriShape*>(nifShape)->vertData;
        else if (dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape))
            vertData = &dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape)->vertData;

        if (vertData) {
            for (auto& vert : *vertData) {
                // Convert to HSL and multiply luminance then convert back.
                boost::gil::rgb8_pixel_t vertRGB(vert.colorData[0], vert.colorData[1], vert.colorData[2]);
                boost::gil::hsl32f_pixel_t vertHSL;
                boost::gil::color_convert(vertRGB, vertHSL);

                float newLVal = vertHSL[2];
                if (truePBRData.contains("vertex_color_lum_mult") && truePBRData["vertex_color_lum_mult"].is_number()) {
                    const auto newVertexColorMult = truePBRData["vertex_color_lum_mult"].get<float>();
                    newLVal = 1 - ((1 - vertHSL[2]) * newVertexColorMult);
                }

                float newSVal = vertHSL[1];
                if (truePBRData.contains("vertex_color_sat_mult") && truePBRData["vertex_color_sat_mult"].is_number()) {
                    const auto newVertexColorMult = truePBRData["vertex_color_sat_mult"].get<float>();
                    newSVal = vertHSL[1] * newVertexColorMult;
                }

                vertHSL[1] = std::clamp(newSVal, 0.0F, 1.0F);
                vertHSL[2] = std::clamp(newLVal, 0.0F, 1.0F);
                boost::gil::color_convert(vertHSL, vertRGB);

                if (vert.colorData[0] != vertRGB[0]) {
                    vert.colorData[0] = vertRGB[0];
                    isChanged = true;
                }

                if (vert.colorData[1] != vertRGB[1]) {
                    vert.colorData[1] = vertRGB[1];
                    isChanged = true;
                }

                if (vert.colorData[2] != vertRGB[2]) {
                    vert.colorData[2] = vertRGB[2];
                    isChanged = true;
                }
            }
        }
    }

    // "zbuffer_write" attribute.
    if (truePBRData.contains("zbuffer_write") && truePBRData["zbuffer_write"].is_boolean()) {
        const auto newZBufferWrite = truePBRData["zbuffer_write"].get<bool>();
        isChanged |= PGNIFUtil::configureShaderFlag(nifShaderBSLSP, nifly::SLSF2_ZBUFFER_WRITE, newZBufferWrite);
    }

    // "alphaprop_delete", "alphaprop_flags", "alphaprop_thresh" and "alphaprop_create" attributes. "alphaprop_delete"
    // removes the alpha property, and the other three are ignored then. A shape without an alpha property only gets
    // one with "alphaprop_create"; both booleans are off unless the entry turns them on. A value that does not fit its
    // NiAlphaProperty field is ignored like one of the wrong type.
    if (isEnabled(truePBRData, "alphaprop_delete")) {
        isChanged |= PGNIFUtil::deleteAlphaProperty(*nif(), *nifShape);
    } else {
        std::optional<uint16_t> newAlphaFlags;
        uint16_t alphaFlags = 0;
        if (truePBRData.contains("alphaprop_flags")
            && PGMaterialState::unsignedFromJSON(truePBRData["alphaprop_flags"], alphaFlags)) {
            newAlphaFlags = alphaFlags;
        }

        std::optional<uint8_t> newAlphaThreshold;
        uint8_t alphaThreshold = 0;
        if (truePBRData.contains("alphaprop_thresh")
            && PGMaterialState::unsignedFromJSON(truePBRData["alphaprop_thresh"], alphaThreshold)) {
            newAlphaThreshold = alphaThreshold;
        }

        isChanged |= PGNIFUtil::setAlphaProperty(
            *nif(), *nifShape, newAlphaFlags, newAlphaThreshold, isEnabled(truePBRData, "alphaprop_create"));
    }

    // "specular_level" attribute.
    if (truePBRData.contains("specular_level") && truePBRData["specular_level"].is_number()) {
        const auto newSpecularLevel = truePBRData["specular_level"].get<float>();
        if (nifShader->GetGlossiness() != newSpecularLevel) {
            nifShader->SetGlossiness(newSpecularLevel);
            isChanged = true;
        }
    }

    // "subsurface_color" attribute.
    if (truePBRData.contains("subsurface_color") && truePBRData["subsurface_color"].is_array()
        && truePBRData["subsurface_color"].size() >= 3 && truePBRData["subsurface_color"][0].is_number()
        && truePBRData["subsurface_color"][1].is_number() && truePBRData["subsurface_color"][2].is_number()) {
        const auto newSpecularColor = nifly::Vector3(truePBRData["subsurface_color"][0].get<float>(),
                                                     truePBRData["subsurface_color"][1].get<float>(),
                                                     truePBRData["subsurface_color"][2].get<float>());
        if (nifShader->GetSpecularColor() != newSpecularColor) {
            nifShader->SetSpecularColor(newSpecularColor);
            isChanged = true;
        }
    }

    // "roughness_scale" attribute.
    if (truePBRData.contains("roughness_scale") && truePBRData["roughness_scale"].is_number()) {
        const auto newRoughnessScale = truePBRData["roughness_scale"].get<float>();
        if (nifShader->GetSpecularStrength() != newRoughnessScale) {
            nifShader->SetSpecularStrength(newRoughnessScale);
            isChanged = true;
        }
    }

    // "subsurface_opacity" attribute.
    if (truePBRData.contains("subsurface_opacity") && truePBRData["subsurface_opacity"].is_number()) {
        const auto newSubsurfaceOpacity = truePBRData["subsurface_opacity"].get<float>();
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newSubsurfaceOpacity);
    }

    // "displacement_scale" attribute.
    if (truePBRData.contains("displacement_scale") && truePBRData["displacement_scale"].is_number()) {
        const auto newDisplacementScale = truePBRData["displacement_scale"].get<float>();
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->rimlightPower, newDisplacementScale);
    }

    // "emmissive_scale" attribute.
    if (truePBRData.contains("emissive_scale") && truePBRData["emissive_scale"].is_number()) {
        const auto newEmissiveScale = truePBRData["emissive_scale"].get<float>();
        if (nifShader->GetEmissiveMultiple() != newEmissiveScale) {
            nifShader->SetEmissiveMultiple(newEmissiveScale);
            isChanged = true;
        }
    }

    // "emmissive_color" attribute.
    if (truePBRData.contains("emissive_color") && truePBRData["emissive_color"].is_array()
        && truePBRData["emissive_color"].size() >= 4 && truePBRData["emissive_color"][0].is_number()
        && truePBRData["emissive_color"][1].is_number() && truePBRData["emissive_color"][2].is_number()
        && truePBRData["emissive_color"][3].is_number()) {
        const auto newEmissiveColor = nifly::Color4(truePBRData["emissive_color"][0].get<float>(),
                                                    truePBRData["emissive_color"][1].get<float>(),
                                                    truePBRData["emissive_color"][2].get<float>(),
                                                    truePBRData["emissive_color"][3].get<float>());
        if (nifShader->GetEmissiveColor() != newEmissiveColor) {
            nifShader->SetEmissiveColor(newEmissiveColor);
            isChanged = true;
        }
    }

    // "uv_scale" attribute.
    if (truePBRData.contains("uv_scale") && truePBRData["uv_scale"].is_number()) {
        const auto newUVScale
            = nifly::Vector2(truePBRData["uv_scale"].get<float>(), truePBRData["uv_scale"].get<float>());
        isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // Enable PBR on the shape (always on, the legacy "pbr" JSON field is ignored).
    isChanged |= enableTruePBROnShape(nifShader, nifShaderBSLSP, truePBRData, matchedPath, newSlots);

    return isChanged;
}

void PatcherMeshShaderTruePBR::applyOnePatchSlots(PGTypes::TextureSet& slots,
                                                  const nlohmann::json& truePBRData,
                                                  const std::wstring& matchedPath)
{
    // "lock_diffuse" attribute.
    if (!(truePBRData.contains("lock_diffuse") && truePBRData["lock_diffuse"].is_boolean()
          && truePBRData["lock_diffuse"].get<bool>())) {
        const auto newDiffuse = matchedPath + L".dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::Diffuse)] = newDiffuse;
    }

    // "lock_normal" attribute.
    if (!(truePBRData.contains("lock_normal") && truePBRData["lock_normal"].is_boolean()
          && truePBRData["lock_normal"].get<bool>())) {
        const auto newNormal = matchedPath + L"_n.dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::Normal)] = newNormal;
    }

    // "emissive" attribute.
    if (truePBRData.contains("emissive") && truePBRData["emissive"].is_boolean()
        && !(truePBRData.contains("lock_emissive") && truePBRData["lock_emissive"].is_boolean()
             && truePBRData["lock_emissive"].get<bool>())) {
        std::wstring newGlow;
        if (truePBRData["emissive"].get<bool>())
            newGlow = matchedPath + L"_g.dds";

        slots[static_cast<size_t>(PGEnums::TextureSlots::Glow)] = newGlow;
    }

    // "parallax" attribute.
    if (truePBRData.contains("parallax") && truePBRData["parallax"].is_boolean()
        && !(truePBRData.contains("lock_parallax") && truePBRData["lock_parallax"].is_boolean()
             && truePBRData["lock_parallax"].get<bool>())) {
        std::wstring newParallax;
        if (truePBRData["parallax"].get<bool>())
            newParallax = matchedPath + L"_p.dds";

        slots[static_cast<size_t>(PGEnums::TextureSlots::Parallax)] = newParallax;
    }

    // "cubemap" attribute.
    if (truePBRData.contains("cubemap") && truePBRData["cubemap"].is_string()
        && !(truePBRData.contains("lock_cubemap") && truePBRData["lock_cubemap"].is_boolean()
             && truePBRData["lock_cubemap"].get<bool>())) {
        const auto newCubemap = StringUtil::utf8toUTF16(truePBRData["cubemap"].get<std::string>());
        slots[static_cast<size_t>(PGEnums::TextureSlots::Cubemap)] = newCubemap;
    } else {
        slots[static_cast<size_t>(PGEnums::TextureSlots::Cubemap)] = L"";
    }

    // "lock_rmaos" attribute.
    if (!(truePBRData.contains("lock_rmaos") && truePBRData["lock_rmaos"].is_boolean()
          && truePBRData["lock_rmaos"].get<bool>())) {
        const auto newRMAOS = matchedPath + L"_rmaos.dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)] = newRMAOS;
    }

    // "lock_cnr" attribute.
    if (!(truePBRData.contains("lock_cnr") && truePBRData["lock_cnr"].is_boolean()
          && truePBRData["lock_cnr"].get<bool>())) {
        // "coat_normal" attribute.
        std::wstring newCNR;
        if (truePBRData.contains("coat_normal") && truePBRData["coat_normal"].is_boolean()
            && truePBRData["coat_normal"].get<bool>()) {
            newCNR = matchedPath + L"_cnr.dds";
        }

        // Fuzz texture slot.
        if (truePBRData.contains("fuzz") && truePBRData["fuzz"].is_object() && truePBRData["fuzz"].contains("texture")
            && truePBRData["fuzz"]["texture"].is_boolean() && truePBRData["fuzz"]["texture"].get<bool>()) {
            newCNR = matchedPath + L"_f.dds";
        }

        slots[static_cast<size_t>(PGEnums::TextureSlots::MultiLayer)] = newCNR;
    }

    // "lock_subsurface" attribute.
    if (!(truePBRData.contains("lock_subsurface") && truePBRData["lock_subsurface"].is_boolean()
          && truePBRData["lock_subsurface"].get<bool>())) {
        // "subsurface_foliage" attribute.
        std::wstring newSubsurface;
        if ((truePBRData.contains("subsurface_foliage") && truePBRData["subsurface_foliage"].is_boolean()
             && truePBRData["subsurface_foliage"].get<bool>())
            || (truePBRData.contains("subsurface") && truePBRData["subsurface"].is_boolean()
                && truePBRData["subsurface"].get<bool>())
            || (truePBRData.contains("coat_diffuse") && truePBRData["coat_diffuse"].is_boolean()
                && truePBRData["coat_diffuse"].get<bool>())) {
            newSubsurface = matchedPath + L"_s.dds";
        }

        slots[static_cast<size_t>(PGEnums::TextureSlots::Backlight)] = newSubsurface;
    }

    // "SlotX" attributes.
    for (int i = 0; i < numTextureSlots - 1; i++) {
        std::string slotName("slot");
        slotName += std::to_string(i + 1);

        if (truePBRData.contains(slotName) && truePBRData[slotName].is_string()) {
            std::string newSlot = truePBRData[slotName].get<std::string>();
            StringUtil::toLowerASCIIFastInPlace(newSlot);

            // Prepend "textures\\" if it's not already there.
            if (!newSlot.empty() && !newSlot.starts_with("textures\\"))
                newSlot.insert(0, "textures\\");

            slots.at(i) = StringUtil::utf8toUTF16(newSlot);
        }
    }
}

bool PatcherMeshShaderTruePBR::enableTruePBROnShape(nifly::NiShader* nifShader,
                                                    nifly::BSLightingShaderProperty* nifShaderBSLSP,
                                                    nlohmann::json& truePBRData,
                                                    const std::wstring& matchedPath,
                                                    PGTypes::TextureSet& newSlots)
{
    bool isChanged = false;

    applyOnePatchSlots(newSlots, truePBRData, matchedPath);

    // "emissive" attribute.
    if (truePBRData.contains("emissive") && truePBRData["emissive"].is_boolean()) {
        isChanged |= PGNIFUtil::configureShaderFlag(
            nifShaderBSLSP, nifly::SLSF1_EXTERNAL_EMITTANCE, truePBRData["emissive"].get<bool>());
    }

    // Revert to default NIFShader type, remove flags used in other types.
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_HAIR_SOFT_LIGHTING);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_GLOW_MAP);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_DETAIL_MAP);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_RGB_TINT);

    // Enable PBR flag.
    isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);

    // Disable any unused flags that might cause issues.
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_EYE_ENVIRONMENT_MAPPING);

    // "subsurface" attribute.
    if (truePBRData.contains("subsurface") && truePBRData["subsurface"].is_boolean()) {
        isChanged |= PGNIFUtil::configureShaderFlag(
            nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING, truePBRData["subsurface"].get<bool>());
    }

    // "hair" attribute.
    if (truePBRData.contains("hair") && truePBRData["hair"].is_boolean() && truePBRData["hair"].get<bool>())
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING);

    // "multilayer" attribute.
    bool enableMultiLayer = false;
    if (truePBRData.contains("multilayer") && truePBRData["multilayer"].is_boolean()
        && truePBRData["multilayer"].get<bool>()) {
        enableMultiLayer = true;

        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);

        // "coat_color" attribute.
        if (truePBRData.contains("coat_color") && truePBRData["coat_color"].size() >= 3
            && truePBRData["coat_color"][0].is_number() && truePBRData["coat_color"][1].is_number()
            && truePBRData["coat_color"][2].is_number()) {
            const auto newCoatColor = nifly::Vector3(truePBRData["coat_color"][0].get<float>(),
                                                     truePBRData["coat_color"][1].get<float>(),
                                                     truePBRData["coat_color"][2].get<float>());
            if (nifShader->GetSpecularColor() != newCoatColor) {
                nifShader->SetSpecularColor(newCoatColor);
                isChanged = true;
            }
        }

        // "coat_specular_level" attribute.
        if (truePBRData.contains("coat_specular_level") && truePBRData["coat_specular_level"].is_number()) {
            const auto newCoatSpecularLevel = truePBRData["coat_specular_level"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, newCoatSpecularLevel);
        }

        // "coat_roughness" attribute.
        if (truePBRData.contains("coat_roughness") && truePBRData["coat_roughness"].is_number()) {
            const auto newCoatRoughness = truePBRData["coat_roughness"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, newCoatRoughness);
        }

        // "coat_strength" attribute.
        if (truePBRData.contains("coat_strength") && truePBRData["coat_strength"].is_number()) {
            const auto newCoatStrength = truePBRData["coat_strength"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newCoatStrength);
        }

        // "coat_diffuse" attribute.
        if (truePBRData.contains("coat_diffuse") && truePBRData["coat_diffuse"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_EFFECT_LIGHTING, truePBRData["coat_diffuse"].get<bool>());
        }

        // "coat_parallax" attribute.
        if (truePBRData.contains("coat_parallax") && truePBRData["coat_parallax"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING, truePBRData["coat_parallax"].get<bool>());
        }

        // "coat_normal" attribute.
        if (truePBRData.contains("coat_normal") && truePBRData["coat_normal"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING, truePBRData["coat_normal"].get<bool>());
        }

        // "inner_uv_scale" attribute.
        if (truePBRData.contains("inner_uv_scale") && truePBRData["inner_uv_scale"].is_number()) {
            const auto newInnerUVScale = nifly::Vector2(truePBRData["inner_uv_scale"].get<float>(),
                                                        truePBRData["inner_uv_scale"].get<float>());
            isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->parallaxInnerLayerTextureScale, newInnerUVScale);
        }
    } else if (truePBRData.contains("glint") && truePBRData["glint"].is_object()) {
        // Glint is enabled.
        const auto& glintParams = truePBRData["glint"];

        // Set shader type to MLP.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        // Enable Glint with FitSlope flag.
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_FIT_SLOPE);

        // Glint parameters.
        if (glintParams.contains("screen_space_scale") && glintParams["screen_space_scale"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness,
                                                   glintParams["screen_space_scale"]);
        }

        if (glintParams.contains("log_microfacet_density") && glintParams["log_microfacet_density"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale,
                                                   glintParams["log_microfacet_density"]);
        }

        if (glintParams.contains("microfacet_roughness") && glintParams["microfacet_roughness"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.u,
                                                   glintParams["microfacet_roughness"]);
        }

        if (glintParams.contains("density_randomization") && glintParams["density_randomization"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.v,
                                                   glintParams["density_randomization"]);
        }
    } else if (truePBRData.contains("fuzz") && truePBRData["fuzz"].is_object()) {
        // Fuzz is enabled.
        const auto& fuzzParams = truePBRData["fuzz"];

        // Set shader type to MLP.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        // Enable Fuzz with soft lighting flag.
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING);

        // Get color.
        auto fuzzColor = std::vector<float> { 0, 0, 0 };
        if (fuzzParams.contains("color") && fuzzParams["color"].is_array() && fuzzParams["color"].size() == 3
            && fuzzParams["color"][0].is_number() && fuzzParams["color"][1].is_number()
            && fuzzParams["color"][2].is_number()) {
            fuzzColor = fuzzParams["color"].get<std::vector<float>>();
        }

        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, fuzzColor[0]);
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, fuzzColor[1]);
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.u, fuzzColor[2]);

        // Get weight.
        float fuzzWeight = 1;
        if (fuzzParams.contains("weight") && fuzzParams["weight"].is_number())
            fuzzWeight = fuzzParams["weight"].get<float>();

        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.v, fuzzWeight);
    } else {
        // Revert to default NIFShader type.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_DEFAULT);
    }

    if (!enableMultiLayer) {
        // Clear multilayer flags.
        isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);

        if (!(truePBRData.contains("hair") && truePBRData["hair"].is_boolean() && truePBRData["hair"].get<bool>()))
            isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING);

        if (!truePBRData.contains("fuzz") || !truePBRData["fuzz"].is_object())
            isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING);
    }

    return isChanged;
}

PatcherMeshShaderTruePBR::AppliedAttributes::AppliedAttributes()
    : m_previous(s_appliedAttributes)
{
    s_appliedAttributes = this;
}

PatcherMeshShaderTruePBR::AppliedAttributes::~AppliedAttributes() { s_appliedAttributes = m_previous; }

const nlohmann::json* PatcherMeshShaderTruePBR::AppliedAttributes::of(const nifly::NiShape& shape) const
{
    const auto it = m_attributes.find(&shape);
    return it == m_attributes.end() ? nullptr : &it->second;
}

nlohmann::json PatcherMeshShaderTruePBR::material(const nifly::NifFile& nif,
                                                  nifly::NiShape& nifShape,
                                                  const PGTypes::TextureSet& slots,
                                                  const nlohmann::json& attributes)
{
    // Community Shaders loads a lighting shader with the PBR flag as PBR, nothing else.
    const auto* const shader = dynamic_cast<const nifly::BSLightingShaderProperty*>(nif.GetShader(&nifShape));
    if (!shader || !PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_UNUSED01))
        return nullptr;

    nlohmann::json result = nlohmann::json::object();
    result["shader_type"] = "pbr";

    // The attributes of the applied entries as the configs give them, those with a value the patch applies.
    const auto& types = materialAttributeTypes();
    for (const auto& [key, value] : attributes.items()) {
        const auto type = types.find(key);
        if (type == types.end())
            continue;

        if (auto applied = appliedValue(value, type->second))
            result[key] = std::move(*applied);
    }

    // A product of vertex color multipliers as the float the patch used; one past the range of a float applied none.
    for (const auto* const key : vertexColorMultipliers) {
        if (!result.contains(key) || !result[key].is_number_float())
            continue;

        const auto product = result[key].get<double>();
        if (std::abs(product) <= std::numeric_limits<float>::max())
            result[key] = jsonNumber(static_cast<float>(product));
        else
            result.erase(key);
    }

    // The UV scale auto_uv computed from the geometry, for both directions as uv_scale gives it.
    const auto autoUV = attributes.find("auto_uv");
    if (!result.contains("uv_scale") && autoUV != attributes.end() && autoUV->is_number()
        && std::isfinite(shader->uvScale.u)) {
        result["uv_scale"] = jsonNumber(shader->uvScale.u);
    }

    // The textures as the slot attributes take them, relative to the textures folder.
    const std::wstring texturesPrefix = L"textures\\";
    for (std::size_t slot = 0; slot < numTextureSlots - 1; slot++) {
        const auto& path = slots.at(slot);
        if (path.empty())
            continue;

        const bool hasPrefix = path.size() > texturesPrefix.size()
            && StringUtil::asciiFastIEquals(path.substr(0, texturesPrefix.size()), texturesPrefix);
        result["slot" + std::to_string(slot + 1)]
            = StringUtil::utf16toUTF8(hasPrefix ? path.substr(texturesPrefix.size()) : path);
    }

    return result;
}

//
// Helpers.
//

nifly::Vector2 PatcherMeshShaderTruePBR::abs2(nifly::Vector2 v) { return { abs(v.u), abs(v.v) }; }

nifly::Vector2 PatcherMeshShaderTruePBR::autoUVScale(const std::vector<nifly::Vector2>* uvs,
                                                     const std::vector<nifly::Vector3>* verts,
                                                     std::vector<nifly::Triangle>& tris)
{
    nifly::Vector2 scale;
    for (const nifly::Triangle& t : tris) {
        const auto v1 = (*verts)[t.p1];
        const auto v2 = (*verts)[t.p2];
        const auto v3 = (*verts)[t.p3];
        const auto uv1 = (*uvs)[t.p1];
        const auto uv2 = (*uvs)[t.p2];
        const auto uv3 = (*uvs)[t.p3];

        const auto s = (abs2(uv2 - uv1) + abs2(uv3 - uv1)) / ((v2 - v1).length() + (v3 - v1).length());
        scale += nifly::Vector2(1 / s.u, 1 / s.v);
    }

    scale *= 10.0 / 4.0;
    scale /= static_cast<float>(tris.size());
    scale.u = std::min(scale.u, scale.v);
    scale.v = std::min(scale.u, scale.v);

    return scale;
}
