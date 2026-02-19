#pragma once

#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "patchers/base/PatcherMeshGlobal.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "patchers/base/PatcherMeshPre.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "patchers/base/PatcherTextureGlobal.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
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
        std::unordered_map<PGEnums::ShapeShader, PatcherMeshShader::PatcherMeshShaderObject> shaderPatchers;
        std::unordered_map<
            PGEnums::ShapeShader,
            std::pair<PGEnums::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformObject>>
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
        std::unordered_map<PGEnums::ShapeShader, PatcherMeshShader::PatcherMeshShaderFactory> shaderPatchers;
        std::unordered_map<
            PGEnums::ShapeShader,
            std::pair<PGEnums::ShapeShader, PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory>>
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
        PGEnums::ShapeShader shader {};
        PatcherMeshShader::PatcherMatch match {};
        PGEnums::ShapeShader shaderTransformTo {};

        [[nodiscard]] auto getJSON() const -> nlohmann::json
        {
            nlohmann::json json = nlohmann::json::object();
            if (mod != nullptr) {
                json["mod"] = StringUtil::utf16toUTF8(mod->name);
            }
            json["shader"] = PGEnums::getStrFromShader(shader);
            json["shaderTransformTo"] = PGEnums::getStrFromShader(shaderTransformTo);
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
            match.shader = PGEnums::getShaderFromStr(json["shader"].get<std::string>());
            match.shaderTransformTo = PGEnums::getShaderFromStr(json["shaderTransformTo"].get<std::string>());
            match.match.matchedPath = StringUtil::utf8toUTF16(json["matchedPath"].get<std::string>());
            for (const auto& matchedFrom : json["matchedFrom"]) {
                match.match.matchedFrom.insert(static_cast<PGEnums::TextureSlots>(matchedFrom.get<int>()));
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
