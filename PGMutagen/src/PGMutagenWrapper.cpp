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

namespace {
constexpr auto dotnetRuntimePreloadErrorMessage = "DotNet Wrapper: .NET runtime failed to preload (error: 0x{:08X}).";

void dnneFailure(enum failure_type type,
                 int errorCode)
{
    switch (type) {
    case failure_load_runtime:
        spdlog::critical(dotnetRuntimePreloadErrorMessage, static_cast<unsigned>(errorCode));
        break;
    case failure_load_export:
        spdlog::critical("DotNet Wrapper failed to load a managed export (error: 0x{:08X}). "
                         "Ensure PGMutagen.dll is present and matches the expected version.",
                         static_cast<unsigned>(errorCode));
        break;
    default:
        spdlog::critical("DotNet Wrapper failed with unknown type {} (error: 0x{:08X}).",
                         static_cast<int>(type),
                         static_cast<unsigned>(errorCode));
        break;
    }
}
} // namespace

std::mutex PGMutagenWrapper::s_libMutex;

void PGMutagenWrapper::libLogMessageIfExists()
{
    static constexpr unsigned traceLog = 0;
    static constexpr unsigned debugLog = 1;
    static constexpr unsigned infoLog = 2;
    static constexpr unsigned warnLog = 3;
    static constexpr unsigned errorLog = 4;
    static constexpr unsigned criticalLog = 5;

    int level = 0;
    wchar_t* message = nullptr;
    GetLogMessage(&message, &level);

    while (message != nullptr) {
        const std::wstring messageOut(message);
        LocalFree(static_cast<HGLOBAL>(message)); // Only free if memory was allocated.
        message = nullptr;

        // Log the message.
        switch (level) {
        case traceLog:
            spdlog::trace(L"{}", messageOut);
            break;
        case debugLog:
            spdlog::debug(L"{}", messageOut);
            break;
        case infoLog:
            spdlog::info(L"{}", messageOut);
            break;
        case warnLog:
            spdlog::warn(L"{}", messageOut);
            break;
        case errorLog:
            spdlog::error(L"{}", messageOut);
            break;
        case criticalLog:
            spdlog::critical(L"{}", messageOut);
            break;
        }

        // Get the next message.
        GetLogMessage(&message, &level);
    }
}

void PGMutagenWrapper::libThrowExceptionIfExists()
{
    wchar_t* message = nullptr;
    GetLastException(&message);

    if (message == nullptr)
        return;

    const std::wstring messageOut(message);
    LocalFree(static_cast<HGLOBAL>(message)); // Only free if memory was allocated.

    throw std::runtime_error("PGMutagenWrapper: " + utf16toUTF8(messageOut));
}

void PGMutagenWrapper::libInitialize(const int& gameType,
                                     const std::wstring& exePath,
                                     const std::wstring& dataPath,
                                     const std::vector<std::wstring>& loadOrder,
                                     const unsigned& lang)
{
    // Proactively try to load the .NET runtime. try_preload_runtime() returns an error
    // code on failure instead of calling abort(), giving us the chance to surface a
    // proper exception to the caller.
    const int runtimeRC = try_preload_runtime();
    if (runtimeRC != 0) {
        spdlog::critical(dotnetRuntimePreloadErrorMessage, static_cast<unsigned>(runtimeRC));
        throw std::runtime_error("PGMutagenWrapper: .NET runtime failed to initialize. "
                                 "Check the log for details.");
    }

    set_failure_callback(dnneFailure);

    // Use vector to manage the memory for LoadOrderArr.
    std::vector<const wchar_t*> loadOrderArr;
    if (!loadOrder.empty()) {
        loadOrderArr.reserve(loadOrder.size()); // Pre-allocate the vector size
        for (const auto& mod : loadOrder)
            loadOrderArr.push_back(mod.c_str()); // Populate the vector with the c_str pointers
    }

    // Add the null terminator to the end.
    loadOrderArr.push_back(nullptr);

    {
        const std::scoped_lock lock(s_libMutex);

        Initialize(gameType, exePath.c_str(), dataPath.c_str(), loadOrderArr.data(), lang);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }
}

void PGMutagenWrapper::libPopulateObjs(const std::filesystem::path& existingModPath)
{
    const std::scoped_lock lock(s_libMutex);

    PopulateObjs(existingModPath.wstring().c_str());
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void PGMutagenWrapper::libResetPatchingState()
{
    const std::scoped_lock lock(s_libMutex);

    ResetPatchingState(0);
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

void PGMutagenWrapper::libFinalize(const std::filesystem::path& outputPath,
                                   int esmMode)
{
    const std::scoped_lock lock(s_libMutex);

    Finalize(outputPath.c_str(), esmMode);
    libLogMessageIfExists();
    libThrowExceptionIfExists();
}

auto PGMutagenWrapper::libGetModelUses(const std::wstring& modelPath) -> std::vector<ModelUse>
{
    uint8_t* buffer = nullptr;
    uint32_t length = 0;

    {
        const std::scoped_lock lock(s_libMutex);
        GetModelUses(modelPath.c_str(), &length, &buffer);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }

    if ((buffer == nullptr) || length == 0)
        return { };

    flatbuffers::Verifier verifier(buffer, length);
    if (!PGMutagenBuffers::VerifyModelUsesBuffer(verifier))
        return { };

    std::vector<ModelUse> modelUsesOut;

    const auto* const modelUses = PGMutagenBuffers::GetModelUses(buffer);
    for (const auto* const mu : *modelUses->uses()) {
        auto curUse = ModelUse();

        curUse.modName = std::wstring(mu->mod_name()->begin(), mu->mod_name()->end());
        curUse.formID = mu->form_id();
        curUse.subModel = std::string(mu->sub_model()->begin(), mu->sub_model()->end());
        curUse.isWeighted = mu->is_weighted();
        curUse.singlepassMATO = mu->singlepass_mato();
        curUse.isIgnored = mu->is_ignored();
        curUse.type = std::string(mu->type()->begin(), mu->type()->end());

        for (const auto* const altTex : *mu->alternate_textures()) {
            auto curAltTex = AlternateTexture();
            curAltTex.slotID = altTex->slot_id();

            // No slots.
            if (altTex->slots() == nullptr || altTex->slots()->textures() == nullptr)
                continue;

            auto slots = std::array<std::wstring, numPluginTextureSlots> { };
            const auto* textures = altTex->slots()->textures();
            for (int i = 0; std::cmp_less(i, numPluginTextureSlots) && std::cmp_less(i, textures->size()); ++i) {
                const auto* texStr = textures->Get(i);
                if (texStr != nullptr)
                    slots.at(i) = std::wstring(texStr->begin(), texStr->end());
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
    flatbuffers::FlatBufferBuilder builder(defaultBufferSize);

    std::vector<flatbuffers::Offset<PGMutagenBuffers::ModelUse>> modelUsesOffsets;
    modelUsesOffsets.reserve(modelUses.size());

    for (const auto& mu : modelUses) {
        const auto modNameOffset = builder.CreateString(utf16toUTF8(mu.modName));
        const auto subModelOffset = builder.CreateString(mu.subModel);
        const auto meshFileOffset = builder.CreateString(utf16toUTF8(mu.meshFile));

        std::vector<flatbuffers::Offset<PGMutagenBuffers::AlternateTexture>> altTexOffsets;
        altTexOffsets.reserve(mu.alternateTextures.size());

        for (const auto& altTex : mu.alternateTextures) {
            std::vector<flatbuffers::Offset<flatbuffers::String>> texOffsets;
            texOffsets.reserve(numPluginTextureSlots);

            for (const auto& tex : altTex.slots)
                texOffsets.push_back(builder.CreateString(utf16toUTF8(tex)));

            const auto slotsOffset = PGMutagenBuffers::CreateTextureSet(builder, builder.CreateVector(texOffsets));

            const auto altTexOffset
                = PGMutagenBuffers::CreateAlternateTexture(builder, altTex.slotID, altTex.slotIDNew, slotsOffset);
            altTexOffsets.push_back(altTexOffset);
        }

        const auto altTexVectorOffset = builder.CreateVector(altTexOffsets);

        const auto modelUseOffset = PGMutagenBuffers::CreateModelUse(builder,
                                                                     modNameOffset,
                                                                     mu.formID,
                                                                     subModelOffset,
                                                                     false,
                                                                     meshFileOffset,
                                                                     false,
                                                                     false,
                                                                     { },
                                                                     altTexVectorOffset);
        modelUsesOffsets.push_back(modelUseOffset);
    }

    const auto modelUsesVectorOffset = builder.CreateVector(modelUsesOffsets);
    const auto modelUsesRoot = PGMutagenBuffers::CreateModelUses(builder, modelUsesVectorOffset);
    builder.Finish(modelUsesRoot);

    uint8_t const* buf = builder.GetBufferPointer();
    unsigned const size = builder.GetSize();

    {
        const std::scoped_lock lock(s_libMutex);
        SetModelUses(size, buf);
        libLogMessageIfExists();
        libThrowExceptionIfExists();
    }
}

std::wstring PGMutagenWrapper::utf8toUTF16(const std::string& str)
{
    // Just return empty string if empty.
    if (str.empty())
        return { };

    // Convert string > wstring.
    const int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring wStr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), wStr.data(), sizeNeeded);

    return wStr;
}

std::string PGMutagenWrapper::utf16toUTF8(const std::wstring& wStr)
{
    // Just return empty string if empty.
    if (wStr.empty())
        return { };

    // Convert wstring > string.
    const int sizeNeeded
        = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(sizeNeeded, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wStr.data(), static_cast<int>(wStr.size()), str.data(), sizeNeeded, nullptr, nullptr);

    return str;
}
