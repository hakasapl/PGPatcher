#pragma once

#include <nlohmann/json_fwd.hpp>
#include <unordered_map>

#include "patchers/base/PatcherMeshGlobal.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "patchers/base/PatcherMeshPre.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "patchers/base/PatcherTextureGlobal.hpp"

#include "NIFUtil.hpp"
#include "ParallaxGenUtil.hpp"

/**
 * @class PatcherUtil
 * @brief Utility class for code that uses patchers
 */
class PatcherUtil {
public:
    /**
     * @struct PatcherObjectSet
     * @brief Stores the patcher objects for a given run
     */
    struct PatcherMeshObjectSet {
        std::vector<PatcherMeshGlobal::PatcherMeshGlobalObject> globalPatchers;
        std::vector<PatcherMeshPre::PatcherMeshPreObject> prePatchers;
        std::unordered_map<NIFUtil::ShapeShader, PatcherMeshShader::PatcherMeshShaderObject> shaderPatchers;
        std::unordered_map<NIFUtil::ShapeShader,
            std::map<NIFUtil::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformObject>>
            shaderTransformPatchers;
        std::vector<PatcherMeshPost::PatcherMeshPostObject> postPatchers;
    };

    /**
     * @struct PatcherSet
     * @brief Stores the patcher factories for a given run
     */
    struct PatcherMeshSet {
        std::vector<PatcherMeshGlobal::PatcherMeshGlobalFactory> globalPatchers;
        std::vector<PatcherMeshPre::PatcherMeshPreFactory> prePatchers;
        std::unordered_map<NIFUtil::ShapeShader, PatcherMeshShader::PatcherMeshShaderFactory> shaderPatchers;
        std::unordered_map<NIFUtil::ShapeShader,
            std::map<NIFUtil::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory>>
            shaderTransformPatchers;
        std::vector<PatcherMeshPost::PatcherMeshPostFactory> postPatchers;
    };

    struct PatcherTextureObjectSet {
        std::vector<PatcherTextureGlobal::PatcherGlobalObject> globalPatchers;
    };

    struct PatcherTextureSet {
        std::vector<PatcherTextureGlobal::PatcherGlobalFactory> globalPatchers;
    };

    /**
     * @struct ShaderPatcherMatch
     * @brief Describes a match with transform properties
     */
    struct ShaderPatcherMatch {
        std::wstring mod;
        NIFUtil::ShapeShader shader {};
        PatcherMeshShader::PatcherMatch match {};
        NIFUtil::ShapeShader shaderTransformTo {};

        [[nodiscard]] auto getJSON() const -> nlohmann::json
        {
            nlohmann::json json = nlohmann::json::object();
            json["mod"] = ParallaxGenUtil::utf16toUTF8(mod);
            json["shader"] = NIFUtil::getStrFromShader(shader);
            json["shaderTransformTo"] = NIFUtil::getStrFromShader(shaderTransformTo);
            json["matchedPath"] = ParallaxGenUtil::utf16toUTF8(match.matchedPath);
            json["matchedFrom"] = nlohmann::json::array();
            for (const auto& matchedFrom : match.matchedFrom) {
                json["matchedFrom"].push_back(static_cast<int>(matchedFrom));
            }

            return json;
        }

        [[nodiscard]] static auto fromJSON(const nlohmann::json& json) -> ShaderPatcherMatch
        {
            ShaderPatcherMatch match;
            match.mod = ParallaxGenUtil::utf8toUTF16(json["mod"].get<std::string>());
            match.shader = NIFUtil::getShaderFromStr(json["shader"].get<std::string>());
            match.shaderTransformTo = NIFUtil::getShaderFromStr(json["shaderTransformTo"].get<std::string>());
            match.match.matchedPath = ParallaxGenUtil::utf8toUTF16(json["matchedPath"].get<std::string>());
            for (const auto& matchedFrom : json["matchedFrom"]) {
                match.match.matchedFrom.insert(static_cast<NIFUtil::TextureSlots>(matchedFrom.get<int>()));
            }

            return match;
        }
    };

    struct ConflictModResults {
        std::unordered_map<std::wstring, std::tuple<std::set<NIFUtil::ShapeShader>, std::unordered_set<std::wstring>>>
            mods;
        std::mutex mutex;
    };

    /**
     * @brief Get the Winning Match object (checks mod priority)
     *
     * @param Matches Matches to check
     * @param NIFPath NIF path to check
     * @param ModPriority Mod priority map
     * @return ShaderPatcherMatch Winning match
     */
    static auto getWinningMatch(const std::vector<ShaderPatcherMatch>& matches,
        const std::unordered_map<std::wstring, int>* modPriority = nullptr) -> ShaderPatcherMatch;

    /**
     * @brief Helper method to run a transform if needed on a match
     *
     * @param Match Match to run transform
     * @param Patchers Patcher set to use
     * @return ShaderPatcherMatch Transformed match
     */
    static auto applyTransformIfNeeded(
        ShaderPatcherMatch& match, const PatcherMeshObjectSet& patchers, const bool& matchOnly = false) -> bool;

private:
    static inline std::mutex s_processShapeMutex;
    static inline std::unordered_map<NIFUtil::TextureSet, std::vector<PatcherUtil::ShaderPatcherMatch>,
        NIFUtil::TextureSetHash>
        s_shaderMatchCache;

public:
    static auto getMatches(const NIFUtil::TextureSet& slots, const PatcherUtil::PatcherMeshObjectSet& patchers,
        const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply,
        PatcherUtil::ConflictModResults* conflictMods = nullptr) -> std::vector<PatcherUtil::ShaderPatcherMatch>;
};
