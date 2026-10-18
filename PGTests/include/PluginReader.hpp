#pragma once

#include "pgutil/PGTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace TestUtil::PluginReader {

/// @brief Little endian unsigned integer at an offset of a byte buffer, 0 when it does not fit.
uint32_t readUInt32(const std::vector<std::byte>& bytes,
                    size_t offset);

/// @brief Four character signature at an offset of a plugin file.
std::string signature(const std::vector<std::byte>& bytes,
                      size_t offset);

/// @brief Data of a subrecord of the record with the given type and form ID, empty if there is none.
std::vector<std::byte> subrecord(const std::filesystem::path& plugin,
                                 const std::string& type,
                                 uint32_t formID,
                                 const std::string& sub);

/// @brief Zero terminated string subrecord of a record, without the terminator.
std::string subrecordString(const std::filesystem::path& plugin,
                            const std::string& type,
                            uint32_t formID,
                            const std::string& sub);

/// @brief The OBND subrecord of a record, std::nullopt if the plugin or the record is not there or has none.
std::optional<PGTypes::ObjectBounds> objectBounds(const std::filesystem::path& plugin,
                                                  const std::string& type,
                                                  uint32_t formID);

}
