#include "patchers/base/PatcherMeshPost.hpp"

#include "patchers/base/PatcherMesh.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <string>
#include <utility>

using namespace std;

PatcherMeshPost::PatcherMeshPost(
    std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName), triggerSave)
{
}
