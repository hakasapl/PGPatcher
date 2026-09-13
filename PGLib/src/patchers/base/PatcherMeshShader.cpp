#include "patchers/base/PatcherMeshShader.hpp"

#include "patchers/base/PatcherMesh.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <string>
#include <utility>

// Constructor.
PatcherMeshShader::PatcherMeshShader(std::filesystem::path nifPath,
                                     nifly::NifFile* nif,
                                     std::string patcherName)
    : PatcherMesh(std::move(nifPath),
                  nif,
                  std::move(patcherName))
{
}
