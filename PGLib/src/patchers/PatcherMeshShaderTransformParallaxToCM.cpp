#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"

#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "pgutil/PGEnums.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <memory>
#include <utility>

void PatcherMeshShaderTransformParallaxToCM::loadOptions(const bool& onlyWhenRequired)
{
    s_onlyWhenRequired = onlyWhenRequired;
}

auto PatcherMeshShaderTransformParallaxToCM::factory() -> PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory
{
    return [](std::filesystem::path nifPath, nifly::NifFile* nif) -> PatcherMeshShaderTransformObject {
        return std::make_unique<PatcherMeshShaderTransformParallaxToCM>(std::move(nifPath), nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderTransformParallaxToCM::fromShader()
{
    return PGEnums::ShapeShader::VanillaParallax;
}
PGEnums::ShapeShader PatcherMeshShaderTransformParallaxToCM::toShader()
{
    return PGEnums::ShapeShader::ComplexMaterial;
}

PatcherMeshShaderTransformParallaxToCM::PatcherMeshShaderTransformParallaxToCM(std::filesystem::path nifPath,
                                                                               nifly::NifFile* nif)
    : PatcherMeshShaderTransform(std::move(nifPath),
                                 nif,
                                 "UpgradeParallaxToCM",
                                 PGEnums::ShapeShader::VanillaParallax,
                                 PGEnums::ShapeShader::ComplexMaterial)
{
}

bool PatcherMeshShaderTransformParallaxToCM::shouldTransform(
    [[maybe_unused]] const PatcherMeshShader::PatcherMatch& baseMatch,
    bool canApplyBaseShader)
{
    return !canApplyBaseShader || !s_onlyWhenRequired;
}

bool PatcherMeshShaderTransformParallaxToCM::transform(const PatcherMeshShader::PatcherMatch& fromMatch,
                                                       PatcherMeshShader::PatcherMatch& result)
{
    const auto heightMap = fromMatch.matchedPath;

    result = fromMatch;

    // Create texture hook.
    PatcherTextureHookConvertToCM::addToProcessList(heightMap);
    result.matchedPath = PatcherTextureHookConvertToCM::outputFilename(heightMap);

    return true;
}
