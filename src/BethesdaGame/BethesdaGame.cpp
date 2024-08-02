#include "BethesdaGame/BethesdaGame.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>

#include "ParallaxGenUtil/ParallaxGenUtil.hpp"

using namespace std;
using namespace ParallaxGenUtil;
namespace fs = std::filesystem;

BethesdaGame::BethesdaGame(GameType GameType, const fs::path &GamePath,
                           const bool &Logging)
    : ObjGameType(GameType), Logging(Logging) {
  if (GamePath.empty()) {
    // If the game path is empty, find
    this->GamePath = this->findGamePathFromSteam();
  } else {
    // If the game path is not empty, use the provided game path
    this->GamePath = GamePath;
  }

  if (this->GamePath.empty()) {
    // If the game path is still empty, throw an exception
    if (this->Logging) {
      spdlog::critical(
          "Unable to locate game data path. Please specify the game data path "
          "manually using the -d argument.");
      exitWithUserInput(1);
    } else {
      throw runtime_error("Game path not found");
    }
  }

  // check if path actually exists
  if (!fs::exists(this->GamePath)) {
    // If the game path does not exist, throw an exception
    if (this->Logging) {
      spdlog::critical(L"Game path does not exist: {}",
                       this->GamePath.wstring());
      exitWithUserInput(1);
    } else {
      throw runtime_error("Game path does not exist");
    }
  }

  this->GameDataPath = this->GamePath / "Data";

  if (!fs::exists(this->GameDataPath / getDataCheckFile())) {
    // If the game data path does not contain Skyrim.esm, throw an exception
    if (this->Logging) {
      spdlog::critical(
          L"Game data path does not contain Skyrim.esm, which probably means "
          L"it's invalid: {}",
          this->GameDataPath.wstring());
      exitWithUserInput(1);
    } else {
      throw runtime_error("Game data path does not contain Skyrim.esm");
    }
  }
}

// Statics
auto BethesdaGame::getINILocations() const -> ININame {
  if (ObjGameType == BethesdaGame::GameType::SKYRIM_SE) {
    return ININame{"skyrim.ini", "skyrimprefs.ini", "skyrimcustom.ini"};
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_GOG) {
    return ININame{"skyrim.ini", "skyrimprefs.ini", "skyrimcustom.ini"};
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_VR) {
    return ININame{"skyrim.ini", "skyrimprefs.ini", "skyrimcustom.ini"};
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM) {
    return ININame{"skyrim.ini", "skyrimprefs.ini", "skyrimcustom.ini"};
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL) {
    return ININame{"enderal.ini", "enderalprefs.ini", "enderalcustom.ini"};
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL_SE) {
    return ININame{"enderal.ini", "enderalprefs.ini", "enderalcustom.ini"};
  }

  return {};
}

auto BethesdaGame::getDocumentLocation() const -> fs::path {
  if (ObjGameType == BethesdaGame::GameType::SKYRIM_SE) {
    return "My Games/Skyrim Special Edition";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_GOG) {
    return "My Games/Skyrim Special Edition GOG";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_VR) {
    return "My Games/Skyrim VR";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM) {
    return "My Games/Skyrim";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL) {
    return "My Games/Enderal";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL_SE) {
    return "My Games/Enderal Special Edition";
  }

  return {};
}

auto BethesdaGame::getAppDataLocation() const -> fs::path {
  if (ObjGameType == BethesdaGame::GameType::SKYRIM_SE) {
    return "Skyrim Special Edition";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_GOG) {
    return "Skyrim Special Edition GOG";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_VR) {
    return "Skyrim VR";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM) {
    return "Skyrim";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL) {
    return "Enderal";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL_SE) {
    return "Enderal Special Edition";
  }

  return {};
}

auto BethesdaGame::getSteamGameID() const -> int {
  if (ObjGameType == BethesdaGame::GameType::SKYRIM_SE) {
    return STEAMGAMEID_SKYRIM_SE;
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_VR) {
    return STEAMGAMEID_SKYRIM_VR;
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM) {
    return STEAMGAMEID_SKYRIM;
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL) {
    return STEAMGAMEID_ENDERAL;
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL_SE) {
    return STEAMGAMEID_ENDERAL_SE;
  }

  return {};
}

auto BethesdaGame::getDataCheckFile() const -> fs::path {
  if (ObjGameType == BethesdaGame::GameType::SKYRIM_SE) {
    return "Skyrim.esm";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_GOG) {
    return "Skyrim.esm";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM_VR) {
    return "SkyrimVR.esm";
  }

  if (ObjGameType == BethesdaGame::GameType::SKYRIM) {
    return "Skyrim.esm";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL) {
    return "Enderal - Forgotten Stories.esm";
  }

  if (ObjGameType == BethesdaGame::GameType::ENDERAL_SE) {
    return "Enderal - Forgotten Stories.esm";
  }

  return {};
}

auto BethesdaGame::getGameType() const -> BethesdaGame::GameType {
  return ObjGameType;
}

auto BethesdaGame::getGamePath() const -> fs::path {
  // Get the game path from the registry
  // If the game is not found, return an empty string
  return GamePath;
}

auto BethesdaGame::getGameDataPath() const -> fs::path { return GameDataPath; }

auto BethesdaGame::findGamePathFromSteam() const -> fs::path {
  // Find the game path from the registry
  // If the game is not found, return an empty string

  // Check if key doesn't exists in steam map
  if (getSteamGameID() == 0) {
    return {};
  }

  string RegPath =
      R"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App )" +
      to_string(this->getSteamGameID());

  HKEY HKey = nullptr;

  char Data[REG_BUFFER_SIZE]{}; // NOLINT
  DWORD DataType = REG_SZ;
  DWORD DataSize = sizeof(Data);

  LONG Result =
      RegOpenKeyExA(HKEY_LOCAL_MACHINE, RegPath.c_str(), 0, KEY_READ, &HKey);
  if (Result == ERROR_SUCCESS) {
    // Query the value
    Result = RegQueryValueExA(HKey, "InstallLocation", nullptr, &DataType,
                              (LPBYTE)Data, &DataSize); // NOLINT
    if (Result == ERROR_SUCCESS) {
      // Ensure null-terminated string
      Data[DataSize] = '\0'; // NOLINT
      return string(Data);   // NOLINT
    }
    // Close the registry key
    RegCloseKey(HKey);
  }

  return {};
}

auto BethesdaGame::getINIPaths() const -> BethesdaGame::ININame {
  BethesdaGame::ININame Output = getINILocations();
  fs::path GameDocsPath = getGameDocumentPath();

  // normal ini file
  Output.INI = GameDocsPath / Output.INI;
  Output.INIPrefs = GameDocsPath / Output.INIPrefs;
  Output.INICustom = GameDocsPath / Output.INICustom;

  return Output;
}

auto BethesdaGame::getLoadOrderFile() const -> fs::path {
  fs::path GameAppdataPath = getGameAppdataPath();
  fs::path LoadOrderFile = GameAppdataPath / "loadorder.txt";
  return LoadOrderFile;
}

auto BethesdaGame::getGameDocumentPath() const -> fs::path {
  fs::path DocPath = getSystemPath(FOLDERID_Documents);
  if (DocPath.empty()) {
    return {};
  }

  DocPath /= getDocumentLocation();
  return DocPath;
}

auto BethesdaGame::getGameAppdataPath() const -> fs::path {
  fs::path AppDataPath = getSystemPath(FOLDERID_LocalAppData);
  if (AppDataPath.empty()) {
    return {};
  }

  AppDataPath /= getAppDataLocation();
  return AppDataPath;
}

auto BethesdaGame::getSystemPath(const GUID &FolderID) -> fs::path {
  PWSTR Path = nullptr;
  HRESULT Result = SHGetKnownFolderPath(FolderID, 0, nullptr, &Path);
  if (SUCCEEDED(Result)) {
    wstring OutPath(Path);
    CoTaskMemFree(Path); // Free the memory allocated for the path

    return OutPath;
  }

  // Handle error
  return {};
}
