#include "patchers/base/PatcherMeshShaderTransform.hpp"

#include "NifFile.hpp"
#include "patchers/base/PatcherMesh.hpp"
#include "util/NIFUtil.hpp"
#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <utility>

PatcherMeshShaderTransform::PatcherMeshShaderTransform(std::filesystem::path nifPath, nifly::NifFile* nif,
    std::string patcherName, const NIFUtil::ShapeShader& from, const NIFUtil::ShapeShader& to)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName))
    , m_fromShader(from)
    , m_toShader(to)
{
}
