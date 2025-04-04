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
    -> vector<tuple<int, int, wstring, string>>
{
    const lock_guard<mutex> lock(s_libMutex);

    int length = 0;
    GetMatchingTXSTObjs(nifName.c_str(), index3D, nullptr, nullptr, nullptr, nullptr, &length);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    vector<int> txstIdArray(length);
    vector<int> altTexIdArray(length);
    vector<wchar_t*> matchedNIFArray(length);
    vector<char*> matchTypeArray(length);
    GetMatchingTXSTObjs(nifName.c_str(), index3D, txstIdArray.data(), altTexIdArray.data(), matchedNIFArray.data(),
        matchTypeArray.data(), nullptr);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    vector<tuple<int, int, wstring, string>> outputArray(length);
    for (int i = 0; i < length; ++i) {
        const auto* matchedNIFStr = static_cast<const wchar_t*>(matchedNIFArray.at(i));
        LocalFree(static_cast<HGLOBAL>(matchedNIFArray.at(i)));

        const auto* matchTypeStr = static_cast<const char*>(matchTypeArray.at(i));
        LocalFree(static_cast<HGLOBAL>(matchTypeArray.at(i)));

        outputArray[i] = { txstIdArray[i], altTexIdArray[i], matchedNIFStr, matchTypeStr };
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
            const auto* slotStr = static_cast<const wchar_t*>(slotsArray.at(i));
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

auto ParallaxGenPlugin::libGetTXSTFormID(const int& txstIndex) -> tuple<unsigned int, wstring, wstring>
{
    const lock_guard<mutex> lock(s_libMutex);

    wchar_t* pluginName = nullptr;
    wchar_t* winningPluginName = nullptr;
    unsigned int formID = 0;
    GetTXSTFormID(txstIndex, &formID, &pluginName, &winningPluginName);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    wstring pluginNameString;
    if (pluginName != nullptr) {
        pluginNameString = wstring(pluginName);
        LocalFree(static_cast<HGLOBAL>(pluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where PluginName is null
        pluginNameString = L"Unknown";
    }

    wstring winningPluginNameString;
    if (winningPluginName != nullptr) {
        winningPluginNameString = wstring(winningPluginName);
        LocalFree(static_cast<HGLOBAL>(winningPluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where WinningPluginName is null
        winningPluginNameString = L"Unknown";
    }

    return make_tuple(formID, pluginNameString, winningPluginNameString);
}

auto ParallaxGenPlugin::libGetModelRecFormID(const int& modelRecHandle) -> tuple<unsigned int, wstring, wstring>
{
    const lock_guard<mutex> lock(s_libMutex);

    wchar_t* pluginName = nullptr;
    wchar_t* winningPluginName = nullptr;
    unsigned int formID = 0;
    GetModelRecFormID(modelRecHandle, &formID, &pluginName, &winningPluginName);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    wstring pluginNameString;
    if (pluginName != nullptr) {
        pluginNameString = wstring(pluginName);
        LocalFree(static_cast<HGLOBAL>(pluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where PluginName is null
        pluginNameString = L"Unknown";
    }

    wstring winningPluginNameString;
    if (winningPluginName != nullptr) {
        winningPluginNameString = wstring(winningPluginName);
        LocalFree(static_cast<HGLOBAL>(winningPluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where WinningPluginName is null
        winningPluginNameString = L"Unknown";
    }

    return make_tuple(formID, pluginNameString, winningPluginNameString);
}

auto ParallaxGenPlugin::libGetAltTexFormID(const int& altTexIndex) -> tuple<unsigned int, wstring, wstring>
{
    const lock_guard<mutex> lock(s_libMutex);

    wchar_t* pluginName = nullptr;
    wchar_t* winningPluginName = nullptr;
    unsigned int formID = 0;
    GetAltTexFormID(altTexIndex, &formID, &pluginName, &winningPluginName);
    libLogMessageIfExists();
    libThrowExceptionIfExists();

    wstring pluginNameString;
    if (pluginName != nullptr) {
        pluginNameString = wstring(pluginName);
        LocalFree(static_cast<HGLOBAL>(pluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where PluginName is null
        pluginNameString = L"Unknown";
    }

    wstring winningPluginNameString;
    if (winningPluginName != nullptr) {
        winningPluginNameString = wstring(winningPluginName);
        LocalFree(static_cast<HGLOBAL>(winningPluginName)); // Only free if memory was allocated.
    } else {
        // Handle the case where WinningPluginName is null
        winningPluginNameString = L"Unknown";
    }

    return make_tuple(formID, pluginNameString, winningPluginNameString);
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

void ParallaxGenPlugin::processShape(const wstring& nifPath, nifly::NiShape* nifShape, const int& index3D,
    PatcherUtil::PatcherMeshObjectSet& patchers, vector<TXSTResult>& results, const string& shapeKey,
    PatcherUtil::ConflictModResults* conflictMods)
{
    const lock_guard<mutex> lock(s_processShapeMutex);

    results.clear();

    // loop through matches
    const auto matches = libGetMatchingTXSTObjs(nifPath, index3D);
    for (const auto& [txstIndex, altTexIndex, matchedNIF, matchType] : matches) {
        // create keys for diagnostics
        string altTexJSONKey;
        string txstJSONKey;

        const auto altTexFormIDTuple = libGetAltTexFormID(altTexIndex);
        const auto txstFormIDCacheKey = ParallaxGenUtil::utf16toUTF8(get<1>(altTexFormIDTuple)) + "/"
            + to_string(get<0>(altTexFormIDTuple)) + "/" + matchType + "/" + to_string(index3D);

        if (PGDiag::isEnabled()) {
            // this is somewhat costly so we only run it if diagnostics are enabled
            altTexJSONKey = getKeyFromFormID(altTexFormIDTuple) + " / " + matchType;
            txstJSONKey = getKeyFromFormID(libGetTXSTFormID(txstIndex));
        }

        const PGDiag::Prefix diagAltTexPrefix(altTexJSONKey, nlohmann::json::value_t::object);
        const PGDiag::Prefix diagShapeKeyPrefix(shapeKey, nlohmann::json::value_t::object);
        PGDiag::insert("origIndex3D", index3D);

        //  Allowed shaders from result of patchers
        vector<PatcherUtil::ShaderPatcherMatch> matches;

        // Output information
        TXSTResult curResult;

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

        // Loop through each shader
        unordered_set<wstring> modSet;
        for (const auto& [shader, patcher] : patchers.shaderPatchers) {
            if (shader == NIFUtil::ShapeShader::NONE) {
                // TEMPORARILY disable default patcher
                continue;
            }

            // note: name is defined in source code in UTF8-encoded files
            const Logger::Prefix prefixPatches(patcher->getPatcherName());

            // Check if shader should be applied
            vector<PatcherMeshShader::PatcherMatch> curMatches;
            if (!patcher->shouldApply(baseSlots, curMatches)) {
                Logger::trace(L"Rejecting: Shader not applicable");
                continue;
            }

            for (const auto& match : curMatches) {
                PatcherUtil::ShaderPatcherMatch curMatch;
                curMatch.mod = s_pgd->getMod(match.matchedPath);
                curMatch.shader = shader;
                curMatch.match = match;
                curMatch.shaderTransformTo = NIFUtil::ShapeShader::UNKNOWN;

                // See if transform is possible
                if (patchers.shaderTransformPatchers.contains(shader)) {
                    const auto& availableTransforms = patchers.shaderTransformPatchers.at(shader);
                    // loop from highest element of map to 0
                    for (const auto& availableTransform : ranges::reverse_view(availableTransforms)) {
                        if (patchers.shaderPatchers.at(availableTransform.first)->canApply(*nifShape)) {
                            // Found a transform that can apply, set the transform in the match
                            curMatch.shaderTransformTo = availableTransform.first;
                            break;
                        }
                    }
                }

                // Add to matches if shader can apply (or if transform shader exists and can apply)
                if (patcher->canApply(*nifShape) || curMatch.shaderTransformTo != NIFUtil::ShapeShader::UNKNOWN) {
                    matches.push_back(curMatch);
                    modSet.insert(curMatch.mod);
                }
            }
        }

        // Populate conflict mods if set
        if (conflictMods != nullptr) {
            if (modSet.size() > 1) {
                const lock_guard<mutex> lock(conflictMods->mutex);

                // add mods to conflict set
                for (const auto& match : matches) {
                    if (conflictMods->mods.find(match.mod) == conflictMods->mods.end()) {
                        conflictMods->mods.insert(
                            { match.mod, { set<NIFUtil::ShapeShader>(), unordered_set<wstring>() } });
                    }

                    get<0>(conflictMods->mods[match.mod]).insert(match.shader);
                    get<1>(conflictMods->mods[match.mod]).insert(modSet.begin(), modSet.end());
                }
            }

            continue;
        }

        // Populate diag JSON if set with each match
        {
            const PGDiag::Prefix diagShaderPatcherPrefix("shaderPatcherMatches", nlohmann::json::value_t::array);
            for (const auto& match : matches) {
                PGDiag::pushBack(match.getJSON());
            }
        }

        // Get winning match
        auto winningShaderMatch = PatcherUtil::getWinningMatch(matches, s_modPriority);
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
            results.push_back(curResult);
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
                results.push_back(curResult);

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
            const string newEDID = fmt::format("PGTXST{:06X}", newFormID);

            // create new TXST record with chosen form ID
            curResult.txstIndex = libCreateNewTXSTPatch(altTexIndex, newSlots, newEDID, newFormID);
            s_newTXSTFormIDs[txstFormIDCacheKey] = newFormID;

            patchers.shaderPatchers.at(winningShaderMatch.shader)
                ->processNewTXSTRecord(winningShaderMatch.match, newEDID);
            s_createdTXSTs[newSlots] = { curResult.txstIndex, newEDID };

            PGDiag::insert("newTXST", newEDID);
        }

        // add to result
        results.push_back(curResult);
    }
}

void ParallaxGenPlugin::assignMesh(const wstring& nifPath, const wstring& baseNIFPath, const vector<TXSTResult>& result)
{
    const lock_guard<mutex> lock(s_processShapeMutex);

    const Logger::Prefix prefix(L"assignMesh");

    // Loop through results
    for (const auto& curResult : result) {
        string altTexJSONKey;

        if (PGDiag::isEnabled()) {
            // this is somewhat costly so we only run it if diagnostics are enabled
            altTexJSONKey = getKeyFromFormID(libGetAltTexFormID(curResult.altTexIndex)) + " / " + curResult.matchType;
        }

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
    for (const auto& [txstIndex, altTexIndex, matchedNIF, matchType] : matches) {
        if (!boost::iequals(nifPath, matchedNIF)) {
            // Skip if not the base NIF
            continue;
        }

        if (oldIndex3D == newIndex3D) {
            // No change
            continue;
        }

        string altTexJSONKey;
        if (PGDiag::isEnabled()) {
            // this is somewhat costly so we only run it if diagnostics are enabled
            altTexJSONKey = getKeyFromFormID(libGetAltTexFormID(altTexIndex)) + " / " + matchType;
        }

        const PGDiag::Prefix diagAltTexPrefix(altTexJSONKey, nlohmann::json::value_t::object);
        const PGDiag::Prefix diagShapeKeyPrefix(shapeKey, nlohmann::json::value_t::object);

        PGDiag::insert("newIndex3D", newIndex3D);

        Logger::trace(L"Setting 3D index for AltTex {} to {}", altTexIndex, newIndex3D);
        libSet3DIndex(altTexIndex, newIndex3D);
    }
}

void ParallaxGenPlugin::savePlugin(const filesystem::path& outputDir, bool esmify) { libFinalize(outputDir, esmify); }
