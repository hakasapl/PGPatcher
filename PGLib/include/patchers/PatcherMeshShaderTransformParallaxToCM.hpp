#pragma once

#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "pgutil/PGEnums.hpp"

#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshShaderTransformParallaxToCM
 * @brief Transform patcher that turns a parallax match into a complex material match whose texture is generated from
 * the height map by PatcherTextureHookConvertToCM
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
    static PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory factory();

    /**
     * @brief Get the From Shader (Parallax)
     *
     * @return PGEnums::ShapeShader Parallax
     */
    static PGEnums::ShapeShader fromShader();

    /**
     * @brief Get the To Shader object (CM)
     *
     * @return PGEnums::ShapeShader (CM)
     */
    static PGEnums::ShapeShader toShader();

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
    bool shouldTransform(const PatcherMeshShader::PatcherMatch& baseMatch,
                         bool canApplyBaseShader) override;

    /**
     * @brief Register the height map for conversion and point the match at the complex material texture that will be
     * generated from it
     *
     * @param fromMatch Parallax match to transform
     * @param[out] result Complex material match
     * @return true always
     */
    bool transform(const PatcherMeshShader::PatcherMatch& fromMatch,
                   PatcherMeshShader::PatcherMatch& result) override;
};
