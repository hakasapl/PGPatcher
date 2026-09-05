#include "util/BinaryIO.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

using namespace std;

namespace BinaryIO {

void Writer::writeBytes(const void* data,
                        size_t size)
{
    if (size == 0) {
        return;
    }

    const auto* bytes = static_cast<const std::byte*>(data);
    m_buffer.insert(m_buffer.end(), bytes, bytes + size);
}

void Writer::writeWString(const wstring& value)
{
    if (value.size() > numeric_limits<uint32_t>::max()) {
        throw runtime_error("String too long for binary serialization");
    }

    write<uint32_t>(static_cast<uint32_t>(value.size()));
    for (const auto& ch : value) {
        write<uint16_t>(static_cast<uint16_t>(ch));
    }
}

void Writer::writeString(const string& value)
{
    if (value.size() > numeric_limits<uint32_t>::max()) {
        throw runtime_error("String too long for binary serialization");
    }

    write<uint32_t>(static_cast<uint32_t>(value.size()));
    writeBytes(value.data(), value.size());
}

auto Writer::data() const -> const vector<std::byte>& { return m_buffer; }

auto Writer::size() const -> size_t { return m_buffer.size(); }

auto Writer::saveToFile(const filesystem::path& filePath) const -> bool
{
    error_code ec;
    filesystem::create_directories(filePath.parent_path(), ec);

    // Write to a temporary file first so a crash mid-write cannot leave a truncated file behind
    auto tempPath = filePath;
    tempPath += L".tmp";

    {
        ofstream out(tempPath, ios::binary | ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        out.write(reinterpret_cast<const char*>(m_buffer.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                  static_cast<streamsize>(m_buffer.size()));
        if (!out.good()) {
            out.close();
            filesystem::remove(tempPath, ec);
            return false;
        }
    }

    filesystem::rename(tempPath, filePath, ec);
    if (ec) {
        // rename cannot replace on some filesystems, fall back to remove + rename
        ec.clear();
        filesystem::remove(filePath, ec);
        ec.clear();
        filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            filesystem::remove(tempPath, ec);
            return false;
        }
    }

    return true;
}

Reader::Reader(const vector<std::byte>& buffer)
    : m_buffer(buffer)
{
}

void Reader::ensureAvailable(size_t size) const
{
    if (size > m_buffer.size() - m_pos) {
        throw runtime_error("Binary reader: unexpected end of data");
    }
}

void Reader::readBytes(void* dest,
                       size_t size)
{
    if (size == 0) {
        return;
    }

    ensureAvailable(size);
    memcpy(dest, m_buffer.data() + m_pos, size);
    m_pos += size;
}

auto Reader::readWString() -> wstring
{
    const auto length = read<uint32_t>();
    ensureAvailable(static_cast<size_t>(length) * sizeof(uint16_t));

    wstring value;
    value.resize(length);
    for (uint32_t i = 0; i < length; i++) {
        value[i] = static_cast<wchar_t>(read<uint16_t>());
    }

    return value;
}

auto Reader::readString() -> string
{
    const auto length = read<uint32_t>();
    ensureAvailable(length);

    string value;
    value.resize(length);
    readBytes(value.data(), length);

    return value;
}

auto Reader::atEnd() const -> bool { return m_pos >= m_buffer.size(); }

auto Reader::remaining() const -> size_t { return m_buffer.size() - m_pos; }

} // namespace BinaryIO
