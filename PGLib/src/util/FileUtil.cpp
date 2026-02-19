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

using namespace std;

namespace FileUtil {
auto getFileBytes(const filesystem::path& filePath) -> vector<std::byte>
{
    ifstream inputFile(filePath, ios::binary | ios::ate);
    if (!inputFile.is_open()) {
        // Unable to open file
        return {};
    }

    auto length = inputFile.tellg();
    if (length == -1) {
        // Unable to find length
        inputFile.close();
        return {};
    }

    inputFile.seekg(0, ios::beg);

    // Make a buffer of the exact size of the file and read the data into it.
    vector<std::byte> buffer(length);
    inputFile.read(reinterpret_cast<char*>(buffer.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                   length);

    inputFile.close();

    return buffer;
}

auto getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json) -> bool
{
    ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        // Unable to open file
        return false;
    }

    try {
        inputFile >> json;
    } catch (...) {
        // Handle JSON parsing error
        return false;
    }

    inputFile.close();
    return true;
}

auto getJSONFromBytes(const vector<std::byte>& bytes,
                      nlohmann::json& json) -> bool
{
    try {
        // Convert vector of bytes to string
        std::string jsonString(bytes.size(), '\0');
        std::memcpy(jsonString.data(), bytes.data(), bytes.size());

        // Parse the JSON string
        json = nlohmann::json::parse(jsonString);
    } catch (...) {
        // Handle JSON parsing error
        return false;
    }

    return true;
}

auto saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& readable) -> bool
{
    ofstream outputFile;
    outputFile.exceptions(std::ios::failbit | std::ios::badbit);
    outputFile.open(filePath, ios::binary);
    if (!outputFile.is_open()) {
        // Unable to open file
        return false;
    }

    if (readable) {
        outputFile << json.dump(2, ' ', false, nlohmann::detail::error_handler_t::replace);
    } else {
        outputFile << json.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);
    }

    outputFile.close();
    return true;
}
}
