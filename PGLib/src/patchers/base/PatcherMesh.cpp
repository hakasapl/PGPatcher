#include "patchers/base/PatcherMesh.hpp"

using namespace std;

PatcherMesh::PatcherMesh(filesystem::path nifPath, nifly::NifFile* nif, string patcherName, const bool& triggerSave)
    : Patcher(std::move(patcherName), triggerSave)
    , m_nifPath(std::move(nifPath))
    , m_nif(nif)
{
}

auto PatcherMesh::getNIFPath() const -> filesystem::path { return m_nifPath; }

auto PatcherMesh::getNIF() const -> nifly::NifFile*
{
    if (m_nif == nullptr) {
        throw std::runtime_error("NIF is null");
    }

    return m_nif;
}

void PatcherMesh::setNIF(nifly::NifFile* nif) { m_nif = nif; }
