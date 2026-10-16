#pragma once

#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief The user-configurable patching parameters, shared by the offline programs and the PGLite runtime.
 *
 * The JSON form is the "params" object of PGPatcher's settings.json. Its keys are serialized names that never change,
 * so a settings file can be copied between the programs.
 */
struct PGParams {
    // Game.
    struct Game {
        std::filesystem::path dir;
        BethesdaGame::GameType type = BethesdaGame::GameType::SkyrimSE;

        bool operator==(const Game& other) const { return dir == other.dir && type == other.type; }
    } game;

    // Mod Manager.
    struct ModManager {
        PGModManager::ModManagerType type = PGModManager::ModManagerType::None;
        std::filesystem::path mo2InstanceDir;
        bool shouldUseMO2LooseFileOrder = true;

        bool operator==(const ModManager& other) const
        {
            return type == other.type && mo2InstanceDir == other.mo2InstanceDir
                && shouldUseMO2LooseFileOrder == other.shouldUseMO2LooseFileOrder;
        }
    } modManager;

    // Output.
    struct Output {
        std::filesystem::path dir;
        bool zip = false;
        PGPlugin::PluginLang pluginLang = PGPlugin::PluginLang::English;

        bool operator==(const Output& other) const
        {
            return dir == other.dir && zip == other.zip && pluginLang == other.pluginLang;
        }
    } output;

    // Processing.
    struct Processing {
        bool multithread = true;
        bool enableModDevMode = false;
        bool enableDebugLogging = false;
        bool enableTraceLogging = false;
        /// @brief Whether only what the 3rd party tools (TexGen, DynDOLOD and BodySlide) read is patched, with the
        /// stock state of every patched shape and record recorded for PGLite, which then runs next to the output.
        bool isPGLiteMode = false;
        std::unordered_set<PGPlugin::ModelRecordType> allowedModelRecordTypes = PGPlugin::defaultRecTypeSet();
        std::vector<std::wstring> vanillaBSAList;
        std::vector<std::pair<std::wstring, PGEnums::TextureType>> textureMaps;
        std::vector<std::wstring> allowList;
        std::vector<std::wstring> blockList;

        bool operator==(const Processing& other) const
        {
            return multithread == other.multithread && enableModDevMode == other.enableModDevMode
                && enableDebugLogging == other.enableDebugLogging && enableTraceLogging == other.enableTraceLogging
                && isPGLiteMode == other.isPGLiteMode && allowedModelRecordTypes == other.allowedModelRecordTypes
                && vanillaBSAList == other.vanillaBSAList && textureMaps == other.textureMaps
                && allowList == other.allowList && blockList == other.blockList;
        }
    } processing;

    // Pre-Patchers.
    struct PrePatcher {
        bool isFixMeshLightingEnabled = false;

        bool operator==(const PrePatcher& other) const
        {
            return isFixMeshLightingEnabled == other.isFixMeshLightingEnabled;
        }
    } prePatcher;

    // Shader Patchers.
    struct ShaderPatcher {
        bool isParallaxEnabled = true;
        bool isComplexMaterialEnabled = true;
        bool isTruePBREnabled = false;

        bool operator==(const ShaderPatcher& other) const
        {
            return isParallaxEnabled == other.isParallaxEnabled
                && isComplexMaterialEnabled == other.isComplexMaterialEnabled
                && isTruePBREnabled == other.isTruePBREnabled;
        }
    } shaderPatcher;

    // Shader Transforms.
    struct ShaderTransforms {
        bool isParallaxToCMEnabled = false;

        bool operator==(const ShaderTransforms& other) const
        {
            return isParallaxToCMEnabled == other.isParallaxToCMEnabled;
        }
    } shaderTransforms;

    // Post-Patchers.
    struct PostPatcher {
        bool disablePrePatchedMaterials = true;
        bool isFixSSSEnabled = false;
        bool isHairFlowMapEnabled = false;

        bool operator==(const PostPatcher& other) const
        {
            return disablePrePatchedMaterials == other.disablePrePatchedMaterials
                && isFixSSSEnabled == other.isFixSSSEnabled && isHairFlowMapEnabled == other.isHairFlowMapEnabled;
        }
    } postPatcher;

    // Global Patchers.
    struct GlobalPatcher {
        bool isRecalculateBoundsEnabled = false;

        bool operator==(const GlobalPatcher& other) const
        {
            return isRecalculateBoundsEnabled == other.isRecalculateBoundsEnabled;
        }
    } globalPatcher;

    bool operator==(const PGParams& other) const
    {
        return game == other.game && modManager == other.modManager && output == other.output
            && processing == other.processing && prePatcher == other.prePatcher && shaderPatcher == other.shaderPatcher
            && shaderTransforms == other.shaderTransforms && postPatcher == other.postPatcher
            && globalPatcher == other.globalPatcher;
    }

    bool operator!=(const PGParams& other) const { return !(*this == other); }

    /// @brief A problem validate() found: the translation key of its message and, for a duplicate entry, the entry.
    struct ValidationError {
        std::string key;
        bool hasItem = false;
        std::wstring item;

        bool operator==(const ValidationError& other) const = default;
    };

    /**
     * @brief Checks the parameters that are not paths of the machine: the parallax to complex material transform
     * needs both of its patchers, and the mesh allow list, the mesh block list, the texture rules and the vanilla
     * archive list have no empty and no duplicate entries. The offline launcher adds the checks of the game, mod
     * manager and output folders; the PGLite menu refuses an Apply with these problems.
     *
     * @return The problems in the order the offline launcher reports them, empty when the parameters are valid.
     */
    [[nodiscard]] std::vector<ValidationError> validate() const;

    /**
     * @brief The parameters of a fresh installation: the Steam game folder, the default mesh block list and the
     * vanilla archive list.
     */
    [[nodiscard]] static PGParams defaults();

    /**
     * @brief Reads the "params" object of a settings JSON. A key the object does not have keeps its value; the list
     * settings (allow list, block list, texture maps, vanilla archive list) are appended to. Texture rule paths are
     * read in the form the file map uses (lower case, backslashes), so a rule typed with forward slashes or capitals
     * matches.
     *
     * @param paramsJSON The "params" object.
     */
    void readJSON(const nlohmann::json& paramsJSON);

    /**
     * @brief Writes every parameter into the "params" object of a settings JSON. Keys the object has that are not
     * parameters are kept.
     *
     * @param paramsJSON The "params" object to write into.
     */
    void writeJSON(nlohmann::json& paramsJSON) const;

    /**
     * @brief Replaces every forward slash in the strings of a JSON document with a backslash, the form the paths of
     * the settings are stored in.
     *
     * @param json JSON document to change in place.
     */
    static void replaceForwardSlashes(nlohmann::json& json);

    /**
     * @brief Spells the mesh rules and the texture rules as a read of the settings file leaves them: backslashes, and
     * lower case for the texture rules, which is the form the matching compares. Two spellings of one rule become
     * equal, so validate() reports them as the duplicate they are. readJSON() calls this; the PGLite menu calls it
     * before it validates the rules as typed.
     */
    void normalizeRules();
};
