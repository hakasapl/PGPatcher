#pragma once

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

namespace PGLite {

/**
 * @brief An image as Dear ImGui textures: one texture per frame, the frame chosen by the time since loading. A GIF
 * gives its frames and delays; an icon gives one frame.
 */
class AnimatedImage {
public:
    /**
     * @brief Decodes the GIF and uploads every frame to the device.
     *
     * @return false if the data is not a GIF the decoder handles or a texture could not be created (logged).
     */
    bool load(const unsigned char* data,
              std::size_t size,
              ID3D11Device& device);

    /**
     * @brief Decodes the largest PNG entry of a Windows icon (.ico) and uploads it as the one frame.
     *
     * @return false if the icon has no PNG entry the decoder handles or a texture could not be created (logged).
     */
    bool loadIcon(const unsigned char* data,
                  std::size_t size,
                  ID3D11Device& device);

    [[nodiscard]] bool isLoaded() const { return !m_frames.empty(); }

    /// @brief The size of a frame in pixels.
    [[nodiscard]] ImVec2 size() const { return { static_cast<float>(m_width), static_cast<float>(m_height) }; }

    /// @brief The texture of the frame that is due now, for ImGui::Image().
    [[nodiscard]] ImTextureID frame() const;

private:
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        int startMs = 0;
        int durationMs = 0;
    };

    /// @brief GIF frames without a delay are shown this long.
    static constexpr int defaultFrameMs = 100;

    /// @brief Uploads RGBA frames stored one after the other; delays may be null for a still image.
    bool upload(const unsigned char* pixels,
                int width,
                int height,
                int frameCount,
                const int* delays,
                ID3D11Device& device);

    std::vector<Frame> m_frames;
    int m_width = 0;
    int m_height = 0;
    int m_totalMs = 0;
    std::chrono::steady_clock::time_point m_start;
};

}
