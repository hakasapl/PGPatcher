#pragma once

#include "PGModManager.hpp"
#include "patchers/base/PatcherMeshGlobal.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "patchers/base/PatcherMeshPre.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "patchers/base/PatcherTextureGlobal.hpp"
#include "pgutil/PGEnums.hpp"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @class PatcherUtil
 * @brief Types that group the registered patchers
 */
class PatcherUtil {
public:
    /**
     * @struct PatcherMeshObjectSet
     * @brief Mesh patcher instances created for one mesh and plugin use. Transform patchers are keyed by the shader
     * they transform from and paired with the shader they transform to
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
     * @struct PatcherMeshSet
     * @brief Factories of the registered mesh patchers, same layout as PatcherMeshObjectSet
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

    /// @brief Texture patcher instances created for one texture
    struct PatcherTextureObjectSet {
        std::vector<PatcherTextureGlobal::PatcherGlobalObject> globalPatchers;
    };

    /// @brief Factories of the registered texture patchers
    struct PatcherTextureSet {
        std::vector<PatcherTextureGlobal::PatcherGlobalFactory> globalPatchers;
    };

    /**
     * @struct ShaderPatcherMatch
     * @brief A shader patcher's match for a shape, with the mod the matched file comes from (nullptr when unknown) and
     * the shader a transform would turn it into (Unknown when none applies)
     */
    struct ShaderPatcherMatch {
        std::shared_ptr<PGModManager::Mod> mod;
        PGEnums::ShapeShader shader { };
        PatcherMeshShader::PatcherMatch match { };
        PGEnums::ShapeShader shaderTransformTo { };
    };
};
