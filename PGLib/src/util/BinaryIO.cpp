#include "util/BinaryIO.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace BinaryIO {

void Writer::writeBytes(const void* data,
                        size_t size)
{
    if (!size)
        return;

    const std::span<const std::byte> bytes(static_cast<const std::byte*>(data), size);
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
}

void Writer::writeWString(const std::wstring& value)
{
    if (value.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("String too long for binary serialization");

    write<uint32_t>(static_cast<uint32_t>(value.size()));
    for (const auto& ch : value)
        write<uint16_t>(static_cast<uint16_t>(ch));
}

void Writer::writeString(const std::string& value)
{
    if (value.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("String too long for binary serialization");

    write<uint32_t>(static_cast<uint32_t>(value.size()));
    writeBytes(value.data(), value.size());
}

const std::vector<std::byte>& Writer::data() const { return m_buffer; }

size_t Writer::size() const { return m_buffer.size(); }

bool Writer::saveToFile(const std::filesystem::path& filePath) const
{
    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);

    // Write to a temporary file first so a crash mid-write cannot leave a truncated file behind.
    auto tempPath = filePath;
    tempPath += L".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;

        out.write(reinterpret_cast<const char*>(m_buffer.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                  static_cast<std::streamsize>(m_buffer.size()));
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, filePath, ec);
    if (ec) {
        // Rename cannot replace on some filesystems, fall back to remove + rename.
        ec.clear();
        std::filesystem::remove(filePath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    return true;
}

Reader::Reader(std::span<const std::byte> buffer)
    : m_buffer(buffer)
{
}

void Reader::ensureAvailable(size_t size) const
{
    if (size > m_buffer.size() - m_pos)
        throw std::runtime_error("Binary reader: unexpected end of data");
}

void Reader::readBytes(void* dest,
                       size_t size)
{
    if (!size)
        return;

    ensureAvailable(size);
    memcpy(dest, m_buffer.subspan(m_pos, size).data(), size);
    m_pos += size;
}

std::wstring Reader::readWString()
{
    const auto length = read<uint32_t>();
    ensureAvailable(static_cast<size_t>(length) * sizeof(uint16_t));

    std::wstring value;
    value.resize(length);
    for (uint32_t i = 0; i < length; i++)
        value[i] = static_cast<wchar_t>(read<uint16_t>());

    return value;
}

std::string Reader::readString()
{
    const auto length = read<uint32_t>();
    ensureAvailable(length);

    std::string value;
    value.resize(length);
    readBytes(value.data(), length);

    return value;
}

bool Reader::atEnd() const { return m_pos >= m_buffer.size(); }

size_t Reader::remaining() const { return m_buffer.size() - m_pos; }

} // namespace BinaryIO
