#pragma once

#include "pgutil/PGEnums.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

constexpr unsigned NUM_TEXTURE_SLOTS = 9;

namespace PGTypes {
using TextureSet = std::array<std::wstring, NUM_TEXTURE_SLOTS>;
using TextureSetStr = std::array<std::string, NUM_TEXTURE_SLOTS>;

struct TextureSetHash {
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

auto getTextureSlotsFromStr(const std::string& slots) -> TextureSet;

auto getStrFromTextureSlots(const TextureSet& slots) -> std::string;

/// @brief texture used by parallaxgen with type
struct PGTexture {
    /// @brief relative path in the data directory
    std::filesystem::path path;
    PGEnums::TextureType type {};

    // Equality operator
    auto operator==(const PGTexture& other) const -> bool { return path == other.path && type == other.type; }
};

struct PGTextureHasher {
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
