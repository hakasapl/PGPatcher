#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
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
    template<typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>)
    void write(const T& value)
    {
        const auto bytes = std::as_bytes(std::span<const T, 1>(&value, 1));
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
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
    [[nodiscard]] const std::vector<std::byte>& data() const;

    /**
     * @brief Returns the number of bytes written so far.
     */
    [[nodiscard]] size_t size() const;

    /**
     * @brief Atomically writes the buffer to a file (writes to a temporary file first, then replaces the target).
     *
     * @param filePath Destination file.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool saveToFile(const std::filesystem::path& filePath) const;
};

/**
 * @brief Bounds-checked reader over an in-memory byte buffer. Throws std::runtime_error when reading past the end.
 */
class Reader {
private:
    std::span<const std::byte> m_buffer;
    size_t m_pos = 0;

    void ensureAvailable(size_t size) const;

public:
    /**
     * @brief Constructs a reader over an existing buffer. The buffer must outlive the reader.
     */
    explicit Reader(std::span<const std::byte> buffer);

    /**
     * @brief Reads the raw representation of an integral, enum, or floating point value.
     */
    template<typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>)
    T read()
    {
        ensureAvailable(sizeof(T));
        T value { };
        std::memcpy(&value, m_buffer.subspan(m_pos, sizeof(T)).data(), sizeof(T));
        m_pos += sizeof(T);
        return value;
    }

    /**
     * @brief Reads a boolean written by Writer::writeBool.
     */
    bool readBool() { return read<uint8_t>() != 0U; }

    /**
     * @brief Reads raw bytes without any length prefix.
     */
    void readBytes(void* dest,
                   size_t size);

    /**
     * @brief Reads a wide string written by Writer::writeWString.
     */
    std::wstring readWString();

    /**
     * @brief Reads a narrow string written by Writer::writeString.
     */
    std::string readString();

    /**
     * @brief Returns true when every byte of the buffer has been consumed.
     */
    [[nodiscard]] bool atEnd() const;

    /**
     * @brief Returns the number of bytes remaining.
     */
    [[nodiscard]] size_t remaining() const;
};

} // namespace BinaryIO
