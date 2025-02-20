#include "patchers/base/PatcherTextureHook.hpp"

#include "Logger.hpp"
#include "patchers/base/PatcherTexture.hpp"

using namespace std;

PatcherTextureHook::PatcherTextureHook(std::filesystem::path texPath, std::string patcherName)
    : PatcherTexture(std::move(texPath), &m_ddsImage, std::move(patcherName))
{
    // only allow DDS files
    const string ddsFileExt = getDDSPath().extension().string();
    if (ddsFileExt != ".dds") {
        throw runtime_error("File is not a DDS file");
    }

    if (!getPGD3D()->getDDS(getDDSPath(), m_ddsImage)) {
        // TODO should we be error logging here?
        Logger::error(L"Unable to load DDS file: {}", getDDSPath().wstring());
    }
}
