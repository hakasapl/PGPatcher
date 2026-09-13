#include "util/FileUtil.hpp"

#include <nlohmann/detail/output/serializer.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace FileUtil {
std::vector<std::byte> fileBytes(const std::filesystem::path& filePath)
{
    std::ifstream inputFile(filePath, std::ios::binary | std::ios::ate);
    if (!inputFile.is_open()) {
        // Unable to open file.
        return { };
    }

    const auto length = inputFile.tellg();
    if (length == -1) {
        // Unable to find length.
        inputFile.close();
        return { };
    }

    inputFile.seekg(0, std::ios::beg);

    // Make a buffer of the exact size of the file and read the data into it.
    std::vector<std::byte> buffer(length);
    inputFile.read(reinterpret_cast<char*>(buffer.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                   length);

    inputFile.close();

    return buffer;
}

bool getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json)
{
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        // Unable to open file.
        return false;
    }

    try {
        inputFile >> json;
    } catch (...) {
        // Handle JSON parsing error.
        return false;
    }

    inputFile.close();
    return true;
}

bool getJSONFromBytes(const std::vector<std::byte>& bytes,
                      nlohmann::json& json)
{
    try {
        // Convert vector of bytes to string.
        std::string jsonString(bytes.size(), '\0');
        std::memcpy(jsonString.data(), bytes.data(), bytes.size());

        // Parse the JSON string.
        json = nlohmann::json::parse(jsonString);
    } catch (...) {
        // Handle JSON parsing error.
        return false;
    }

    return true;
}

bool saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& shouldBeReadable)
{
    std::ofstream outputFile;
    outputFile.exceptions(std::ios::failbit | std::ios::badbit);
    outputFile.open(filePath, std::ios::binary);
    if (!outputFile.is_open()) {
        // Unable to open file.
        return false;
    }

    if (shouldBeReadable)
        outputFile << json.dump(2, ' ', false, nlohmann::detail::error_handler_t::replace);
    else
        outputFile << json.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);

    outputFile.close();
    return true;
}
}
