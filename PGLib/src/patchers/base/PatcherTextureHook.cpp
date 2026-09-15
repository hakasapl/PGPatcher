#include "patchers/base/PatcherTextureHook.hpp"

#include "patchers/base/PatcherTexture.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <string>
#include <utility>

PatcherTextureHook::PatcherTextureHook(std::filesystem::path texPath,
                                       DirectX::ScratchImage* tex,
                                       std::string patcherName)
    : PatcherTexture(std::move(texPath),
                     tex,
                     std::move(patcherName))
{
}
