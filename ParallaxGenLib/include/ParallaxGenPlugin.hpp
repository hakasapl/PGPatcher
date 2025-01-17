#pragma once

#include <Geometry.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <windows.h>

#include <boost/functional/hash.hpp>

#include "BethesdaGame.hpp"
#include "NIFUtil.hpp"
#include "ParallaxGenDirectory.hpp"
#include "patchers/PatcherUtil.hpp"

class ParallaxGenPlugin {
private:
    static constexpr int LOG_POLL_INTERVAL = 1000;

    static std::mutex s_libMutex;
    static void libLogMessageIfExists();
    static void libThrowExceptionIfExists();
    static void libInitialize(const int& gameType, const std::wstring& dataPath, const std::wstring& outputPlugin,
        const std::vector<std::wstring>& loadOrder = {});
    static void libPopulateObjs();
    static void libFinalize(const std::filesystem::path& outputPath, const bool& esmify);

    /// @brief get the TXST objects that are used by a shape
    /// @param[in] nifName filename of the nif
    /// @param[in] name3D "3D name" - name of the shape
    /// @param[in] index3D "3D index" - index of the shape in the nif
    /// @return TXST objects, pairs of (texture set index,alternate textures ids)
    static auto libGetMatchingTXSTObjs(const std::wstring& nifName, const std::wstring& name3D, const int& index3D)
        -> std::vector<std::tuple<int, int>>;

    /// @brief get the assigned textures of all slots in a texture set
    /// @param[in] txstIndex index of the texture set
    /// @return array of texture files for all texture slots
    static auto libGetTXSTSlots(const int& txstIndex) -> std::array<std::wstring, NUM_TEXTURE_SLOTS>;

    static void libCreateTXSTPatch(const int& txstIndex, const std::array<std::wstring, NUM_TEXTURE_SLOTS>& slots);

    static auto libCreateNewTXSTPatch(const int& altTexIndex, const std::array<std::wstring, NUM_TEXTURE_SLOTS>& slots,
        const std::string& newEDID) -> int;

    /// @brief assign texture set to an "alternate texture", i.e. the "new texture" entry
    /// @param[in] altTexIndex global index of the alternate texture
    /// @param[in] txstIndex global index of the texture set
    static void libSetModelAltTex(const int& altTexIndex, const int& txstIndex);

    /// @brief set the "3D index" of an alternate texture, i.e. the shape index
    /// @param[in] altTexIndex global index of the alternate texture
    /// @param[in] index3D index to set
    static void libSet3DIndex(const int& altTexIndex, const int& index3D);

    /// @brief get the form id and mod name of the given texture set
    /// @param[in] txstIndex  the texture set
    /// @return pair of form id and mod name
    static auto libGetTXSTFormID(const int& txstIndex) -> std::tuple<unsigned int, std::wstring>;

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
    static std::unordered_map<std::array<std::wstring, NUM_TEXTURE_SLOTS>, int, ArrayHash, ArrayEqual> s_createdTXSTs;
    static std::mutex s_createdTXSTMutex;

    static int s_edidCounter;
    static std::mutex s_edidCounterMutex;

    // Runner vars
    static std::unordered_map<std::wstring, int>* s_modPriority;

public:
    static void loadStatics(ParallaxGenDirectory* pgd);
    static void loadModPriorityMap(std::unordered_map<std::wstring, int>* modPriority);

    static void initialize(const BethesdaGame& game);

    static void populateObjs();

    struct TXSTResult {
        int modelRecHandle {};
        int altTexIndex {};
        int txstIndex {};
        NIFUtil::ShapeShader shader {};
    };

    static void processShape(const std::wstring& nifPath, nifly::NiShape* nifShape, const std::wstring& name3D,
        const int& index3D, PatcherUtil::PatcherObjectSet& patchers, std::vector<TXSTResult>& results,
        PatcherUtil::ConflictModResults* conflictMods = nullptr);

    static void assignMesh(const std::wstring& nifPath, const std::vector<TXSTResult>& result);

    static void set3DIndices(
        const std::wstring& nifPath, const std::vector<std::tuple<nifly::NiShape*, int, int>>& shapeTracker);

    static void savePlugin(const std::filesystem::path& outputDir, bool esmify);
};
