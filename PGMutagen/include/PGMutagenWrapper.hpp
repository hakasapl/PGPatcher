#pragma once

#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

class PGMutagenWrapper {
private:
    static constexpr int NUM_PLUGIN_TEXTURE_SLOTS = 8;
    static constexpr int DEFAULT_BUFFER_SIZE = 1024;

    static std::mutex s_libMutex;

    static void libLogMessageIfExists();
    static void libThrowExceptionIfExists();

public:
    struct AlternateTexture {
        int slotID = 0;
        int slotIDNew = 0;
        std::array<std::wstring, NUM_PLUGIN_TEXTURE_SLOTS> slots;
    };

    struct ModelUse {
        std::wstring modName;
        unsigned int formID;
        std::string subModel;
        bool isWeighted;
        std::wstring meshFile;
        bool singlepassMATO;
        bool isIgnored;
        std::string type;
        std::vector<AlternateTexture> alternateTextures;
    };

    static void libInitialize(const int& gameType, const std::wstring& exePath, const std::wstring& dataPath,
        const std::vector<std::wstring>& loadOrder = {}, const unsigned int& lang = 0);
    static void libPopulateObjs(const std::filesystem::path& existingModPath = {});
    static void libFinalize(const std::filesystem::path& outputPath, const bool& esmify);

    static auto libGetModelUses(const std::wstring& modelPath) -> std::vector<ModelUse>;
    static void libSetModelUses(const std::vector<ModelUse>& modelUses);

private:
    // Helpers
    static auto utf8toUTF16(const std::string& str) -> std::wstring;
    static auto utf16toUTF8(const std::wstring& wStr) -> std::string;
};
