#pragma once

#include <DirectXTex.h>

#include <array>
#include <cstddef>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgiformat.h>
#include <filesystem>
#include <minwindef.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <winnt.h>
#include <wrl/client.h>

class ParallaxGenD3D {
private:
    static constexpr unsigned NUM_GPU_THREADS = 16;
    static constexpr unsigned GPU_BUFFER_SIZE_MULTIPLE = 16;
    static constexpr unsigned MAX_CHANNEL_VALUE = 255;

    std::filesystem::path m_shaderPath;

    // GPU objects
    Microsoft::WRL::ComPtr<ID3D11Device> m_ptrDevice; // GPU device
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ptrContext; // GPU context

    static inline const D3D_FEATURE_LEVEL s_featureLevel = D3D_FEATURE_LEVEL_11_0; // DX11

    std::unordered_map<std::filesystem::path, DirectX::TexMetadata> m_ddsMetaDataCache;
    std::shared_mutex m_ddsMetaDataMutex;

    std::mutex m_gpuOperationMutex;

    // Global shader storage
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_shaderCountAlphaValues;

public:
    //
    // Static Helpers
    //

    /**
     * @brief Get the error message from a HRESULT
     *
     * @param hr HRESULT
     * @return std::string error message
     */
    static auto getHRESULTErrorMessage(HRESULT hr) -> std::string;

    /**
     * @brief Get the DXGI_FORMAT from a string
     *
     * @param format string representation of the DXGI_FORMAT
     * @return DXGI_FORMAT DXGI_FORMAT on success, DXGI_FORMAT_UNKNOWN on failure
     */
    static auto getDXGIFormatFromString(const std::string& format) -> DXGI_FORMAT;

    //
    // Instance Functions
    //

    /**
     * @brief Construct a new ParallaxGenD3D object
     *
     * @param pgd Pointer to the ParallaxGenDirectory object
     * @param shaderPath Path to shader folder
     */
    ParallaxGenD3D(std::filesystem::path shaderPath);

    /**
     * @brief Initialize GPU. This must be called before any other GPU functions
     *
     * @return true on success
     * @return false on failure
     */
    auto initGPU() -> bool;

    /**
     * @brief Initialize internal shaders
     *
     * @return true on success
     * @return false on failure
     */
    auto initShaders() -> bool;

    //
    // Global Runners (they use helpers below)
    //

    /**
     * @brief Apply a shader to a texture
     *
     * @param inTexture input texture
     * @param shader shader to apply
     * @param shaderParams shader parameters (const buffer)
     * @param[out] outTexture output texture
     * @return true on success
     * @return false on failure
     */
    auto applyShaderToTexture(const DirectX::ScratchImage& inTexture, DirectX::ScratchImage& outTexture,
        const Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader,
        const DXGI_FORMAT& outFormat = DXGI_FORMAT_R8G8B8A8_UNORM, const UINT& outWidth = 0, const UINT& outHeight = 0,
        const void* shaderParams = nullptr, const UINT& shaderParamsSize = 0) -> bool;

    auto checkIfCM(const std::filesystem::path& ddsPath, bool& result, bool& hasEnvMask, bool& hasGlosiness,
        bool& hasMetalness) -> bool;

    /**
     * @brief Count the number of alpha values in a texture
     *
     * @param image input image
     * @param outData output data
     * @return true on success
     * @return false on failure
     */
    auto countPixelValues(const DirectX::ScratchImage& image, std::array<int, 4>& outData) -> bool;

    //
    // GPU Helpers
    //

    /**
     * @brief Compiles shader into a D3D11 Shader blob
     *
     * @param filename Filename of shader relative to "shaders" folder
     * @param[out] outShader Output shader blob
     * @return true on success
     * @return false on failure
     */
    auto initShader(const std::filesystem::path& filename, Microsoft::WRL::ComPtr<ID3D11ComputeShader>& outShader) const
        -> bool;

    /**
     * @brief Create a Texture2D object on the GPU from a ScratchImage
     *
     * @param texture ScratchImage to create texture from
     * @param[out] dest Output texture
     * @return true on success
     * @return false on failure
     */
    auto createTexture2D(const DirectX::ScratchImage& texture, Microsoft::WRL::ComPtr<ID3D11Texture2D>& dest) const
        -> bool;

    /**
     * @brief Create a Texture2D object on the GPU from an existing Texture2D
     *
     * @param existingTexture Existing texture
     * @param[out] dest Output texture
     * @return true on success
     * @return false on failure
     */
    auto createTexture2D(Microsoft::WRL::ComPtr<ID3D11Texture2D>& existingTexture,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& dest) const -> bool;

    /**
     * @brief Create a Texture2D object from a description
     *
     * @param desc Texture description
     * @param[out] dest Output texture
     * @return true on success
     * @return false on failure
     */
    auto createTexture2D(D3D11_TEXTURE2D_DESC& desc, Microsoft::WRL::ComPtr<ID3D11Texture2D>& dest) const -> bool;

    /**
     * @brief Create a Shader Resource View for a Texture2D
     *
     * @param texture Texture2D object
     * @param[out] dest Output Shader Resource View
     * @return true on success
     * @return false on failure
     */
    auto createShaderResourceView(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& dest) const -> bool;

    /**
     * @brief Create a Unordered Access View object from a Texture2D
     *
     * @param texture Texture2D object
     * @param[out] dest Output Unordered Access View
     * @return true on success
     * @return false on failure
     */
    auto createUnorderedAccessView(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& dest) const -> bool;

    /**
     * @brief Create a Unordered Access View object from a generic resource and description
     *
     * @param gpuResource generic resource
     * @param desc description
     * @param[out] dest Output Unordered Access View
     * @return true on success
     * @return false on failure
     */
    auto createUnorderedAccessView(const Microsoft::WRL::ComPtr<ID3D11Resource>& gpuResource,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC& desc, Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& dest) const
        -> bool;

    /**
     * @brief Create a Buffer object
     *
     * @param data buffer data
     * @param desc buffer description
     * @param[out] dest Output buffer
     * @return true on success
     * @return false on failure
     */
    auto createBuffer(const void* data, D3D11_BUFFER_DESC& desc, Microsoft::WRL::ComPtr<ID3D11Buffer>& dest) const
        -> bool;

    /**
     * @brief Create a Constant Buffer object
     *
     * @param data constant buffer data
     * @param size size of the buffer
     * @param[out] dest Output constant buffer
     * @return true on success
     * @return false on failure
     */
    auto createConstantBuffer(const void* data, const UINT& size, Microsoft::WRL::ComPtr<ID3D11Buffer>& dest) const
        -> bool;

    /**
     * @brief dispatch a compute shader
     *
     * @param shader shader to dispatch
     * @param srvs shader resource views
     * @param uavs unordered access views
     * @param constantBuffers constant buffers
     * @param threadGroupCountX thread group count X
     * @param threadGroupCountY thread group count Y
     * @param threadGroupCountZ thread group count Z
     * @return true on success
     * @return false on failure
     */
    [[nodiscard]] auto blockingDispatch(const Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader,
        const std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>& srvs,
        const std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>>& uavs,
        const std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>& constantBuffers, UINT threadGroupCountX,
        UINT threadGroupCountY, UINT threadGroupCountZ) -> bool;

    /**
     * @brief read back a texture from the GPU
     *
     * @param gpuResource texture to read back
     * @param[out] outImage output image
     * @return true on success
     * @return false on failure
     */
    [[nodiscard]] auto readBack(
        const Microsoft::WRL::ComPtr<ID3D11Texture2D>& gpuResource, DirectX::ScratchImage& outImage) -> bool;

    /**
     * @brief read back a buffer from the GPU
     *
     * @tparam T data type
     * @param gpuResource buffer to read back
     * @param[out] outData output data
     * @return true on success
     * @return false on failure
     */
    template <typename T>
    [[nodiscard]] auto readBack(const Microsoft::WRL::ComPtr<ID3D11Buffer>& gpuResource, std::vector<T>& outData)
        -> bool;

    /**
     * @brief Copy a resource on the GPU
     *
     * @param src Source resource
     * @param dest Destination resource
     */
    void copyResource(
        const Microsoft::WRL::ComPtr<ID3D11Resource>& src, const Microsoft::WRL::ComPtr<ID3D11Resource>& dest);

    /**
     * @brief Generate mips for a shader resource view
     *
     * @param srv Shader resource view
     */
    void generateMips(const Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv);

    /**
     * @brief Flush GPU (clear any released variables)
     */
    void flushGPU();

    //
    // Texture helpers
    //

    /**
     * @brief Get the DDS image from a path
     *
     * @param ddsPath path of DDS file (relative to data)
     * @param dds output DDS image
     * @return true on success
     * @return false on failure
     */
    auto getDDS(const std::filesystem::path& ddsPath, DirectX::ScratchImage& dds) const -> bool;

    /**
     * @brief Get the DDS metadata from a path
     *
     * @param ddsPath path of DDS file (relative to data)
     * @param ddsMeta output DDS metadata
     * @return true on success
     * @return false on failure
     */
    auto getDDSMetadata(const std::filesystem::path& ddsPath, DirectX::TexMetadata& ddsMeta) -> bool;

    /**
     * @brief Check if aspect ratio between two textures matches
     *
     * @param ddsPath1 path of dds file 1
     * @param ddsPath2 path of dds file 2
     * @return true on success
     * @return false on failure
     */
    auto checkIfAspectRatioMatches(const std::filesystem::path& ddsPath1, const std::filesystem::path& ddsPath2)
        -> bool;

private:
    //
    // Private Helpers
    //
    static auto isPowerOfTwo(unsigned int x) -> bool;

    static auto loadRawPixelsToScratchImage(const std::vector<unsigned char>& rawPixels, const size_t& width,
        const size_t& height, const size_t& mips, DXGI_FORMAT format) -> DirectX::ScratchImage;
};
