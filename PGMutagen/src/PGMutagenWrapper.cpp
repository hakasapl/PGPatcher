#include "PGMutagenWrapper.hpp"

#include "PGMutagenBuffers_generated.h"
#include "PGMutagenNE.h"
#include "dnne.h"

#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/string.h>
#include <flatbuffers/verifier.h>
#include <spdlog/spdlog.h>

#include <array>
#include <combaseapi.h>
#include <cstdint>
#include <filesystem>
#include <minwindef.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <stringapiset.h>
#include <utility>
#include <vector>
#include <winbase.h>
#include <winnls.h>

using namespace std;

namespace {
void dnneFailure([[maybe_unused]] enum failure_type type, [[maybe_unused]] int errorCode)
{
    spdlog::critical("DotNet Wrapper failed to load, verify .NET runtime is installed properly");
}
} // namespace

mutex PGMutagenWrapper::s_libMutex;

void PGMutagenWrapper::libLogMessageIfExists()
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
            spdlog::trace(L"{}", messageOut);
            break;
        case DEBUG_LOG:
            spdlog::debug(L"{}", messageOut);
            break;
        case INFO_LOG:
            spdlog::info(L"{}", messageOut);
            break;
        case WARN_LOG:
            spdlog::warn(L"{}", messageOut);
            break;
        case ERROR_LOG:
            spdlog::error(L"{}", messageOut);
            break;
        case CRITICAL_LOG:
            spdlog::critical(L"{}", messageOut);
            break;
        }

        // Get the next message
        GetLogMessage(&message, &level);
    }
}

void PGMutagenWrapper::libThrowExceptionIfExists()
{
    wchar_t* message = nullptr;
    GetLastException(&message);

    if (message == nullptr) {
        return;
    }

    const wstring messageOut(message);
    LocalFree(static_cast<HGLOBAL>(message)); // Only free if memory was allocated.

    throw runtime_error("PGMutagenWrapper: " + utf16toUTF8(messageOut));
}

void PGMutagenWrapper::libInitialize(const int& gameType, const std::wstring& exePath, const wstring& dataPath,
    const vector<wstring>& loadOrder, const unsigned int& lang)
{
    set_failure_callback(dnneFailure);

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

    {
        const lock_guard<mutex> lock(s_libMutex);

        Initialize(gameType, exePath.c_str(), dataPath.c_str(), loadOrderArr.data(), lang);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }
}

void PGMutagenWrapper::libPopulateObjs(const filesystem::path& existingModPath)
{
    const lock_guard<mutex> lock(s_libMutex);

    PopulateObjs(existingModPath.wstring().c_str());
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void PGMutagenWrapper::libFinalize(const filesystem::path& outputPath, const bool& esmify)
{
    const lock_guard<mutex> lock(s_libMutex);

    Finalize(outputPath.c_str(), static_cast<int>(esmify));
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

auto PGMutagenWrapper::libGetModelUses(const std::wstring& modelPath) -> std::vector<ModelUse>
{
    uint8_t* buffer = nullptr;
    uint32_t length = 0;

    {
        const lock_guard<mutex> lock(s_libMutex);
        GetModelUses(modelPath.c_str(), &length, &buffer);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }

    if ((buffer == nullptr) || length == 0) {
        return {};
    }

    flatbuffers::Verifier verifier(buffer, length);
    if (!PGMutagenBuffers::VerifyModelUsesBuffer(verifier)) {
        return {};
    }

    vector<ModelUse> modelUsesOut;

    const auto* const modelUses = PGMutagenBuffers::GetModelUses(buffer);
    for (const auto* const mu : *modelUses->uses()) {
        auto curUse = ModelUse();

        curUse.modName = wstring(mu->mod_name()->begin(), mu->mod_name()->end());
        curUse.formID = mu->form_id();
        curUse.subModel = string(mu->sub_model()->begin(), mu->sub_model()->end());
        curUse.isWeighted = mu->is_weighted();
        curUse.singlepassMATO = mu->singlepass_mato();
        curUse.isIgnored = mu->is_ignored();
        curUse.type = string(mu->type()->begin(), mu->type()->end());

        for (const auto* const altTex : *mu->alternate_textures()) {
            auto curAltTex = AlternateTexture();
            curAltTex.slotID = altTex->slot_id();

            // no slots
            if (altTex->slots() == nullptr || altTex->slots()->textures() == nullptr) {
                continue;
            }

            auto slots = array<wstring, NUM_PLUGIN_TEXTURE_SLOTS> {};
            const auto* textures = altTex->slots()->textures();
            for (int i = 0; std::cmp_less(i, NUM_PLUGIN_TEXTURE_SLOTS) && std::cmp_less(i, textures->size()); ++i) {
                const auto* texStr = textures->Get(i);
                if (texStr != nullptr) {
                    slots.at(i) = wstring(texStr->begin(), texStr->end());
                }
            }

            curAltTex.slots = slots;

            curUse.alternateTextures.push_back(curAltTex);
        }

        modelUsesOut.push_back(curUse);
    }

    ::CoTaskMemFree(buffer);

    return modelUsesOut;
}

void PGMutagenWrapper::libSetModelUses(const std::vector<ModelUse>& modelUses)
{
    flatbuffers::FlatBufferBuilder builder(DEFAULT_BUFFER_SIZE);

    vector<flatbuffers::Offset<PGMutagenBuffers::ModelUse>> modelUsesOffsets;
    modelUsesOffsets.reserve(modelUses.size());

    for (const auto& mu : modelUses) {
        const auto modNameOffset = builder.CreateString(utf16toUTF8(mu.modName));
        const auto subModelOffset = builder.CreateString(mu.subModel);
        const auto meshFileOffset = builder.CreateString(utf16toUTF8(mu.meshFile));

        vector<flatbuffers::Offset<PGMutagenBuffers::AlternateTexture>> altTexOffsets;
        altTexOffsets.reserve(mu.alternateTextures.size());

        for (const auto& altTex : mu.alternateTextures) {
            vector<flatbuffers::Offset<flatbuffers::String>> texOffsets;
            texOffsets.reserve(NUM_PLUGIN_TEXTURE_SLOTS);

            for (const auto& tex : altTex.slots) {
                texOffsets.push_back(builder.CreateString(utf16toUTF8(tex)));
            }

            const auto slotsOffset = PGMutagenBuffers::CreateTextureSet(builder, builder.CreateVector(texOffsets));

            const auto altTexOffset
                = PGMutagenBuffers::CreateAlternateTexture(builder, altTex.slotID, altTex.slotIDNew, slotsOffset);
            altTexOffsets.push_back(altTexOffset);
        }

        const auto altTexVectorOffset = builder.CreateVector(altTexOffsets);

        const auto modelUseOffset = PGMutagenBuffers::CreateModelUse(builder, modNameOffset, mu.formID, subModelOffset,
            false, meshFileOffset, false, false, {}, altTexVectorOffset);
        modelUsesOffsets.push_back(modelUseOffset);
    }

    const auto modelUsesVectorOffset = builder.CreateVector(modelUsesOffsets);
    const auto modelUsesRoot = PGMutagenBuffers::CreateModelUses(builder, modelUsesVectorOffset);
    builder.Finish(modelUsesRoot);

    uint8_t const* buf = builder.GetBufferPointer();
    unsigned int const size = builder.GetSize();

    {
        const lock_guard<mutex> lock(s_libMutex);
        SetModelUses(size, buf);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }
}

auto PGMutagenWrapper::utf8toUTF16(const string& str) -> wstring
{
    // Just return empty string if empty
    if (str.empty()) {
        return {};
    }

    // Convert string > wstring
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring wStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.length(), wStr.data(), sizeNeeded);

    return wStr;
}

auto PGMutagenWrapper::utf16toUTF8(const wstring& wStr) -> string
{
    // Just return empty string if empty
    if (wStr.empty()) {
        return {};
    }

    // Convert wstring > string
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), nullptr, 0, nullptr, nullptr);
    string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wStr.data(), (int)wStr.size(), str.data(), sizeNeeded, nullptr, nullptr);

    return str;
}
