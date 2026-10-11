#pragma once

#include "patchers/base/PatcherMesh.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

/**
 * @class PatcherMeshShaderTransform
 * @brief Base class for shader transform patchers, which turn a match of one shader into a match of another
 */
class PatcherMeshShaderTransform : public PatcherMesh {
private:
    PGEnums::ShapeShader m_fromShader; /** Shader to transform from */
    PGEnums::ShapeShader m_toShader; /** Shader to transform to */

public:
    // Custom type definitions.
    using PatcherMeshShaderTransformFactory
        = std::function<std::unique_ptr<PatcherMeshShaderTransform>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshShaderTransformObject = std::unique_ptr<PatcherMeshShaderTransform>;

    // Constructors.
    PatcherMeshShaderTransform(std::filesystem::path nifPath,
                               nifly::NifFile* nif,
                               std::string patcherName,
                               const PGEnums::ShapeShader& from,
                               const PGEnums::ShapeShader& to);
    virtual ~PatcherMeshShaderTransform() = default;
    PatcherMeshShaderTransform(const PatcherMeshShaderTransform& other) = default;
    PatcherMeshShaderTransform& operator=(const PatcherMeshShaderTransform& other) = default;
    PatcherMeshShaderTransform(PatcherMeshShaderTransform&& other) noexcept = default;
    PatcherMeshShaderTransform& operator=(PatcherMeshShaderTransform&& other) noexcept = default;

    /**
     * @brief Decide whether a match of the source shader should be transformed
     *
     * @param baseMatch Match of the source shader
     * @param canApplyBaseShader Whether the shape can take the source shader as is
     * @return true if the match should be transformed
     */
    virtual bool shouldTransform(const PatcherMeshShader::PatcherMatch& baseMatch,
                                 bool canApplyBaseShader) = 0;

    /**
     * @brief Transform a match of the source shader into a match of the target shader
     *
     * @param fromMatch Match to transform
     * @param[out] result Transformed match (may alias fromMatch)
     * @return true if the match was transformed
     */
    virtual bool transform(const PatcherMeshShader::PatcherMatch& fromMatch,
                           PatcherMeshShader::PatcherMatch& result) = 0;
};
