#pragma once

#include <string>

#include "patchers/base/PatcherMesh.hpp"
#include "patchers/base/PatcherMeshShader.hpp"

/**
 * @class PatcherMeshShaderTransform
 * @brief Base class for shader transform patchers
 */
class PatcherMeshShaderTransform : public PatcherMesh {
private:
    NIFUtil::ShapeShader m_fromShader; /** Shader to transform from */
    NIFUtil::ShapeShader m_toShader; /** Shader to transform to */

public:
    // Custom type definitions
    using PatcherMeshShaderTransformFactory
        = std::function<std::unique_ptr<PatcherMeshShaderTransform>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshShaderTransformObject = std::unique_ptr<PatcherMeshShaderTransform>;

    // Constructors
    PatcherMeshShaderTransform(std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName,
        const NIFUtil::ShapeShader& from, const NIFUtil::ShapeShader& to);
    virtual ~PatcherMeshShaderTransform() = default;
    PatcherMeshShaderTransform(const PatcherMeshShaderTransform& other) = default;
    auto operator=(const PatcherMeshShaderTransform& other) -> PatcherMeshShaderTransform& = default;
    PatcherMeshShaderTransform(PatcherMeshShaderTransform&& other) noexcept = default;
    auto operator=(PatcherMeshShaderTransform&& other) noexcept -> PatcherMeshShaderTransform& = default;

    virtual auto shouldTransform(const PatcherMeshShader::PatcherMatch& baseMatch, bool canApplyBaseShader) -> bool = 0;

    /**
     * @brief Transform shader match to new shader match
     *
     * @param FromMatch shader match to transform
     * @return PatcherShader::PatcherMatch transformed match
     */
    virtual auto transform(const PatcherMeshShader::PatcherMatch& fromMatch, PatcherMeshShader::PatcherMatch& result)
        -> bool
        = 0;
};
