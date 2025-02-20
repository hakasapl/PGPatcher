#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"

#include "NIFUtil.hpp"

#include <filesystem>
#include <utility>

#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include "Logger.hpp"

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
    filesystem::path complexMap;

    {
        PatcherTextureHookConvertToCM texHook(heightMap);
        if (!texHook.applyPatch(complexMap)) {
            Logger::error(L"Failed to convert height map to complex material: {}", heightMap);
            return false;
        }

        result.matchedPath = complexMap.wstring();
    }

    return true;
}
