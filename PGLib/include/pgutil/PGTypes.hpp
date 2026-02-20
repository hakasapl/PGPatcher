#pragma once

#include "pgutil/PGEnums.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

constexpr unsigned NUM_TEXTURE_SLOTS = 9;

/**
 * @brief Namespace containing core texture and mesh type definitions used throughout PGPatcher.
 */
namespace PGTypes {
/// @brief Array of wide-string texture paths indexed by texture slot (up to NUM_TEXTURE_SLOTS entries).
using TextureSet = std::array<std::wstring, NUM_TEXTURE_SLOTS>;
/// @brief Array of narrow-string texture paths indexed by texture slot (up to NUM_TEXTURE_SLOTS entries).
using TextureSetStr = std::array<std::string, NUM_TEXTURE_SLOTS>;

/**
 * @brief Hash functor for TextureSet, enabling use as an unordered_map/unordered_set key.
 */
struct TextureSetHash {
    /**
     * @brief Computes a combined hash over all texture slot strings in the set.
     *
     * @param ts The TextureSet to hash.
     * @return Combined hash value.
     */
    auto operator()(const TextureSet& ts) const -> std::size_t
    {
        static constexpr auto MAGIC_HASH = 0x9e3779b9; // Golden ratio
        static constexpr auto BIT_MIX_LEFT = 6;
        static constexpr auto BIT_MIX_RIGHT = 2;
        std::size_t h = 0;
        for (const auto& s : ts) {
            h ^= std::hash<std::wstring> {}(s) + MAGIC_HASH + (h << BIT_MIX_LEFT)
                + (h >> BIT_MIX_RIGHT); // hash combine
        }
        return h;
    }
};

/**
 * @brief Parses a comma-separated string of texture slot paths into a TextureSet.
 *
 * @param slots Comma-separated UTF-8 texture paths (up to NUM_TEXTURE_SLOTS entries).
 * @return TextureSet populated from the parsed paths.
 */
auto getTextureSlotsFromStr(const std::string& slots) -> TextureSet;

/**
 * @brief Serializes a TextureSet into a comma-separated UTF-8 string.
 *
 * @param slots The TextureSet to serialize.
 * @return Comma-separated string of texture slot paths.
 */
auto getStrFromTextureSlots(const TextureSet& slots) -> std::string;

/// @brief texture used by parallaxgen with type
struct PGTexture {
    /// @brief relative path in the data directory
    std::filesystem::path path;
    PGEnums::TextureType type {};

    // Equality operator
    auto operator==(const PGTexture& other) const -> bool { return path == other.path && type == other.type; }
};

/**
 * @brief Hash functor for PGTexture, enabling use as an unordered_map/unordered_set key.
 */
struct PGTextureHasher {
    /**
     * @brief Computes a hash combining the texture path and type.
     *
     * @param texture The PGTexture to hash.
     * @return Combined hash value.
     */
    auto operator()(const PGTexture& texture) const -> size_t
    {
        // Hash the path and the texture type, and combine them
        const std::size_t pathHash = std::hash<std::filesystem::path>()(texture.path);
        const std::size_t typeHash = std::hash<int>()(static_cast<int>(texture.type));

        // Combine the hashes using bitwise XOR and bit shifting
        return pathHash ^ (typeHash << 1);
    }
};
}
