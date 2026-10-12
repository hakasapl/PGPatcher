#include "PCH.h"

#include "Gif.hpp"

#include "util/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>

// stb_image decodes the GIF, with the frames composed and their delays in milliseconds; only its GIF part is built.
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include <stb_image.h>
#pragma warning(pop)

namespace PGLite {

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
            Logger::warn("The animated image could not be uploaded to the device");
            m_frames.clear();
            break;
        }

        const int duration = delays && delays[i] > 0 ? delays[i] : defaultFrameMs;
        m_frames.push_back({ .view = view, .startMs = totalMs, .durationMs = duration });
        totalMs += duration;
    }

    stbi_image_free(pixels);
    stbi_image_free(delays);

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
