#include "PCH.h"

#include "Gif.hpp"

#include "util/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>

// stb_image decodes the GIF (frames composed, delays in milliseconds) and the PNG of the icon; only those parts are
// built.
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#pragma warning(pop)

namespace PGLite {

namespace {

constexpr std::size_t iconHeaderSize = 6;
constexpr std::size_t iconEntrySize = 16;
constexpr unsigned char pngSignature[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

std::uint16_t readU16(const unsigned char* data) { return static_cast<std::uint16_t>(data[0] | (data[1] << 8)); }

std::uint32_t readU32(const unsigned char* data)
{
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

}

bool AnimatedImage::load(const unsigned char* data,
                         std::size_t size,
                         ID3D11Device& device)
{
    m_frames.clear();

    int* delays = nullptr;
    int width = 0;
    int height = 0;
    int frameCount = 0;
    int channels = 0;
    auto* const pixels
        = stbi_load_gif_from_memory(data, static_cast<int>(size), &delays, &width, &height, &frameCount, &channels, 4);
    if (!pixels) {
        Logger::warn("The animated image could not be decoded: {}", stbi_failure_reason());
        return false;
    }

    const bool isUploaded = upload(pixels, width, height, frameCount, delays, device);
    stbi_image_free(pixels);
    stbi_image_free(delays);
    return isUploaded;
}

bool AnimatedImage::loadIcon(const unsigned char* data,
                             std::size_t size,
                             ID3D11Device& device)
{
    m_frames.clear();

    // The directory of the icon: the largest entry that is a PNG, which the big sizes of a modern icon are.
    const unsigned char* best = nullptr;
    std::size_t bestSize = 0;
    unsigned bestWidth = 0;
    const auto count = size >= iconHeaderSize ? readU16(data + 4) : 0;
    for (std::size_t i = 0; i < count; i++) {
        const auto* const entry = data + iconHeaderSize + i * iconEntrySize;
        if (static_cast<std::size_t>(entry - data) + iconEntrySize > size)
            break;

        const unsigned width = entry[0] ? entry[0] : 256;
        const auto entrySize = readU32(entry + 8);
        const auto offset = readU32(entry + 12);
        if (offset + entrySize > size || entrySize < sizeof(pngSignature)
            || std::memcmp(data + offset, pngSignature, sizeof(pngSignature)) != 0)
            continue;

        if (width > bestWidth) {
            bestWidth = width;
            best = data + offset;
            bestSize = entrySize;
        }
    }
    if (!best) {
        Logger::warn("The icon has no PNG entry");
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    auto* const pixels = stbi_load_from_memory(best, static_cast<int>(bestSize), &width, &height, &channels, 4);
    if (!pixels) {
        Logger::warn("The icon could not be decoded: {}", stbi_failure_reason());
        return false;
    }

    const bool isUploaded = upload(pixels, width, height, 1, nullptr, device);
    stbi_image_free(pixels);
    return isUploaded;
}

bool AnimatedImage::upload(const unsigned char* pixels,
                           int width,
                           int height,
                           int frameCount,
                           const int* delays,
                           ID3D11Device& device)
{
    const auto frameBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    int totalMs = 0;
    for (int i = 0; i < frameCount; i++) {
        D3D11_TEXTURE2D_DESC desc = { };
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial = { };
        initial.pSysMem = pixels + static_cast<std::size_t>(i) * frameBytes;
        initial.SysMemPitch = static_cast<UINT>(width) * 4;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device.CreateTexture2D(&desc, &initial, &texture))
            || FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            Logger::warn("The image could not be uploaded to the device");
            m_frames.clear();
            return false;
        }

        const int duration = delays && delays[i] > 0 ? delays[i] : defaultFrameMs;
        m_frames.push_back({ .view = view, .startMs = totalMs, .durationMs = duration });
        totalMs += duration;
    }

    m_width = width;
    m_height = height;
    m_totalMs = totalMs;
    m_start = std::chrono::steady_clock::now();
    return !m_frames.empty();
}

ImTextureID AnimatedImage::frame() const
{
    if (m_frames.empty())
        return 0;

    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start).count();
    const auto position = static_cast<int>(elapsed % std::max(m_totalMs, 1));
    const Frame* current = &m_frames.back();
    for (const auto& candidate : m_frames) {
        if (position < candidate.startMs + candidate.durationMs) {
            current = &candidate;
            break;
        }
    }

    return reinterpret_cast<ImTextureID>(current->view.Get()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

}
