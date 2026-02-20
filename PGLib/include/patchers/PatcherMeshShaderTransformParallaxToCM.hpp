#pragma once

#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "pgutil/PGEnums.hpp"


#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshShaderTransformParallaxToCM
 * @brief Transform patcher to upgrade Parallax to CM
 */
class PatcherMeshShaderTransformParallaxToCM : public PatcherMeshShaderTransform {
private:
    static inline bool s_onlyWhenRequired = true;

public:
    /**
     * @brief Loads patcher options from a flag value.
     *
     * @param onlyWhenRequired When true, the Parallax-to-CM transform is only applied when
     *                         the base parallax shader cannot be used; when false it is always applied.
     */
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
     * @return PGEnums::ShapeShader Parallax
     */
    static auto getFromShader() -> PGEnums::ShapeShader;

    /**
     * @brief Get the To Shader object (CM)
     *
     * @return PGEnums::ShapeShader (CM)
     */
    static auto getToShader() -> PGEnums::ShapeShader;

    /**
     * @brief Construct a new Patcher Upgrade Parallax To CM patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshShaderTransformParallaxToCM(std::filesystem::path nifPath,
                                           nifly::NifFile* nif);

    /**
     * @brief Determines whether the Parallax-to-CM transform should be applied for a given match.
     *
     * @param baseMatch        The matched shader/texture information for the current shape.
     * @param canApplyBaseShader true if the base parallax shader can be applied without transformation.
     * @return true if the transform should be applied; false if it should be skipped.
     */
    auto shouldTransform(const PatcherMeshShader::PatcherMatch& baseMatch,
                         bool canApplyBaseShader) -> bool override;

    /**
     * @brief Transform shader match to new shader match
     *
     * @param fromMatch Match to transform
     * @return PatcherShader::PatcherMatch Transformed match
     */
    auto transform(const PatcherMeshShader::PatcherMatch& fromMatch,
                   PatcherMeshShader::PatcherMatch& result) -> bool override;
};
