#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "BethesdaGame.hpp"
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
        int modManagerOrder = -1;
        std::set<NIFUtil::ShapeShader> shaders;
    };

    struct ModSetHash {
        auto operator()(const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& modSet) const -> std::size_t
        {
            std::size_t seed = 0;
            std::hash<std::wstring> hasher;
            for (const auto& mod : modSet) {
                // Combine hashes using boost-like hash_combine (xoring with rotation)
                seed ^= hasher(mod->name) + 0x9e3779b9 + (seed << 6) // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                    + (seed >> 2);
            }
            return seed;
        }
    };

    struct ModSetEqual {
        auto operator()(const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& lhs,
            const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& rhs) const -> bool
        {
            if (lhs.size() != rhs.size()) {
                return false;
            }

            std::unordered_set<std::wstring> lhsNames;

            for (const auto& mod : lhs) {
                lhsNames.insert(mod->name);
            }

            std::unordered_set<std::wstring> rhsNames;
            for (const auto& mod : rhs) {
                rhsNames.insert(mod->name);
            }

            return lhsNames == rhsNames;
        }
    };

private:
    std::unordered_map<std::wstring, std::shared_ptr<Mod>> m_modMap;
    std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>> m_modFileMap;

    std::mutex m_modLooseFileConflictsMutex;
    std::unordered_map<std::shared_ptr<Mod>, std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>, Mod::ModHash>
        m_modLooseFileConflicts;

    std::mutex m_modConflictResolutionMutex;
    std::unordered_map<std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>, std::shared_ptr<Mod>, ModSetHash,
        ModSetEqual>
        m_modConflictResolution;

    ModManagerType m_mmType;
    BethesdaGame m_bg;

    static constexpr const char* MO2INI_PROFILESDIR_KEY = "profiles_directory=";
    static constexpr const char* MO2INI_MODDIR_KEY = "mod_directory=";
    static constexpr const char* MO2INI_BASEDIR_KEY = "base_directory=";
    static constexpr const char* MO2INI_BASEDIR_WILDCARD = "%BASE_DIR%";

public:
    ModManagerDirectory(const BethesdaGame& bg, const ModManagerType& mmType);

    [[nodiscard]] auto getModFileMap() const -> const std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>>&;
    [[nodiscard]] auto getModByFile(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>;
    [[nodiscard]] auto getMods() const -> std::vector<std::shared_ptr<Mod>>;
    [[nodiscard]] auto getMod(const std::wstring& modName) const -> std::shared_ptr<Mod>;

    void loadJSON(const nlohmann::json& json);
    auto getJSON() -> nlohmann::json;

    static auto getMO2ProfilesFromInstanceDir(const std::filesystem::path& instanceDir) -> std::vector<std::wstring>;

    void populateModFileMapMO2(
        const std::filesystem::path& instanceDir, const std::wstring& profile, const std::filesystem::path& outputDir);
    void populateModFileMapVortex(const std::filesystem::path& deploymentDir);

    auto doModsConflictInLooseFiles(const std::shared_ptr<Mod>& mod1, const std::shared_ptr<Mod>& mod2) -> bool;

    auto disqualifyModsFromSet(const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& modSet)
        -> std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>;

    void addConflictModSet(const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& modSet,
        const std::shared_ptr<Mod>& winningMod = nullptr);

    auto getConflicts() -> std::unordered_map<std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>,
        std::shared_ptr<Mod>, ModSetHash, ModSetEqual>;

    void setConflicts(const std::unordered_map<std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>,
        std::shared_ptr<Mod>, ModSetHash, ModSetEqual>& conflicts);

    [[nodiscard]] auto getWinningMod(const std::unordered_set<std::shared_ptr<Mod>, Mod::ModHash>& modSet)
        -> std::shared_ptr<Mod>;

    [[nodiscard]] auto getModManagerType() const -> ModManagerType { return m_mmType; }
    [[nodiscard]] auto getBethesdaGame() const -> BethesdaGame { return m_bg; }

    // Helpers
    [[nodiscard]] static auto getModManagerTypes() -> std::vector<ModManagerType>;
    [[nodiscard]] static auto getStrFromModManagerType(const ModManagerType& type) -> std::string;
    [[nodiscard]] static auto getModManagerTypeFromStr(const std::string& type) -> ModManagerType;

private:
    auto createOrGetNewModPtr(const std::wstring& modName) -> std::shared_ptr<Mod>;

    static auto getMO2FilePaths(const std::filesystem::path& instanceDir)
        -> std::pair<std::filesystem::path, std::filesystem::path>;

    static auto getVortexPath() -> std::filesystem::path;
};
