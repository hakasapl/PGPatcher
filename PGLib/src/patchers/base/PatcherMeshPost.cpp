#include "patchers/base/PatcherMeshPost.hpp"

using namespace std;

PatcherMeshPost::PatcherMeshPost(
    std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName), triggerSave)
{
}
