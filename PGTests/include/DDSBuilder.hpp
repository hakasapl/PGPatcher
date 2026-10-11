#pragma once

#include <DirectXTex.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

/**
 * @brief Builds small DDS textures for tests.
 */
namespace TestUtil::DDSBuilder {
/// @brief One pixel of an 8 bit RGBA image.
struct RGBA {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

/// @brief Gives the color of the pixel at (x, y).
using PixelFn = std::function<RGBA(size_t, size_t)>;

/// @brief Creates an R8G8B8A8_UNORM image with one mip level.
DirectX::ScratchImage rgbaImage(size_t width,
                                size_t height,
                                const PixelFn& pixel);

/// @brief Creates an R8G8B8A8_UNORM image of one color (opaque black by default).
DirectX::ScratchImage rgbaImage(size_t width,
                                size_t height,
                                const RGBA& color = { });

/// @brief Block compresses an image (BC1 drops the alpha channel, BC3 keeps it).
DirectX::ScratchImage compress(const DirectX::ScratchImage& image,
                               DXGI_FORMAT format);

/// @brief Serializes an image as a DDS file.
std::vector<std::byte> ddsBytes(const DirectX::ScratchImage& image);

/// @brief Writes an image as a DDS file, creating its folders.
void writeDDS(const std::filesystem::path& path,
              const DirectX::ScratchImage& image);

/// @brief Writes an R8G8B8A8_UNORM DDS of one color.
void writeDDS(const std::filesystem::path& path,
              size_t width,
              size_t height,
              const RGBA& color = { });

/// @brief Writes an R8G8B8A8_UNORM DDS computed per pixel.
void writeDDS(const std::filesystem::path& path,
              size_t width,
              size_t height,
              const PixelFn& pixel);
}
