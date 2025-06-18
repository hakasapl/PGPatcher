#include <utility>

#include "patchers/base/PatcherMeshShaderTransform.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include <spdlog/spdlog.h>

PatcherMeshShaderTransform::PatcherMeshShaderTransform(std::filesystem::path nifPath, nifly::NifFile* nif,
    std::string patcherName, const NIFUtil::ShapeShader& from, const NIFUtil::ShapeShader& to)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName))
    , m_fromShader(from)
    , m_toShader(to)
{
}
