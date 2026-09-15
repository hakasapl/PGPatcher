#include "patchers/base/PatcherTexture.hpp"

#include "patchers/base/Patcher.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <string>
#include <utility>

PatcherTexture::PatcherTexture(std::filesystem::path ddsPath,
                               DirectX::ScratchImage* dds,
                               std::string patcherName)
    : Patcher(std::move(patcherName))
    , m_ddsPath(std::move(ddsPath))
    , m_dds(dds)
{
}

std::filesystem::path PatcherTexture::ddsPath() const { return m_ddsPath; }
DirectX::ScratchImage* PatcherTexture::dds() const { return m_dds; }
