#include "VanillaAssets.hpp"

#include "common/BethesdaGame.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <windows.h>

namespace TestUtil::VanillaAssets {

namespace {
std::wstring environmentVariable(const wchar_t* name)
{
    std::array<wchar_t, 4096> buffer { };
    const auto length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size())
        return { };

    return { buffer.data(), length };
}
}

std::filesystem::path gameDir()
{
    if (auto configured = environmentVariable(L"PG_TEST_SKYRIM_DIR"); !configured.empty())
        return { std::move(configured) };

    return LR"(C:\Steam\steamapps\common\Skyrim Special Edition)";
}

std::filesystem::path appDataDir()
{
    const auto localAppData = environmentVariable(L"LOCALAPPDATA");
    if (localAppData.empty())
        return { };

    return std::filesystem::path(localAppData) / L"Skyrim Special Edition";
}

bool isAvailable() { return BethesdaGame::isGamePathValid(gameDir(), BethesdaGame::GameType::SkyrimSE, appDataDir()); }

bool isFullRunRequested() { return !environmentVariable(L"PG_TEST_VANILLA_FULL").empty(); }

}
