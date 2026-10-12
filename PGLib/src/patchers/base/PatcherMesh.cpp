#include "patchers/base/PatcherMesh.hpp"

#include "patchers/base/Patcher.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

PatcherMesh::PatcherMesh(std::filesystem::path nifPath,
                         nifly::NifFile* nif,
                         std::string patcherName)
    : Patcher(std::move(patcherName))
    , m_nifPath(std::move(nifPath))
    , m_nif(nif)
    , m_meshUse()
{
}

std::filesystem::path PatcherMesh::nifPath() const { return m_nifPath; }

const PGPlugin::MeshUseAttributes& PatcherMesh::meshUse() const { return m_meshUse; }

void PatcherMesh::setMeshUse(const PGPlugin::MeshUseAttributes& meshUse) { m_meshUse = meshUse; }

nifly::NifFile* PatcherMesh::nif() const
{
    if (!m_nif)
        throw std::runtime_error("NIF is null");

    return m_nif;
}

void PatcherMesh::setNIF(nifly::NifFile* nif) { m_nif = nif; }
