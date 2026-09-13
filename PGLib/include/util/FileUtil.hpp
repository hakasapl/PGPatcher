#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

/**
 * @brief Utility functions for common file I/O operations.
 */
namespace FileUtil {

/**
 * @brief Reads the entire contents of a file into a byte vector.
 *
 * @param filePath Path to the file to read.
 * @return A vector of bytes containing the file contents, or an empty vector on failure.
 */
std::vector<std::byte> fileBytes(const std::filesystem::path& filePath);

/**
 * @brief Parses a JSON file from disk into a nlohmann::json object.
 *
 * @param filePath Path to the JSON file.
 * @param json Output parameter populated with the parsed JSON on success.
 * @return true if the file was opened and parsed successfully, false otherwise.
 */
bool getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json);

/**
 * @brief Parses JSON from a byte vector into a nlohmann::json object.
 *
 * @param bytes Raw bytes containing a UTF-8 encoded JSON string.
 * @param json Output parameter populated with the parsed JSON on success.
 * @return true if parsing succeeded, false otherwise.
 */
bool getJSONFromBytes(const std::vector<std::byte>& bytes,
                      nlohmann::json& json);

/**
 * @brief Serializes a nlohmann::json object and writes it to a file.
 *
 * @param filePath Destination file path to write the JSON to.
 * @param json The JSON object to serialize.
 * @param readable If true, the output is pretty-printed with 2-space indentation; otherwise compact.
 * @return true if the file was written successfully, false otherwise.
 */
bool saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& readable);

}
