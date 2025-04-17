#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "NIFUtil.hpp"

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

        std::mutex mutex; // Mutex for the mod, anyone modifying the mod should lock this mutex
        std::wstring name;
        bool isNew = false;
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
    static constexpr const char* MO2INI_BASEDIR_WILDCARD = "%BASE_DIR%";

public:
    ModManagerDirectory(const ModManagerType& mmType);

    [[nodiscard]] auto getModFileMap() const -> const std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>>&;
    [[nodiscard]] auto getModByFile(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>;
    [[nodiscard]] auto getMods() const -> std::vector<std::shared_ptr<Mod>>;
    [[nodiscard]] auto getMod(const std::wstring& modName) const -> std::shared_ptr<Mod>;

    void loadJSON(const nlohmann::json& json);
    auto getJSON() -> nlohmann::json;

    static auto getMO2ProfilesFromInstanceDir(const std::filesystem::path& instanceDir) -> std::vector<std::wstring>;

    void populateModFileMapMO2(const std::filesystem::path& instanceDir, const std::wstring& profile,
        const std::filesystem::path& outputDir, const bool& useMO2Order);
    void populateModFileMapVortex(const std::filesystem::path& deploymentDir);

    // Helpers
    [[nodiscard]] static auto getModManagerTypes() -> std::vector<ModManagerType>;
    [[nodiscard]] static auto getStrFromModManagerType(const ModManagerType& type) -> std::string;
    [[nodiscard]] static auto getModManagerTypeFromStr(const std::string& type) -> ModManagerType;

private:
    static auto getMO2FilePaths(const std::filesystem::path& instanceDir)
        -> std::pair<std::filesystem::path, std::filesystem::path>;
};
