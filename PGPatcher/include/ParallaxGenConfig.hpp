#pragma once

#include <filesystem>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenPlugin.hpp"

/**
 * @class ParallaxGenConfig
 * @brief Class responsible for ParallaxGen runtime configuration and JSON file interaction
 */
class ParallaxGenConfig {
public:
    /**
     * @struct PGParams
     * @brief Struct that holds all the user-configurable parameters for ParallaxGen
     */
    struct PGParams {
        // Game
        struct Game {
            std::filesystem::path dir;
            BethesdaGame::GameType type = BethesdaGame::GameType::SKYRIM_SE;

            auto operator==(const Game& other) const -> bool { return dir == other.dir && type == other.type; }
        } Game;

        // Mod Manager
        struct ModManager {
            ModManagerDirectory::ModManagerType type = ModManagerDirectory::ModManagerType::NONE;
            std::filesystem::path mo2InstanceDir;
            bool mo2UseLooseFileOrder = false;

            auto operator==(const ModManager& other) const -> bool
            {
                return type == other.type && mo2InstanceDir == other.mo2InstanceDir
                    && mo2UseLooseFileOrder == other.mo2UseLooseFileOrder;
            }
        } ModManager;

        // Output
        struct Output {
            std::filesystem::path dir;
            bool zip = true;

            auto operator==(const Output& other) const -> bool { return dir == other.dir && zip == other.zip; }
        } Output;

        // Advanced
        bool advanced = false;

        // Processing
        struct Processing {
            bool multithread = true;
            bool highMem = false;
            bool bsa = true;
            bool pluginPatching = true;
            bool pluginESMify = false;
            ParallaxGenPlugin::PluginLang pluginLang = ParallaxGenPlugin::PluginLang::ENGLISH;
            bool diagnostics = false;

            auto operator==(const Processing& other) const -> bool
            {
                return multithread == other.multithread && highMem == other.highMem && bsa == other.bsa
                    && pluginPatching == other.pluginPatching && pluginESMify == other.pluginESMify
                    && pluginLang == other.pluginLang && diagnostics == other.diagnostics;
            }
        } Processing;

        // Pre-Patchers
        struct PrePatcher {
            bool disableMLP = false;
            bool fixMeshLighting = false;

            auto operator==(const PrePatcher& other) const -> bool
            {
                return disableMLP == other.disableMLP && fixMeshLighting == other.fixMeshLighting;
            }
        } PrePatcher;

        // Shader Patchers
        struct ShaderPatcher {
            bool parallax = true;
            bool complexMaterial = true;

            struct ShaderPatcherComplexMaterial {
                std::vector<std::wstring> listsDyncubemapBlocklist;

                auto operator==(const ShaderPatcherComplexMaterial& other) const -> bool
                {
                    return listsDyncubemapBlocklist == other.listsDyncubemapBlocklist;
                }
            } ShaderPatcherComplexMaterial;

            bool truePBR = true;
            struct ShaderPatcherTruePBR {
                bool checkPaths = true;
                bool printNonExistentPaths = false;

                auto operator==(const ShaderPatcherTruePBR& other) const -> bool
                {
                    return checkPaths == other.checkPaths && printNonExistentPaths == other.printNonExistentPaths;
                }
            } ShaderPatcherTruePBR;

            auto operator==(const ShaderPatcher& other) const -> bool
            {
                return parallax == other.parallax && complexMaterial == other.complexMaterial
                    && truePBR == other.truePBR && ShaderPatcherComplexMaterial == other.ShaderPatcherComplexMaterial
                    && ShaderPatcherTruePBR == other.ShaderPatcherTruePBR;
            }
        } ShaderPatcher;

        // Shader Transforms
        struct ShaderTransforms {
            bool parallaxToCM = false;
            struct ShaderTransformParallaxToCM {
                bool onlyWhenRequired = true;

                auto operator==(const ShaderTransformParallaxToCM& other) const -> bool
                {
                    return onlyWhenRequired == other.onlyWhenRequired;
                }
            } ShaderTransformParallaxToCM;

            auto operator==(const ShaderTransforms& other) const -> bool
            {
                return parallaxToCM == other.parallaxToCM
                    && ShaderTransformParallaxToCM == other.ShaderTransformParallaxToCM;
            }
        } ShaderTransforms;

        // Post-Patchers
        struct PostPatcher {
            bool disablePrePatchedMaterials = true;
            bool fixSSS = false;
            bool hairFlowMap = false;

            auto operator==(const PostPatcher& other) const -> bool
            {
                return disablePrePatchedMaterials == other.disablePrePatchedMaterials && fixSSS == other.fixSSS
                    && hairFlowMap == other.hairFlowMap;
            }
        } PostPatcher;

        // Global Patchers
        struct GlobalPatcher {
            bool fixEffectLightingCS = false;

            auto operator==(const GlobalPatcher& other) const -> bool
            {
                return fixEffectLightingCS == other.fixEffectLightingCS;
            }
        } GlobalPatcher;

        // Lists
        struct MeshRules {
            std::vector<std::wstring> allowList;
            std::vector<std::wstring> blockList;

            auto operator==(const MeshRules& other) const -> bool
            {
                return allowList == other.allowList && blockList == other.blockList;
            }
        } MeshRules;

        struct TextureRules {
            std::vector<std::wstring> vanillaBSAList;
            std::vector<std::pair<std::wstring, NIFUtil::TextureType>> textureMaps;

            auto operator==(const TextureRules& other) const -> bool
            {
                return vanillaBSAList == other.vanillaBSAList && textureMaps == other.textureMaps;
            }
        } TextureRules;

        auto operator==(const PGParams& other) const -> bool
        {
            return Game == other.Game && ModManager == other.ModManager && Output == other.Output
                && Processing == other.Processing && PrePatcher == other.PrePatcher
                && ShaderPatcher == other.ShaderPatcher && ShaderTransforms == other.ShaderTransforms
                && PostPatcher == other.PostPatcher && GlobalPatcher == other.GlobalPatcher
                && MeshRules == other.MeshRules && TextureRules == other.TextureRules && advanced == other.advanced;
        }

        auto operator!=(const PGParams& other) const -> bool { return !(*this == other); }
    };

private:
    static std::filesystem::path s_exePath; /** Stores the ExePath of ParallaxGen.exe */

    PGParams m_params; /** Stores the configured parameters */

    nlohmann::json m_userConfig; /** Stores the user config JSON object */

public:
    /**
     * @brief Loads required statics for the class
     *
     * @param exePath Path to ParallaxGen.exe (folder)
     */
    static void loadStatics(const std::filesystem::path& exePath);

    /**
     * @brief Get the User Config File object
     *
     * @return std::filesystem::path Path to user config file
     */
    [[nodiscard]] static auto getUserConfigFile() -> std::filesystem::path;

    /**
     * @brief Get the Mod Config File object
     *
     * @return std::filesystem::path Path to mod config file
     */
    [[nodiscard]] static auto getModConfigFile() -> std::filesystem::path;

    /**
     * @brief Loads the config files in the `cfg` folder
     */
    void loadConfig();

    /**
     * @brief Get the current Params
     *
     * @return PGParams params
     */
    [[nodiscard]] auto getParams() const -> PGParams;

    /**
     * @brief Set params (also saves to user json)
     *
     * @param params new params to set
     */
    void setParams(const PGParams& params);

    /**
     * @brief Validates a given param struct
     *
     * @param params Params to validate
     * @param errors Error messages
     * @return true no validation errors
     * @return false validation errors
     */
    [[nodiscard]] static auto validateParams(const PGParams& params, std::vector<std::string>& errors) -> bool;

    /**
     * @brief Get the Default Params object
     *
     * @return PGParams default params
     */
    [[nodiscard]] static auto getDefaultParams() -> PGParams;

    /**
     * @brief Get the User Config JSON object
     *
     * @return nlohmann::json User config JSON
     */
    [[nodiscard]] auto getUserConfigJSON() const -> nlohmann::json;

    /**
     * @brief Saves user config to the user json file
     *
     * @return true if save was successful
     */
    auto saveUserConfig() -> bool;

    /**
     * @brief Saves the current mod configuration to modrules.json
     *
     * @return true if save was successful
     */
    static auto saveModConfig() -> bool;

private:
    /**
     * @brief Parses JSON from a file
     *
     * @param bytes Bytes to parse
     * @param j parsed JSON object
     * @return true no json errors
     * @return false unable to parse
     */
    static auto parseJSON(const std::vector<std::byte>& bytes, nlohmann::json& j) -> bool;

    /**
     * @brief Adds a JSON config to the current config
     *
     * @param j JSON object to add
     */
    auto addConfigJSON(const nlohmann::json& j) -> void;

    /**
     * @brief Replaces any / with \\ in a JSON object
     *
     * @param j JSON object to change
     */
    static void replaceForwardSlashes(nlohmann::json& j);
};
