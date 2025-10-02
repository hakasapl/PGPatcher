#pragma once

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace ParallaxGenUtil {

// narrow and wide string conversion functions
auto utf8toUTF16(const std::string& str) -> std::wstring;
auto utf16toUTF8(const std::wstring& str) -> std::string;
auto windows1252toUTF16(const std::string& str) -> std::wstring;
auto utf16toWindows1252(const std::wstring& str) -> std::string;
auto asciitoUTF16(const std::string& str) -> std::wstring;
auto utf16toASCII(const std::wstring& str) -> std::string;

auto utf8VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;
auto utf16VectorToUTF8(const std::vector<std::wstring>& vec) -> std::vector<std::string>;
auto windows1252VectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;
auto utf16VectorToWindows1252(const std::vector<std::wstring>& vec) -> std::vector<std::string>;
auto asciiVectorToUTF16(const std::vector<std::string>& vec) -> std::vector<std::wstring>;
auto utf16VectorToASCII(const std::vector<std::wstring>& vec) -> std::vector<std::string>;

auto containsOnlyAscii(const std::string& str) -> bool;
auto containsOnlyAscii(const std::wstring& str) -> bool;

auto toLowerASCII(const std::wstring& str) -> std::wstring;

// Get the file bytes of a file
auto getFileBytes(const std::filesystem::path& filePath) -> std::vector<std::byte>;

auto getJSON(const std::filesystem::path& filePath, nlohmann::json& json) -> bool;
auto getJSONFromBytes(const std::vector<std::byte>& bytes, nlohmann::json& json) -> bool;
auto saveJSON(const std::filesystem::path& filePath, const nlohmann::json& json, const bool& readable) -> bool;

auto checkIfStringInJSONArray(const nlohmann::json& json, const std::string& str) -> bool;

/**
 * @brief Get the Plugin Path From Data Path object (removes textures or meshes from beginning of path)
 *
 * @param dataPath The data path to process
 * @return std::filesystem::path The plugin path derived from the data path, or the original path if it does not start
 * with "meshes" or "textures"
 */
auto getPluginPathFromDataPath(const std::filesystem::path& dataPath) -> std::filesystem::path;

// Template Functions
template <typename T> auto isInVector(const std::vector<T>& vec, const T& test) -> bool
{
    return std::find(vec.begin(), vec.end(), test) != vec.end();
}

// concatenates two vectors without duplicates
template <typename T> void concatenateVectorsWithoutDuplicates(std::vector<T>& vec1, const std::vector<T>& vec2)
{
    std::unordered_set<T> set(vec1.begin(), vec1.end());

    for (const auto& element : vec2) {
        if (set.find(element) == set.end()) {
            vec1.push_back(element);
            set.insert(element);
        }
    }
};

// adds an element to a vector if it is not already present
template <typename T> void addUniqueElement(std::vector<T>& vec, const T& element)
{
    if (std::find(vec.begin(), vec.end(), element) == vec.end()) {
        vec.push_back(element);
    }
}

auto getThreadID() -> std::string;

} // namespace ParallaxGenUtil
