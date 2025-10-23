#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"

#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "util/NIFUtil.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <memory>
#include <utility>

using namespace std;

void PatcherMeshShaderTransformParallaxToCM::loadOptions(const bool& onlyWhenRequired)
{
    s_onlyWhenRequired = onlyWhenRequired;
}

auto PatcherMeshShaderTransformParallaxToCM::getFactory()
    -> PatcherMeshShaderTransform::PatcherMeshShaderTransformFactory
{
    return [](filesystem::path nifPath, nifly::NifFile* nif) -> PatcherMeshShaderTransformObject {
        return make_unique<PatcherMeshShaderTransformParallaxToCM>(std::move(nifPath), nif);
    };
}

auto PatcherMeshShaderTransformParallaxToCM::getFromShader() -> NIFUtil::ShapeShader
{
    return NIFUtil::ShapeShader::VANILLAPARALLAX;
}
auto PatcherMeshShaderTransformParallaxToCM::getToShader() -> NIFUtil::ShapeShader
{
    return NIFUtil::ShapeShader::COMPLEXMATERIAL;
}

PatcherMeshShaderTransformParallaxToCM::PatcherMeshShaderTransformParallaxToCM(
    std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshShaderTransform(std::move(nifPath), nif, "UpgradeParallaxToCM", NIFUtil::ShapeShader::VANILLAPARALLAX,
          NIFUtil::ShapeShader::COMPLEXMATERIAL)
{
}

auto PatcherMeshShaderTransformParallaxToCM::shouldTransform(
    [[maybe_unused]] const PatcherMeshShader::PatcherMatch& baseMatch, bool canApplyBaseShader) -> bool
{
    return !canApplyBaseShader || !s_onlyWhenRequired;
}

auto PatcherMeshShaderTransformParallaxToCM::transform(
    const PatcherMeshShader::PatcherMatch& fromMatch, PatcherMeshShader::PatcherMatch& result) -> bool
{
    const auto heightMap = fromMatch.matchedPath;

    result = fromMatch;

    // create texture hook
    PatcherTextureHookConvertToCM::addToProcessList(heightMap);
    result.matchedPath = PatcherTextureHookConvertToCM::getOutputFilename(heightMap);

    getPGD()->addGeneratedFile(result.matchedPath);

    return true;
}
