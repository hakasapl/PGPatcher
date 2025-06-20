#pragma once

#include <Geometry.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <filesystem>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include <boost/functional/hash.hpp>

#include "BethesdaGame.hpp"
#include "ParallaxGenDirectory.hpp"
#include "patchers/base/PatcherUtil.hpp"
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

private:
    static constexpr int LOG_POLL_INTERVAL = 1000;

    static std::mutex s_libMutex;
    static void libLogMessageIfExists();
    static void libThrowExceptionIfExists();
    static void libInitialize(const int& gameType, const std::wstring& exePath, const std::wstring& dataPath,
        const std::vector<std::wstring>& loadOrder = {}, const PluginLang& lang = PluginLang::ENGLISH);
    static void libPopulateObjs();
    static void libFinalize(const std::filesystem::path& outputPath, const bool& esmify);

    /// @brief get the TXST objects that are used by a shape
    /// @param[in] nifName filename of the nif
    /// @param[in] name3D "3D name" - name of the shape
    /// @param[in] index3D "3D index" - index of the shape in the nif
    /// @return TXST objects, pairs of (texture set index,alternate textures ids)
    static auto libGetMatchingTXSTObjs(const std::wstring& nifName, const int& index3D) -> std::vector<
        std::tuple<int, int, std::wstring, std::string, std::wstring, unsigned int, std::wstring, unsigned int>>;

    /// @brief get the assigned textures of all slots in a texture set
    /// @param[in] txstIndex index of the texture set
    /// @return array of texture files for all texture slots
    static auto libGetTXSTSlots(const int& txstIndex) -> std::array<std::wstring, NUM_TEXTURE_SLOTS>;

    static auto libCreateNewTXSTPatch(const int& altTexIndex, const std::array<std::wstring, NUM_TEXTURE_SLOTS>& slots,
        const std::string& newEDID, const unsigned int& newFormID) -> int;

    /// @brief assign texture set to an "alternate texture", i.e. the "new texture" entry
    /// @param[in] altTexIndex global index of the alternate texture
    /// @param[in] txstIndex global index of the texture set
    static void libSetModelAltTex(const int& altTexIndex, const int& txstIndex);

    /// @brief set the "3D index" of an alternate texture, i.e. the shape index
    /// @param[in] altTexIndex global index of the alternate texture
    /// @param[in] index3D index to set
    static void libSet3DIndex(const int& altTexIndex, const int& index3D);

    /// @brief get the model record handle from the alternate texture handle
    /// @param[in] altTexIndex global index of the alternate texture
    /// @return model record handle
    static auto libGetModelRecHandleFromAltTexHandle(const int& altTexIndex) -> int;

    static void libSetModelRecNIF(const int& modelRecHandle, const std::wstring& nifPath);

    static bool s_loggingEnabled;
    static void logMessages();

    static ParallaxGenDirectory* s_pgd;

    static std::mutex s_processShapeMutex;

    // Custom hash function for std::array<std::string, 9>
    struct ArrayHash {
        auto operator()(const std::array<std::wstring, NUM_TEXTURE_SLOTS>& arr) const -> std::size_t
        {
            std::size_t hash = 0;
            for (const auto& str : arr) {
                // Convert string to lowercase and hash
                const std::wstring lowerStr = boost::to_lower_copy(str);
                boost::hash_combine(hash, boost::hash<std::wstring>()(lowerStr));
            }
            return hash;
        }
    };

    struct ArrayEqual {
        auto operator()(const std::array<std::wstring, NUM_TEXTURE_SLOTS>& lhs,
            const std::array<std::wstring, NUM_TEXTURE_SLOTS>& rhs) const -> bool
        {
            for (size_t i = 0; i < NUM_TEXTURE_SLOTS; ++i) {
                if (!boost::iequals(lhs.at(i), rhs.at(i))) {
                    return false;
                }
            }
            return true;
        }
    };

    static std::unordered_map<std::string, unsigned int> s_txstFormIDs;
    static std::unordered_map<std::string, unsigned int> s_newTXSTFormIDs;
    static std::unordered_set<unsigned int> s_txstResrvedFormIDs;
    static std::unordered_set<unsigned int> s_txstUsedFormIDs;
    static unsigned int s_curTXSTFormID;
    static std::unordered_map<std::array<std::wstring, NUM_TEXTURE_SLOTS>, std::pair<int, std::string>, ArrayHash,
        ArrayEqual>
        s_createdTXSTs;
    static std::shared_mutex s_createdTXSTMutex;

    // Runner vars
    static std::unordered_map<std::wstring, int>* s_modPriority;

public:
    static auto getPluginLangFromString(const std::string& lang) -> PluginLang;
    static auto getStringFromPluginLang(const PluginLang& lang) -> std::string;
    static auto getAvailablePluginLangStrs() -> std::vector<std::string>;

    static void loadStatics(ParallaxGenDirectory* pgd);
    static void loadModPriorityMap(std::unordered_map<std::wstring, int>* modPriority);

    static void initialize(
        const BethesdaGame& game, const std::filesystem::path& exePath, const PluginLang& lang = PluginLang::ENGLISH);

    static void populateObjs();

    static void loadTXSTCache(const nlohmann::json& txstCache);
    static auto getTXSTCache() -> nlohmann::json;

    struct TXSTResult {
        std::wstring matchedNIF;
        int modelRecHandle {};
        int altTexIndex {};
        unsigned int altTexFormID {};
        std::wstring altTexModKey;
        int txstIndex {};
        unsigned int txstFormID {};
        std::wstring txstModKey;
        std::string matchType;
        NIFUtil::ShapeShader shader {};
    };

    static void processShape(const std::wstring& nifPath, const PatcherUtil::PatcherMeshObjectSet& patchers,
        const int& index3D, const bool& dryRun,
        const std::unordered_map<NIFUtil::ShapeShader, bool>* canApply = nullptr, const std::string& shapeKey = "",
        std::unordered_map<int, std::unordered_map<int, ParallaxGenPlugin::TXSTResult>>* results = nullptr);

    static void assignMesh(
        const std::wstring& nifPath, const std::wstring& baseNIFPath, const std::vector<TXSTResult>& result);

    static void set3DIndices(
        const std::wstring& nifPath, const int& oldIndex3D, const int& newIndex3D, const std::string& shapeKey);

    static void savePlugin(const std::filesystem::path& outputDir, bool esmify);

private:
    static auto getKeyFromFormID(const std::tuple<unsigned int, std::wstring, std::wstring>& formID) -> std::string;
};
