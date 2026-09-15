#pragma once

#include "pgutil/PGEnums.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

constexpr unsigned numTextureSlots = 9;

/**
 * @brief Namespace containing core texture and mesh type definitions used throughout PGPatcher.
 */
namespace PGTypes {
/// @brief Array of wide-string texture paths indexed by texture slot (up to NUM_TEXTURE_SLOTS entries).
using TextureSet = std::array<std::wstring, numTextureSlots>;
/// @brief Array of narrow-string texture paths indexed by texture slot (up to NUM_TEXTURE_SLOTS entries).
using TextureSetStr = std::array<std::string, numTextureSlots>;

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
    std::size_t operator()(const TextureSet& ts) const
    {
        static constexpr auto magicHash = 0x9e3779b9; // Golden ratio
        static constexpr auto bitMixLeft = 6;
        static constexpr auto bitMixRight = 2;
        std::size_t h = 0;
        for (const auto& s : ts)
            h ^= std::hash<std::wstring> { }(s) + magicHash + (h << bitMixLeft) + (h >> bitMixRight); // hash combine
        return h;
    }
};

/**
 * @brief Parses a comma-separated string of texture slot paths into a TextureSet.
 *
 * @param slots Comma-separated UTF-8 texture paths (up to NUM_TEXTURE_SLOTS entries).
 * @return TextureSet populated from the parsed paths.
 */
TextureSet textureSlotsFromStr(const std::string& slots);

/**
 * @brief Serializes a TextureSet into a comma-separated UTF-8 string.
 *
 * @param slots The TextureSet to serialize.
 * @return Comma-separated string of texture slot paths.
 */
std::string strFromTextureSlots(const TextureSet& slots);

/// @brief A single (texture, slot, type) vote produced by reading a shape of a NIF during texture classification
struct TextureVote {
    std::wstring texture; /**< lowercase relative texture path */
    PGEnums::TextureSlots slot { };
    PGEnums::TextureType type { };
};

/// @brief Result of the complex material classification of an environment mask texture
struct CMClassification {
    bool isCM = false;
    bool hasEnvMask = false;
    bool hasGlossiness = false;
    bool hasMetalness = false;

    bool operator==(const CMClassification& other) const = default;
};

/// @brief texture used by parallaxgen with type
struct PGTexture {
    /// @brief relative path in the data directory
    std::filesystem::path path;
    PGEnums::TextureType type { };

    // Equality operator.
    bool operator==(const PGTexture& other) const { return path == other.path && type == other.type; }
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
    size_t operator()(const PGTexture& texture) const
    {
        // Hash the path and the texture type, and combine them.
        const std::size_t pathHash = std::hash<std::filesystem::path>()(texture.path);
        const std::size_t typeHash = std::hash<int>()(static_cast<int>(texture.type));

        // Combine the hashes using bitwise XOR and bit shifting.
        return pathHash ^ (typeHash << 1);
    }
};
}
