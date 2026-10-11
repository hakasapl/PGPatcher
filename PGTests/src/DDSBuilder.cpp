#include "DDSBuilder.hpp"

#include "TestUtil.hpp"

#include <DirectXTex.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace TestUtil::DDSBuilder {

DirectX::ScratchImage rgbaImage(size_t width,
                                size_t height,
                                const PixelFn& pixel)
{
    DirectX::ScratchImage image;
    if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1)))
        throw std::runtime_error("failed to create image");

    const auto* img = image.GetImage(0, 0, 0);
    const std::span<uint8_t> pixels(img->pixels, img->rowPitch * height);
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            const auto color = pixel(x, y);
            const size_t offset = (y * img->rowPitch) + (x * 4);
            pixels[offset] = color.r;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.b;
            pixels[offset + 3] = color.a;
        }
    }

    return image;
}

DirectX::ScratchImage rgbaImage(size_t width,
                                size_t height,
                                const RGBA& color)
{
    return rgbaImage(width, height, [&color](size_t, size_t) { return color; });
}

DirectX::ScratchImage compress(const DirectX::ScratchImage& image,
                               DXGI_FORMAT format)
{
    DirectX::ScratchImage out;
    if (FAILED(DirectX::Compress(image.GetImages(),
                                 image.GetImageCount(),
                                 image.GetMetadata(),
                                 format,
                                 DirectX::TEX_COMPRESS_DEFAULT,
                                 DirectX::TEX_THRESHOLD_DEFAULT,
                                 out))) {
        throw std::runtime_error("failed to compress image");
    }

    return out;
}

std::vector<std::byte> ddsBytes(const DirectX::ScratchImage& image)
{
    DirectX::Blob blob;
    if (FAILED(DirectX::SaveToDDSMemory(
            image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, blob))) {
        throw std::runtime_error("failed to serialize image");
    }

    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(blob.GetBufferPointer()), blob.GetBufferSize());
    const auto data = std::as_bytes(raw);
    return { data.begin(), data.end() };
}

void writeDDS(const std::filesystem::path& path,
              const DirectX::ScratchImage& image)
{
    writeFile(path, ddsBytes(image));
}

void writeDDS(const std::filesystem::path& path,
              size_t width,
              size_t height,
              const RGBA& color)
{
    writeDDS(path, rgbaImage(width, height, color));
}

void writeDDS(const std::filesystem::path& path,
              size_t width,
              size_t height,
              const PixelFn& pixel)
{
    writeDDS(path, rgbaImage(width, height, pixel));
}

}
