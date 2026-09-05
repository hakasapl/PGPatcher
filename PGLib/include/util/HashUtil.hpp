#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

/**
 * @brief Deterministic (build- and run-stable) hashing helpers.
 *
 * std::hash is not guaranteed to be stable across builds, so anything that is persisted to disk and compared on a
 * later run must use these helpers instead.
 */
namespace HashUtil {

/**
 * @brief Incremental 64-bit FNV-1a hasher.
 */
class Fnv1a64 {
private:
    static constexpr uint64_t OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static constexpr uint64_t PRIME = 0x100000001b3ULL;

    uint64_t m_hash = OFFSET_BASIS;

public:
    /**
     * @brief Mixes raw bytes into the hash.
     *
     * @param data Pointer to the bytes.
     * @param size Number of bytes.
     */
    void addBytes(const void* data,
                  size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++) {
            m_hash ^= static_cast<uint64_t>(bytes[i]);
            m_hash *= PRIME;
        }
    }

    /**
     * @brief Mixes an integral or enum value into the hash (as its little-endian byte representation).
     */
    template <typename T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void add(const T& value)
    {
        addBytes(&value, sizeof(T));
    }

    /**
     * @brief Mixes a wide string (length-prefixed) into the hash.
     */
    void add(const std::wstring& value)
    {
        add(static_cast<uint64_t>(value.size()));
        addBytes(value.data(), value.size() * sizeof(wchar_t));
    }

    /**
     * @brief Mixes a narrow string (length-prefixed) into the hash.
     */
    void add(const std::string& value)
    {
        add(static_cast<uint64_t>(value.size()));
        addBytes(value.data(), value.size());
    }

    /**
     * @brief Mixes a floating point value into the hash.
     */
    void add(const float& value) { addBytes(&value, sizeof(float)); }

    /**
     * @brief Mixes a floating point value into the hash.
     */
    void add(const double& value) { addBytes(&value, sizeof(double)); }

    /**
     * @brief Returns the current hash value.
     */
    [[nodiscard]] auto value() const -> uint64_t { return m_hash; }
};

} // namespace HashUtil
