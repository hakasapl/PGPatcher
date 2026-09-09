#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @brief Little-endian binary serialization helpers used for on-disk caches.
 */
namespace BinaryIO {

/**
 * @brief Appends primitive values and strings to an in-memory byte buffer.
 */
class Writer {
private:
    std::vector<std::byte> m_buffer;

public:
    /**
     * @brief Appends the raw representation of an integral, enum, or floating point value.
     */
    template <typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>)
    void write(const T& value)
    {
        const auto* bytes
            = reinterpret_cast<const std::byte*>(&value); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        m_buffer.insert(m_buffer.end(), bytes, bytes + sizeof(T));
    }

    /**
     * @brief Appends a boolean as a single byte.
     */
    void writeBool(bool value) { write<uint8_t>(value ? 1U : 0U); }

    /**
     * @brief Appends raw bytes without any length prefix.
     */
    void writeBytes(const void* data,
                    size_t size);

    /**
     * @brief Appends a wide string as a 32-bit code unit count followed by UTF-16 code units.
     */
    void writeWString(const std::wstring& value);

    /**
     * @brief Appends a narrow string as a 32-bit byte count followed by the bytes.
     */
    void writeString(const std::string& value);

    /**
     * @brief Returns the bytes written so far.
     */
    [[nodiscard]] auto data() const -> const std::vector<std::byte>&;

    /**
     * @brief Returns the number of bytes written so far.
     */
    [[nodiscard]] auto size() const -> size_t;

    /**
     * @brief Atomically writes the buffer to a file (writes to a temporary file first, then replaces the target).
     *
     * @param filePath Destination file.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] auto saveToFile(const std::filesystem::path& filePath) const -> bool;
};

/**
 * @brief Bounds-checked reader over an in-memory byte buffer. Throws std::runtime_error when reading past the end.
 */
class Reader {
private:
    const std::vector<std::byte>& m_buffer;
    size_t m_pos = 0;

    void ensureAvailable(size_t size) const;

public:
    /**
     * @brief Constructs a reader over an existing buffer. The buffer must outlive the reader.
     */
    explicit Reader(const std::vector<std::byte>& buffer);

    /**
     * @brief Reads the raw representation of an integral, enum, or floating point value.
     */
    template <typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>)
    auto read() -> T
    {
        ensureAvailable(sizeof(T));
        T value {};
        std::memcpy(&value, m_buffer.data() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return value;
    }

    /**
     * @brief Reads a boolean written by Writer::writeBool.
     */
    auto readBool() -> bool { return read<uint8_t>() != 0U; }

    /**
     * @brief Reads raw bytes without any length prefix.
     */
    void readBytes(void* dest,
                   size_t size);

    /**
     * @brief Reads a wide string written by Writer::writeWString.
     */
    auto readWString() -> std::wstring;

    /**
     * @brief Reads a narrow string written by Writer::writeString.
     */
    auto readString() -> std::string;

    /**
     * @brief Returns true when every byte of the buffer has been consumed.
     */
    [[nodiscard]] auto atEnd() const -> bool;

    /**
     * @brief Returns the number of bytes remaining.
     */
    [[nodiscard]] auto remaining() const -> size_t;
};

} // namespace BinaryIO
