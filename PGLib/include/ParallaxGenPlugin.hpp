#pragma once

#include <Geometry.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>
#include <windows.h>

#include "BethesdaGame.hpp"
#include "util/MeshTracker.hpp"
#include "util/NIFUtil.hpp"

class ParallaxGenPlugin {
public:
    enum class PluginLang : uint8_t {
        ENGLISH,
        GERMAN,
        ITALIAN,
        SPANISH,
        SPANISH_MEXICO,
        FRENCH,
        POLISH,
        PORTUGUESE_BRAZIL,
        CHINESE,
        RUSSIAN,
        JAPANESE,
        CZECH,
        HUNGARIAN,
        DANISH,
        FINNISH,
        GREEK,
        NORWEGIAN,
        SWEDISH,
        TURKISH,
        ARABIC,
        KOREAN,
        THAI,
        CHINESE_SIMPLIFIED
    };

    struct MeshUseAttributes {
        bool singlepassMATO;
        std::unordered_map<unsigned int, NIFUtil::TextureSet> alternateTextures;
    };

    static auto getPluginLangFromString(const std::string& lang) -> PluginLang;
    static auto getStringFromPluginLang(const PluginLang& lang) -> std::string;
    static auto getAvailablePluginLangStrs() -> std::vector<std::string>;

    static void initialize(
        const BethesdaGame& game, const std::filesystem::path& exePath, const PluginLang& lang = PluginLang::ENGLISH);

    static void populateObjs(const std::filesystem::path& existingModPath = {});

    static auto getModelUses(const std::wstring& modelPath)
        -> std::vector<std::pair<MeshTracker::FormKey, MeshUseAttributes>>;

    static void setModelUses(const std::vector<MeshTracker::MeshResult>& meshResults);

    static void savePlugin(const std::filesystem::path& outputDir, bool esmify);
};
