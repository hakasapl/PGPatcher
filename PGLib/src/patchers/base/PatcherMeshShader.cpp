#include "patchers/base/PatcherMeshShader.hpp"

#include "NifFile.hpp"
#include <BasicTypes.hpp>
#include <Shaders.hpp>
#include <string>
#include <utility>

using namespace std;

// Constructor
PatcherMeshShader::PatcherMeshShader(filesystem::path nifPath, nifly::NifFile* nif, string patcherName)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName))
{
}
