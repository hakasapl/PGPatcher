#include "ParallaxGenPlugin.hpp"

#include <mutex>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <utility>
#include <winbase.h>

#include "Logger.hpp"
#include "NIFUtil.hpp"
#include "PGDiag.hpp"
#include "PGMutagenNE.h"
#include "ParallaxGenUtil.hpp"
#include "ParallaxGenWarnings.hpp"
#include "patchers/base/PatcherUtil.hpp"

using namespace std;

namespace {
void dnneFailure(enum failure_type type, int errorCode)
{
    static constexpr unsigned int FAIL_CODE = 0x80008096;

    if (type == failure_type::failure_load_runtime) {
        if (errorCode == FAIL_CODE) { // FrameworkMissingFailure from
                                      // https://github.com/dotnet/runtime/blob/main/src/native/corehost/error_codes.h
            Logger::critical("The required .NET runtime is missing");
        } else {
            Logger::critical("Could not load .NET runtime, error code {:#X}", errorCode);
        }
    } else if (type == failure_type::failure_load_export) {
        Logger::critical("Could not load .NET exports, error code {:#X}", errorCode);
    }
}
} // namespace

mutex ParallaxGenPlugin::s_libMutex;

void ParallaxGenPlugin::libLogMessageIfExists()
{
    static constexpr unsigned int TRACE_LOG = 0;
    static constexpr unsigned int DEBUG_LOG = 1;
    static constexpr unsigned int INFO_LOG = 2;
    static constexpr unsigned int WARN_LOG = 3;
    static constexpr unsigned int ERROR_LOG = 4;
    static constexpr unsigned int CRITICAL_LOG = 5;

    int level = 0;
    wchar_t* message = nullptr;
    GetLogMessage(&message, &level);

    while (message != nullptr) {
        const wstring messageOut(message);
        LocalFree(static_cast<HGLOBAL>(message)); // Only free if memory was allocated.
        message = nullptr;

        // log the message
        switch (level) {
        case TRACE_LOG:
            Logger::trace(messageOut);
            break;
        case DEBUG_LOG:
            Logger::debug(messageOut);
            break;
        case INFO_LOG:
            Logger::info(messageOut);
            break;
        case WARN_LOG:
            Logger::warn(messageOut);
            break;
        case ERROR_LOG:
            Logger::error(messageOut);
            break;
        case CRITICAL_LOG:
            Logger::critical(messageOut);
            break;
        }

        // Get the next message
        GetLogMessage(&message, &level);
    }
}

void ParallaxGenPlugin::libThrowExceptionIfExists()
{
    wchar_t* message = nullptr;
    GetLastException(&message);

    if (message == nullptr) {
        return;
    }

    const wstring messageOut(message);
    LocalFree(static_cast<HGLOBAL>(message)); // Only free if memory was allocated.

    throw runtime_error("PGMutagen.dll: " + ParallaxGenUtil::utf16toASCII(messageOut));
}

void ParallaxGenPlugin::libInitialize(
    const int& gameType, const std::wstring& exePath, const wstring& dataPath, const vector<wstring>& loadOrder)
{
    const lock_guard<mutex> lock(s_libMutex);

    // Use vector to manage the memory for LoadOrderArr
    vector<const wchar_t*> loadOrderArr;
    if (!loadOrder.empty()) {
        loadOrderArr.reserve(loadOrder.size()); // Pre-allocate the vector size
        for (const auto& mod : loadOrder) {
            loadOrderArr.push_back(mod.c_str()); // Populate the vector with the c_str pointers
        }
    }

    // Add the null terminator to the end
    loadOrderArr.push_back(nullptr);

    Initialize(gameType, exePath.c_str(), dataPath.c_str(), loadOrderArr.data());
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void ParallaxGenPlugin::libPopulateObjs()
{
    const lock_guard<mutex> lock(s_libMutex);

    PopulateObjs();
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void ParallaxGenPlugin::libFinalize(const filesystem::path& outputPath, const bool& esmify)
{
    const lock_guard<mutex> lock(s_libMutex);

    Finalize(outputPath.c_str(), static_cast<int>(esmify));
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

auto ParallaxGenPlugin::libGetMatchingTXSTObjs(const wstring& nifName, const int& index3D)
    -> vector<tuple<int, int, wstring, string, wstring, unsigned int, wstring, unsigned int>>
{
    const lock_guard<mutex> lock(s_libMutex);

    int length = 0;
    GetMatchingTXSTObjs(
        nifName.c_str(), index3D, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &length);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    vector<int> txstIdArray(length);
    vector<int> altTexIdArray(length);
    vector<wchar_t*> matchedNIFArray(length);
    vector<char*> matchTypeArray(length);
    vector<wchar_t*> altTexModKeys(length);
    vector<unsigned int> altTexFormIDs(length);
    vector<wchar_t*> txstModKeys(length);
    vector<unsigned int> txstFormIDs(length);
    GetMatchingTXSTObjs(nifName.c_str(), index3D, txstIdArray.data(), altTexIdArray.data(), matchedNIFArray.data(),
        matchTypeArray.data(), altTexModKeys.data(), altTexFormIDs.data(), txstModKeys.data(), txstFormIDs.data(),
        nullptr);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    vector<tuple<int, int, wstring, string, wstring, unsigned int, wstring, unsigned int>> outputArray(length);
    for (int i = 0; i < length; ++i) {
        auto matchedNIFStr = wstring(static_cast<const wchar_t*>(matchedNIFArray.at(i)));
        boost::to_lower(matchedNIFStr);
        LocalFree(static_cast<HGLOBAL>(matchedNIFArray.at(i)));

        const auto* matchTypeStr = static_cast<const char*>(matchTypeArray.at(i));
        LocalFree(static_cast<HGLOBAL>(matchTypeArray.at(i)));

        auto altTexModKeyStr = wstring(static_cast<const wchar_t*>(altTexModKeys.at(i)));
        boost::to_lower(altTexModKeyStr);
        LocalFree(static_cast<HGLOBAL>(altTexModKeys.at(i)));

        auto txstModKeyStr = wstring(static_cast<const wchar_t*>(txstModKeys.at(i)));
        boost::to_lower(txstModKeyStr);
        LocalFree(static_cast<HGLOBAL>(txstModKeys.at(i)));

        outputArray[i] = { txstIdArray[i], altTexIdArray[i], matchedNIFStr, matchTypeStr, altTexModKeyStr,
            altTexFormIDs[i], txstModKeyStr, txstFormIDs[i] };
    }

    return outputArray;
}

auto ParallaxGenPlugin::libGetTXSTSlots(const int& txstIndex) -> array<wstring, NUM_TEXTURE_SLOTS>
{
    const lock_guard<mutex> lock(s_libMutex);

    array<wchar_t*, NUM_TEXTURE_SLOTS> slotsArray = { nullptr };
    auto outputArray = array<wstring, NUM_TEXTURE_SLOTS>();

    // Call the function
    GetTXSTSlots(txstIndex, slotsArray.data());
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    // Process the slots
    for (int i = 0; i < NUM_TEXTURE_SLOTS; ++i) {
        if (slotsArray.at(i) != nullptr) {
            // Convert to C-style string (Ansi)
            auto slotStr = wstring(static_cast<const wchar_t*>(slotsArray.at(i)));
            boost::to_lower(slotStr);
            outputArray.at(i) = slotStr;

            // Free the unmanaged memory allocated by Marshal.StringToHGlobalAnsi
            LocalFree(static_cast<HGLOBAL>(slotsArray.at(i)));
        }
    }

    return outputArray;
}

auto ParallaxGenPlugin::libCreateNewTXSTPatch(const int& altTexIndex, const array<wstring, NUM_TEXTURE_SLOTS>& slots,
    const string& newEDID, const unsigned int& newFormID) -> int
{
    const lock_guard<mutex> lock(s_libMutex);

    // Prepare the array of const wchar_t* pointers from the Slots array
    array<const wchar_t*, NUM_TEXTURE_SLOTS> slotsArray = { nullptr };
    for (int i = 0; cmp_less(i, NUM_TEXTURE_SLOTS); ++i) {
        slotsArray.at(i) = slots.at(i).c_str(); // Point to the internal wide string data of each wstring
    }

    // Call the CreateNewTXSTPatch function with AltTexIndex and the array of wide string pointers
    int newTXSTId = 0;
    CreateNewTXSTPatch(altTexIndex, slotsArray.data(), newEDID.c_str(), newFormID, &newTXSTId);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    return newTXSTId;
}

void ParallaxGenPlugin::libSetModelAltTex(const int& altTexIndex, const int& txstIndex)
{
    const lock_guard<mutex> lock(s_libMutex);

    SetModelAltTex(altTexIndex, txstIndex);
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void ParallaxGenPlugin::libSet3DIndex(const int& altTexIndex, const int& index3D)
{
    const lock_guard<mutex> lock(s_libMutex);

    Set3DIndex(altTexIndex, index3D);
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

auto ParallaxGenPlugin::libGetModelRecHandleFromAltTexHandle(const int& altTexIndex) -> int
{
    const lock_guard<mutex> lock(s_libMutex);

    int modelRecHandle = 0;
    GetModelRecHandleFromAltTexHandle(altTexIndex, &modelRecHandle);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    return modelRecHandle;
}

void ParallaxGenPlugin::libSetModelRecNIF(const int& modelRecHandle, const wstring& nifPath)
{
    const lock_guard<mutex> lock(s_libMutex);

    SetModelRecNIF(modelRecHandle, nifPath.c_str());
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

// Statics
unordered_map<string, unsigned int> ParallaxGenPlugin::s_txstFormIDs;
unordered_map<string, unsigned int> ParallaxGenPlugin::s_newTXSTFormIDs;
unordered_set<unsigned int> ParallaxGenPlugin::s_txstResrvedFormIDs;
unordered_set<unsigned int> ParallaxGenPlugin::s_txstUsedFormIDs;
unsigned int ParallaxGenPlugin::s_curTXSTFormID = 0;

mutex ParallaxGenPlugin::s_createdTXSTMutex;
unordered_map<array<wstring, NUM_TEXTURE_SLOTS>, pair<int, string>, ParallaxGenPlugin::ArrayHash,
    ParallaxGenPlugin::ArrayEqual>
    ParallaxGenPlugin::s_createdTXSTs;

ParallaxGenDirectory* ParallaxGenPlugin::s_pgd;

mutex ParallaxGenPlugin::s_processShapeMutex;

void ParallaxGenPlugin::loadStatics(ParallaxGenDirectory* pgd) { ParallaxGenPlugin::s_pgd = pgd; }

unordered_map<wstring, int>* ParallaxGenPlugin::s_modPriority;

void ParallaxGenPlugin::loadModPriorityMap(std::unordered_map<std::wstring, int>* modPriority)
{
    ParallaxGenPlugin::s_modPriority = modPriority;
}

void ParallaxGenPlugin::initialize(const BethesdaGame& game, const filesystem::path& exePath)
{
    set_failure_callback(dnneFailure);

    // Maps BethesdaGame::GameType to Mutagen game type
    static const unordered_map<BethesdaGame::GameType, int> mutagenGameTypeMap
        = { { BethesdaGame::GameType::SKYRIM, 1 }, { BethesdaGame::GameType::SKYRIM_SE, 2 },
              { BethesdaGame::GameType::SKYRIM_VR, 3 }, { BethesdaGame::GameType::ENDERAL, 5 },
              { BethesdaGame::GameType::ENDERAL_SE, 6 }, { BethesdaGame::GameType::SKYRIM_GOG, 7 } };

    libInitialize(
        mutagenGameTypeMap.at(game.getGameType()), exePath, game.getGameDataPath().wstring(), game.getActivePlugins());
}

void ParallaxGenPlugin::loadTXSTCache(const nlohmann::json& txstCache)
{
    s_newTXSTFormIDs.clear();

    // loop through json objects
    for (const auto& [key, value] : txstCache.items()) {
        // convert key to array
        s_txstFormIDs[key] = value.get<unsigned int>();
        s_txstResrvedFormIDs.insert(value.get<unsigned int>());
    }
}

auto ParallaxGenPlugin::getTXSTCache() -> nlohmann::json
{
    nlohmann::json txstCache;

    // loop through map
    for (const auto& [key, value] : s_newTXSTFormIDs) {
        txstCache[key] = value;
    }

    return txstCache;
}

void ParallaxGenPlugin::populateObjs() { libPopulateObjs(); }

auto ParallaxGenPlugin::getKeyFromFormID(const tuple<unsigned int, wstring, wstring>& formID) -> string
{
    return ParallaxGenUtil::utf16toUTF8(get<1>(formID)) + " (" + ParallaxGenUtil::utf16toUTF8(get<2>(formID)) + ") "
        + format("{:X}", get<0>(formID));
}

void ParallaxGenPlugin::processShape(const std::wstring& nifPath, const PatcherUtil::PatcherMeshObjectSet& patchers,
    const int& index3D, const bool& dryRun, const std::unordered_map<NIFUtil::ShapeShader, bool>* canApply,
    const std::string& shapeKey, std::vector<TXSTResult>* results)
{
    if ((results == nullptr || canApply == nullptr || shapeKey.empty()) && !dryRun) {
        throw runtime_error("Non dryrun parameters not present");
    }

    const lock_guard<mutex> lock(s_processShapeMutex);

    if (!dryRun) {
        results->clear();
    }

    // loop through matches
    const auto matches = libGetMatchingTXSTObjs(nifPath, index3D);
    for (const auto& [txstIndex, altTexIndex, matchedNIF, matchType, altTexModKey, altTexFormID, txstModKey,
             txstFormID] : matches) {
        const auto txstFormIDCacheKey = ParallaxGenUtil::utf16toUTF8(altTexModKey) + "/" + to_string(altTexFormID) + "/"
            + matchType + "/" + to_string(index3D);

        // this is somewhat costly so we only run it if diagnostics are enabled
        const auto altTexJSONKey
            = ParallaxGenUtil::utf16toUTF8(altTexModKey) + " / " + to_string(altTexFormID) + " / " + matchType;
        const auto txstJSONKey = ParallaxGenUtil::utf16toUTF8(txstModKey) + " / " + to_string(txstFormID);

        if (!dryRun) {
            const PGDiag::Prefix diagAltTexPrefix(altTexJSONKey, nlohmann::json::value_t::object);
            const PGDiag::Prefix diagShapeKeyPrefix(shapeKey, nlohmann::json::value_t::object);
            PGDiag::insert("origIndex3D", index3D);
        }

        // Output information
        TXSTResult curResult;
        curResult.txstModKey = txstModKey;
        curResult.txstFormID = txstFormID;
        curResult.altTexModKey = altTexModKey;
        curResult.altTexFormID = altTexFormID;

        // Get TXST slots
        PGDiag::insert("origTXST", txstJSONKey);

        auto oldSlots = libGetTXSTSlots(txstIndex);
        auto baseSlots = oldSlots;
        {
            const PGDiag::Prefix diagOrigTexPrefix("origTextures", nlohmann::json::value_t::array);
            for (auto& slot : baseSlots) {
                PGDiag::pushBack(slot);

                // Remove PBR from beginning if its there so that we can actually process it
                static constexpr const char* PBR_PREFIX = "textures\\pbr\\";

                if (boost::istarts_with(slot, PBR_PREFIX)) {
                    slot.replace(0, strlen(PBR_PREFIX), L"textures\\");
                }
            }
        }

        auto matches = PatcherUtil::getMatches(baseSlots, patchers, dryRun);
        if (dryRun) {
            return;
        }

        // remove any matches that cannot apply
        for (auto it = matches.begin(); it != matches.end();) {
            if (!canApply->contains(it->shader) && !canApply->contains(it->shaderTransformTo)) {
                it = matches.erase(it);
            } else {
                ++it;
            }
        }

        // Get winning match
        auto winningShaderMatch = PatcherUtil::getWinningMatch(matches);
        PGDiag::insert("winningShaderMatch", winningShaderMatch.getJSON());

        curResult.matchedNIF = matchedNIF;

        // Apply transforms
        if (PatcherUtil::applyTransformIfNeeded(winningShaderMatch, patchers)) {
            PGDiag::insert("shaderTransformResult", winningShaderMatch.getJSON());
        }

        if (winningShaderMatch.shader == NIFUtil::ShapeShader::UNKNOWN) {
            // if no match apply the default patcher
            winningShaderMatch.shader = NIFUtil::ShapeShader::NONE;
        }

        curResult.shader = winningShaderMatch.shader;

        // loop through patchers
        NIFUtil::TextureSet newSlots;
        patchers.shaderPatchers.at(winningShaderMatch.shader)
            ->applyPatchSlots(baseSlots, winningShaderMatch.match, newSlots);

        // Post warnings if any
        for (const auto& curMatchedFrom : winningShaderMatch.match.matchedFrom) {
            ParallaxGenWarnings::mismatchWarn(
                winningShaderMatch.match.matchedPath, newSlots.at(static_cast<int>(curMatchedFrom)));
        }

        ParallaxGenWarnings::meshWarn(winningShaderMatch.match.matchedPath, nifPath);

        // Check if oldprefix is the same as newprefix
        bool foundDiff = false;
        for (int i = 0; i < NUM_TEXTURE_SLOTS; ++i) {
            if (!boost::iequals(oldSlots.at(i), newSlots.at(i))) {
                foundDiff = true;
            }
        }

        curResult.altTexIndex = altTexIndex;
        curResult.modelRecHandle = libGetModelRecHandleFromAltTexHandle(altTexIndex);
        curResult.matchType = matchType;

        if (!foundDiff) {
            // No need to patch
            spdlog::trace(L"Plugin Patching | {} | {} | Not patching because nothing to change", nifPath, index3D);
            curResult.txstIndex = txstIndex;
            results->push_back(curResult);
            continue;
        }

        PGDiag::insert("newTextures", NIFUtil::textureSetToStr(newSlots));

        {
            const lock_guard<mutex> lock(s_createdTXSTMutex);

            // Check if we need to make a new TXST record
            if (s_createdTXSTs.contains(newSlots)) {
                // Already modded
                spdlog::trace(L"Plugin Patching | {} | {} | Already added, skipping", nifPath, index3D);
                curResult.txstIndex = s_createdTXSTs[newSlots].first;
                results->push_back(curResult);

                PGDiag::insert("newTXST", s_createdTXSTs[newSlots].second);

                continue;
            }

            // Create a new TXST record
            spdlog::trace(L"Plugin Patching | {} | {} | Creating a new TXST record and patching", nifPath, index3D);

            // Find formID to use
            unsigned int newFormID = 0;
            if (s_txstFormIDs.contains(txstFormIDCacheKey)
                && !s_txstUsedFormIDs.contains(s_txstFormIDs[txstFormIDCacheKey])) {
                // use old formid for new record
                newFormID = s_txstFormIDs[txstFormIDCacheKey];
            } else {
                // find next available formid
                while (s_txstResrvedFormIDs.contains(++s_curTXSTFormID)) { }
                newFormID = s_curTXSTFormID;
            }

            static constexpr unsigned int MAX_FORMID = 0xFFFFFF;
            if (newFormID > MAX_FORMID) {
                throw runtime_error("Form ID overflow");
            }

            if (newFormID == 0) {
                throw runtime_error("Failed to find a new form ID for TXST record");
            }

            if (s_txstUsedFormIDs.contains(newFormID)) {
                throw runtime_error("Form ID already in use");
            }

            // Insert used FORMID
            s_txstUsedFormIDs.insert(newFormID);

            // Find EDID
            const auto edidLabel = ParallaxGenUtil::utf16toUTF8(filesystem::path(baseSlots.at(0)).stem().wstring());
            const string newEDID = fmt::format("PG_{}_{:06X}", edidLabel, newFormID);

            // create new TXST record with chosen form ID
            curResult.txstIndex = libCreateNewTXSTPatch(altTexIndex, newSlots, newEDID, newFormID);
            s_newTXSTFormIDs[txstFormIDCacheKey] = newFormID;

            patchers.shaderPatchers.at(winningShaderMatch.shader)
                ->processNewTXSTRecord(winningShaderMatch.match, newEDID);
            s_createdTXSTs[newSlots] = { curResult.txstIndex, newEDID };

            PGDiag::insert("newTXST", newEDID);
        }

        // add to result
        results->push_back(curResult);
    }
}

void ParallaxGenPlugin::assignMesh(const wstring& nifPath, const wstring& baseNIFPath, const vector<TXSTResult>& result)
{
    const lock_guard<mutex> lock(s_processShapeMutex);

    const Logger::Prefix prefix(L"assignMesh");

    // Loop through results
    for (const auto& curResult : result) {
        const auto altTexJSONKey = ParallaxGenUtil::utf16toUTF8(curResult.altTexModKey) + " / "
            + to_string(curResult.altTexFormID) + " / " + curResult.matchType;

        const PGDiag::Prefix diagAltTexPrefix(altTexJSONKey, nlohmann::json::value_t::object);
        PGDiag::insert("origModel", baseNIFPath);

        if (!boost::iequals(curResult.matchedNIF, baseNIFPath)) {
            // Skip if not the base NIF
            continue;
        }

        if (!boost::iequals(curResult.matchedNIF, nifPath)) {
            // Set model rec handle
            libSetModelRecNIF(curResult.altTexIndex, nifPath);

            PGDiag::insert("newModel", nifPath);
        }

        // Set model alt tex
        libSetModelAltTex(curResult.altTexIndex, curResult.txstIndex);
    }
}

void ParallaxGenPlugin::set3DIndices(
    const wstring& nifPath, const int& oldIndex3D, const int& newIndex3D, const std::string& shapeKey)
{
    const lock_guard<mutex> lock(s_processShapeMutex);

    const Logger::Prefix prefix(L"set3DIndices");

    // find matches
    const auto matches = libGetMatchingTXSTObjs(nifPath, oldIndex3D);

    // Set indices
    for (const auto& [txstIndex, altTexIndex, matchedNIF, matchType, altTexModKey, altTexFormID, txstModKey,
             txstFormID] : matches) {
        if (!boost::iequals(nifPath, matchedNIF)) {
            // Skip if not the base NIF
            continue;
        }

        if (oldIndex3D == newIndex3D) {
            // No change
            continue;
        }

        const auto altTexJSONKey
            = ParallaxGenUtil::utf16toUTF8(altTexModKey) + " / " + to_string(altTexFormID) + " / " + matchType;

        const PGDiag::Prefix diagAltTexPrefix(altTexJSONKey, nlohmann::json::value_t::object);
        const PGDiag::Prefix diagShapeKeyPrefix(shapeKey, nlohmann::json::value_t::object);

        PGDiag::insert("newIndex3D", newIndex3D);

        Logger::trace(L"Setting 3D index for AltTex {} to {}", altTexIndex, newIndex3D);
        libSet3DIndex(altTexIndex, newIndex3D);
    }
}

void ParallaxGenPlugin::savePlugin(const filesystem::path& outputDir, bool esmify)
{
    libFinalize(outputDir, esmify);
    // TODO add to generated files
}
