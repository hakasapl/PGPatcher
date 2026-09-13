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

auto PatcherTexture::getDDSPath() const -> std::filesystem::path { return m_ddsPath; }
auto PatcherTexture::getDDS() const -> DirectX::ScratchImage* { return m_dds; }
