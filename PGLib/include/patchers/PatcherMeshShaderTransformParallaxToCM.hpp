#pragma once

#include "NIFUtil.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include <mutex>

/**
 * @class PatcherMeshShaderTransformParallaxToCM
 * @brief Transform patcher to upgrade Parallax to CM
 */
class PatcherMeshShaderTransformParallaxToCM : public PatcherMeshShaderTransform {
private:
    static inline bool s_onlyWhenRequired = true;

public:
    static void loadOptions(const bool& onlyWhenRequired);

    /**
     * @brief Get the Factory object for Parallax > CM transform
     *
     * @return PatcherShaderTransform::PatcherShaderTransformFactory Factory object
     */
    static auto getFactory() -> PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory;

    /**
     * @brief Get the From Shader (Parallax)
     *
     * @return NIFUtil::ShapeShader Parallax
     */
    static auto getFromShader() -> NIFUtil::ShapeShader;

    /**
     * @brief Get the To Shader object (CM)
     *
     * @return NIFUtil::ShapeShader (CM)
     */
    static auto getToShader() -> NIFUtil::ShapeShader;

    /**
     * @brief Construct a new Patcher Upgrade Parallax To CM patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshShaderTransformParallaxToCM(std::filesystem::path nifPath, nifly::NifFile* nif);

    auto shouldTransform(const PatcherMeshShader::PatcherMatch& baseMatch, bool canApplyBaseShader) -> bool override;

    /**
     * @brief Transform shader match to new shader match
     *
     * @param fromMatch Match to transform
     * @return PatcherShader::PatcherMatch Transformed match
     */
    auto transform(const PatcherMeshShader::PatcherMatch& fromMatch, PatcherMeshShader::PatcherMatch& result)
        -> bool override;
};
