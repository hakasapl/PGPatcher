#include "patchers/base/PatcherTextureHook.hpp"

#include "PGRunObserver.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherTexture.hpp"
#include "pgutil/PGEnums.hpp"

#include <DirectXTex.h>
#include <boost/crc.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
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

std::uint32_t PatcherTextureHook::signatureOf(std::span<const unsigned char> bytecode,
                                              std::span<const std::uint32_t> values)
{
    boost::crc_32_type crc;
    crc.process_bytes(bytecode.data(), bytecode.size_bytes());
    crc.process_bytes(values.data(), values.size_bytes());
    return crc.checksum();
}

std::filesystem::path PatcherTextureHook::outputPathOf(PGRunObserver::HookKind kind,
                                                       const std::filesystem::path& texPath)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PatcherTextureHookConvertToCM::outputFilename(texPath)
                                                        : PatcherTextureHookFixSSS::outputFilename(texPath);
}

PGEnums::TextureType PatcherTextureHook::outputTypeOf(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PGEnums::TextureType::ComplexMaterial
                                                        : PGEnums::TextureType::SubsurfaceColor;
}

std::uint32_t PatcherTextureHook::generatorSignatureOf(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PatcherTextureHookConvertToCM::generatorSignature()
                                                        : PatcherTextureHookFixSSS::generatorSignature();
}

const char* PatcherTextureHook::hookNameOf(PGRunObserver::HookKind kind)
{
    return kind == PGRunObserver::HookKind::ConvertToCM ? PatcherTextureHookConvertToCM::hookName
                                                        : PatcherTextureHookFixSSS::hookName;
}
