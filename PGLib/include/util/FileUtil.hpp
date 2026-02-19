#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace FileUtil {
auto getFileBytes(const std::filesystem::path& filePath) -> std::vector<std::byte>;

auto getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json) -> bool;
auto getJSONFromBytes(const std::vector<std::byte>& bytes,
                      nlohmann::json& json) -> bool;
auto saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& readable) -> bool;
}
