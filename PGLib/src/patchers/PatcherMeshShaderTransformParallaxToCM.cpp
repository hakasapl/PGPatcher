#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"

#include "util/NIFUtil.hpp"

#include <filesystem>
#include <utility>

#include "patchers/PatcherTextureHookConvertToCM.hpp"

using namespace std;

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

auto PatcherMeshShaderTransformParallaxToCM::transform(
    const PatcherMeshShader::PatcherMatch& fromMatch, PatcherMeshShader::PatcherMatch& result) -> bool
{
    const auto heightMap = fromMatch.matchedPath;

    result = fromMatch;

    // create texture hook
    PatcherTextureHookConvertToCM::addToProcessList(heightMap);
    result.matchedPath = PatcherTextureHookConvertToCM::getOutputFilename(heightMap);

    return true;
}
