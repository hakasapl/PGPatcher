#include "patchers/base/PatcherTexture.hpp"

#include "patchers/base/Patcher.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <string>
#include <utility>

using namespace std;

PatcherTexture::PatcherTexture(filesystem::path ddsPath, DirectX::ScratchImage* dds, string patcherName)
    : Patcher(std::move(patcherName))
    , m_ddsPath(std::move(ddsPath))
    , m_dds(dds)
{
}

auto PatcherTexture::getDDSPath() const -> filesystem::path { return m_ddsPath; }
auto PatcherTexture::getDDS() const -> DirectX::ScratchImage* { return m_dds; }
