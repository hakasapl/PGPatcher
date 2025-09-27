#pragma once

#include <filesystem>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "util/NIFUtil.hpp"

#include "BethesdaGame.hpp"

class ModManagerDirectory {

public:
    enum class ModManagerType : uint8_t { NONE, VORTEX, MODORGANIZER2 };

    struct Mod {
        // Hash function for Mod struct shared pointer that hashes only name
        struct ModHash {
            auto operator()(const std::shared_ptr<Mod>& mod) const -> std::size_t
            {
                return std::hash<std::wstring>()(mod->name);
            }
        };

        std::shared_mutex mutex; // Mutex for the mod, anyone modifying the mod should lock this mutex
        std::wstring name;
        bool isNew = false;
        bool isEnabled = false;
        int modManagerOrder;
        int priority = -1;
        std::set<NIFUtil::ShapeShader> shaders;
        std::unordered_set<std::shared_ptr<Mod>, ModHash> conflicts;
    };

private:
    std::unordered_map<std::wstring, std::shared_ptr<Mod>> m_modMap;
    std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>> m_modFileMap;

    ModManagerType m_mmType;

    static constexpr const char* MO2INI_PROFILESDIR_KEY = "profiles_directory=";
    static constexpr const char* MO2INI_MODDIR_KEY = "mod_directory=";
    static constexpr const char* MO2INI_BASEDIR_KEY = "base_directory=";
    static constexpr const char* MO2INI_GAMEDIR_KEY = "gamePath=";
    static constexpr const char* MO2INI_PROFILE_KEY = "selected_profile=";
    static constexpr const char* MO2INI_GAMENAME_KEY = "gameName=";
    static constexpr const char* MO2INI_GAMEEDITION_KEY = "game_edition=";
    static constexpr const char* MO2INI_BASEDIR_WILDCARD = "%BASE_DIR%";

    static constexpr const char* MO2INI_BYTEARRAYPREFIX = "@ByteArray(";
    static constexpr const char* MO2INI_BYTEARRAYSUFFIX = ")";

public:
    ModManagerDirectory(const ModManagerType& mmType);

    [[nodiscard]] auto getModFileMap() const -> const std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>>&;
    [[nodiscard]] auto getModByFile(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>;
    [[nodiscard]] auto getMods() const -> std::vector<std::shared_ptr<Mod>>;
    [[nodiscard]] auto getModsByPriority() const -> std::vector<std::shared_ptr<Mod>>;
    [[nodiscard]] auto getMod(const std::wstring& modName) const -> std::shared_ptr<Mod>;

    void loadJSON(const nlohmann::json& json);
    auto getJSON() -> nlohmann::json;

    static auto isValidMO2InstanceDir(const std::filesystem::path& instanceDir) -> bool;
    static auto getGamePathFromInstanceDir(const std::filesystem::path& instanceDir) -> std::filesystem::path;
    static auto getGameTypeFromInstanceDir(const std::filesystem::path& instanceDir) -> BethesdaGame::GameType;
    static auto getSelectedProfileFromInstanceDir(const std::filesystem::path& instanceDir) -> std::wstring;

    void populateModFileMapMO2(
        const std::filesystem::path& instanceDir, const std::filesystem::path& outputDir, const bool& useMO2Order);
    void populateModFileMapVortex(const std::filesystem::path& deploymentDir);

    // Helpers
    [[nodiscard]] static auto getModManagerTypes() -> std::vector<ModManagerType>;
    [[nodiscard]] static auto getStrFromModManagerType(const ModManagerType& type) -> std::string;
    [[nodiscard]] static auto getModManagerTypeFromStr(const std::string& type) -> ModManagerType;
    void assignNewModPriorities() const;

private:
    static auto getMO2INIField(const std::filesystem::path& instanceDir, const std::string& fieldName,
        const bool& isByteArray = false) -> std::wstring;

    static auto getMO2FilePaths(const std::filesystem::path& instanceDir)
        -> std::pair<std::filesystem::path, std::filesystem::path>;
};
