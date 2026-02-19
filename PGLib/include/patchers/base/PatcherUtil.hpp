#pragma once

#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "patchers/base/PatcherMeshGlobal.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "patchers/base/PatcherMeshPre.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "patchers/base/PatcherTextureGlobal.hpp"
#include "util/NIFUtil.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
        std::unordered_map<
            NIFUtil::ShapeShader,
            std::pair<NIFUtil::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformObject>>
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
        std::unordered_map<
            NIFUtil::ShapeShader,
            std::pair<NIFUtil::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory>>
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
        std::shared_ptr<PGModManager::Mod> mod;
        NIFUtil::ShapeShader shader {};
        PatcherMeshShader::PatcherMatch match {};
        NIFUtil::ShapeShader shaderTransformTo {};

        [[nodiscard]] auto getJSON() const -> nlohmann::json
        {
            nlohmann::json json = nlohmann::json::object();
            if (mod != nullptr) {
                json["mod"] = StringUtil::utf16toUTF8(mod->name);
            }
            json["shader"] = NIFUtil::getStrFromShader(shader);
            json["shaderTransformTo"] = NIFUtil::getStrFromShader(shaderTransformTo);
            json["matchedPath"] = StringUtil::utf16toUTF8(match.matchedPath);
            json["matchedFrom"] = nlohmann::json::array();
            for (const auto& matchedFrom : match.matchedFrom) {
                json["matchedFrom"].push_back(static_cast<int>(matchedFrom));
            }

            return json;
        }

        [[nodiscard]] static auto fromJSON(const nlohmann::json& json) -> ShaderPatcherMatch
        {
            ShaderPatcherMatch match;
            if (json.contains("mod") && json["mod"].is_string()) {
                match.mod = PGGlobals::getPGMM()->getMod(StringUtil::utf8toUTF16(json["mod"].get<std::string>()));
            } else {
                match.mod = nullptr;
            }
            match.shader = NIFUtil::getShaderFromStr(json["shader"].get<std::string>());
            match.shaderTransformTo = NIFUtil::getShaderFromStr(json["shaderTransformTo"].get<std::string>());
            match.match.matchedPath = StringUtil::utf8toUTF16(json["matchedPath"].get<std::string>());
            for (const auto& matchedFrom : json["matchedFrom"]) {
                match.match.matchedFrom.insert(static_cast<NIFUtil::TextureSlots>(matchedFrom.get<int>()));
            }

            return match;
        }

        // equality operator
        auto operator==(const ShaderPatcherMatch& other) const -> bool
        {
            return mod == other.mod && shader == other.shader && shaderTransformTo == other.shaderTransformTo
                && match.matchedPath == other.match.matchedPath && match.matchedFrom == other.match.matchedFrom;
        }
        // inequality operator
        auto operator!=(const ShaderPatcherMatch& other) const -> bool { return !(*this == other); }
    };
};
