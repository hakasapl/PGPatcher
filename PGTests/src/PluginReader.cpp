#include "PluginReader.hpp"

#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace TestUtil::PluginReader {

uint32_t readUInt32(const std::vector<std::byte>& bytes,
                    size_t offset)
{
    uint32_t value = 0;
    if (offset + sizeof(value) <= bytes.size())
        std::memcpy(&value, &bytes.at(offset), sizeof(value));
    return value;
}

std::string signature(const std::vector<std::byte>& bytes,
                      size_t offset)
{
    std::string sig;
    for (size_t i = offset; i < offset + 4 && i < bytes.size(); i++)
        sig.push_back(static_cast<char>(bytes[i]));
    return sig;
}

std::vector<std::byte> subrecord(const std::filesystem::path& plugin,
                                 const std::string& type,
                                 uint32_t formID,
                                 const std::string& sub)
{
    if (!std::filesystem::exists(plugin))
        return { };

    const auto bytes = FileUtil::fileBytes(plugin);
    size_t pos = 0;
    while (pos + 24 <= bytes.size()) {
        const auto sig = signature(bytes, pos);
        const auto size = readUInt32(bytes, pos + 4);
        if (sig == "GRUP") {
            pos += 24; // the records of the group follow its header
            continue;
        }

        if (sig == type && readUInt32(bytes, pos + 12) == formID) {
            size_t subPos = pos + 24;
            const size_t end = std::min(subPos + size, bytes.size());
            while (subPos + 6 <= end) {
                const auto subSig = signature(bytes, subPos);
                uint16_t subSize = 0;
                std::memcpy(&subSize, &bytes.at(subPos + 4), sizeof(subSize));
                subPos += 6;
                if (subSig == sub) {
                    const auto first = static_cast<std::ptrdiff_t>(subPos);
                    const auto last = static_cast<std::ptrdiff_t>(std::min(subPos + subSize, end));
                    return { bytes.begin() + first, bytes.begin() + last };
                }
                subPos += subSize;
            }
            return { };
        }

        pos += 24 + size;
    }

    return { };
}

std::string subrecordString(const std::filesystem::path& plugin,
                            const std::string& type,
                            uint32_t formID,
                            const std::string& sub)
{
    std::string text;
    for (const auto byte : subrecord(plugin, type, formID, sub))
        text.push_back(static_cast<char>(byte));
    while (!text.empty() && text.back() == '\0')
        text.pop_back();
    return text;
}

std::optional<PGTypes::ObjectBounds> objectBounds(const std::filesystem::path& plugin,
                                                  const std::string& type,
                                                  uint32_t formID)
{
    const auto data = subrecord(plugin, type, formID, "OBND");
    std::array<int16_t, 6> values { };
    if (data.size() != sizeof(values))
        return std::nullopt;

    std::memcpy(values.data(), data.data(), data.size());
    return PGTypes::ObjectBounds {
        .min = { values[0], values[1], values[2] },
        .max = { values[3], values[4], values[5] },
    };
}

}
