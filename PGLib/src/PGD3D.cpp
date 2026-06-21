#include "PGD3D.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "util/Logger.hpp"

#include <DirectXMath.h>
#include <DirectXTex.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <dxgiformat.h>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <comdef.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>
#endif

using namespace std;
using namespace StringUtil;
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
#endif

#ifdef _WIN32
// We need to access unions as part of certain DX11 structures
// reinterpret cast is needed often for type casting with DX11
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
#endif

PGD3D::PGD3D(filesystem::path shaderPath)
    : m_shaderPath(std::move(shaderPath))
{
}

auto PGD3D::checkIfCM(const filesystem::path& ddsPath,
                      bool& result,
                      bool& hasEnvMask,
                      bool& hasGlosiness,
                      bool& hasMetalness) -> bool
{
    // get metadata (should only pull headers, which is much faster)
    DirectX::TexMetadata ddsImageMeta {};
    if (!getDDSMetadata(ddsPath, ddsImageMeta)) {
        result = false;
        return false;
    }

    // If Alpha is opaque move on
    if (ddsImageMeta.GetAlphaMode() == DirectX::TEX_ALPHA_MODE_OPAQUE) {
        result = false;
        return true;
    }

    // bool bcCompressed = false;
    //  Only check DDS with alpha channels
    switch (ddsImageMeta.format) {
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
    case DXGI_FORMAT_BC7_TYPELESS:
        // bcCompressed = true;
        [[fallthrough]];
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        break;
    default:
        result = false;
        return true;
    }

    // Read image
    DirectX::ScratchImage image;
    if (!getDDS(ddsPath, image)) {
        result = false;
        return false;
    }

    array<int, 4> values {};
    if (!countPixelValues(image, values)) {
        result = false;
        return false;
    }

    const size_t numPixels = ddsImageMeta.width * ddsImageMeta.height;
    if (values[3] > numPixels / 2) {
        // check alpha
        result = false;
        return true;
    }

    if (values[0] > 0) {
        hasEnvMask = true;
    }

    if (values[1] > 0) {
        // check green
        hasGlosiness = true;
    }

    if (values[2] > 0) {
        hasMetalness = true;
    }

    result = true;
    return true;
}

auto PGD3D::countPixelValues(const DirectX::ScratchImage& image,
                             array<int,
                                   4>& outData) -> bool
{
#ifdef _WIN32
    if ((m_ptrContext == nullptr) || (m_ptrDevice == nullptr) || (m_shaderCountAlphaValues == nullptr)) {
        throw runtime_error("GPU not initialized");
    }

    // Create GPU texture
    ComPtr<ID3D11Texture2D> inputTex;
    if (!createTexture2D(image, inputTex)) {
        return false;
    }

    // Create SRV
    ComPtr<ID3D11ShaderResourceView> inputSRV;
    if (!createShaderResourceView(inputTex, inputSRV)) {
        return false;
    }

    // Create buffer for output
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(UINT) * 4;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(UINT);

    array<unsigned int, 4> outputData = {0, 0, 0, 0};
    ComPtr<ID3D11Buffer> outputBuffer;
    if (!createBuffer(outputData.data(), bufferDesc, outputBuffer)) {
        return false;
    }

    // Create UAV for output buffer
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 4;

    ComPtr<ID3D11UnorderedAccessView> outputBufferUAV;
    if (!createUnorderedAccessView(outputBuffer, uavDesc, outputBufferUAV)) {
        return false;
    }

    // Dispatch shader
    const DirectX::TexMetadata imageMeta = image.GetMetadata();
    if (!blockingDispatch(m_shaderCountAlphaValues,
                          {inputSRV},
                          {outputBufferUAV},
                          {},
                          static_cast<UINT>(imageMeta.width),
                          static_cast<UINT>(imageMeta.height),
                          1)) {
        return false;
    }

    // Clean Up Objects
    inputTex.Reset();
    inputSRV.Reset();

    // Read back data

    vector<array<int, 4>> data;
    if (!readBack<array<int, 4>>(outputBuffer, data)) {
        return false;
    }

    // Cleanup
    outputBuffer.Reset();
    outputBufferUAV.Reset();

    m_ptrContext->Flush(); // Flush GPU to avoid leaks

    outData = data[0];
    return true;
#else
    // Linux CPU fallback: replicate CountAlphaValues.hlsl logic
    // Decompress to RGBA8 if needed
    DirectX::ScratchImage rgbaImage;
    const DirectX::TexMetadata& meta = image.GetMetadata();

    bool needsDecompress = DirectX::IsCompressed(meta.format);
    const DirectX::ScratchImage* src = &image;
    if (needsDecompress) {
        HRESULT hr = DirectX::Decompress(image.GetImages(), image.GetImageCount(), meta,
                                         DXGI_FORMAT_R8G8B8A8_UNORM, rgbaImage);
        if (FAILED(hr)) {
            return false;
        }
        src = &rgbaImage;
    } else if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        HRESULT hr = DirectX::Convert(image.GetImages(), image.GetImageCount(), meta,
                                      DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                      DirectX::TEX_THRESHOLD_DEFAULT, rgbaImage);
        if (FAILED(hr)) {
            return false;
        }
        src = &rgbaImage;
    }

    array<int, 4> counts = {0, 0, 0, 0};

    // Only process the first mip (mip 0, array 0)
    const DirectX::Image* img = src->GetImage(0, 0, 0);
    if (img == nullptr) {
        return false;
    }

    const size_t pixelCount = img->width * img->height;
    const uint8_t* pixels = img->pixels;
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t r = pixels[i * 4 + 0];
        const uint8_t g = pixels[i * 4 + 1];
        const uint8_t b = pixels[i * 4 + 2];
        const uint8_t a = pixels[i * 4 + 3];
        if (r >= 4) { ++counts[0]; }
        if (g >= 4) { ++counts[1]; }
        if (b >= 4) { ++counts[2]; }
        if (a > 254) { ++counts[3]; }
    }

    outData = counts;
    return true;
#endif
}

auto PGD3D::checkIfAspectRatioMatches(const std::filesystem::path& ddsPath1,
                                      const std::filesystem::path& ddsPath2) -> bool
{
    // get metadata (should only pull headers, which is much faster)
    DirectX::TexMetadata ddsImageMeta1 {};
    if (!getDDSMetadata(ddsPath1, ddsImageMeta1)) {
        Logger::error(L"Unable to process texture: {}", ddsPath1.wstring());
        return false;
    }

    DirectX::TexMetadata ddsImageMeta2 {};
    if (!getDDSMetadata(ddsPath2, ddsImageMeta2)) {
        Logger::error(L"Unable to process texture: {}", ddsPath2.wstring());
        return false;
    }

    // Validate dimensions before calculating aspect ratios
    if (ddsImageMeta1.height == 0 || ddsImageMeta2.height == 0) {
        if (ddsImageMeta1.height == 0) {
            Logger::error(L"Unable to process texture: {}", ddsPath1.wstring());
        }
        if (ddsImageMeta2.height == 0) {
            Logger::error(L"Unable to process texture: {}", ddsPath2.wstring());
        }
        return false;
    }

    // calculate aspect ratios
    const float aspectRatio1 = static_cast<float>(ddsImageMeta1.width) / static_cast<float>(ddsImageMeta1.height);
    const float aspectRatio2 = static_cast<float>(ddsImageMeta2.width) / static_cast<float>(ddsImageMeta2.height);

    // check if aspect ratios don't match
    return aspectRatio1 == aspectRatio2;
}

//
// GPU Code
//

auto PGD3D::initGPU() -> bool
{
#ifdef _WIN32
    const std::scoped_lock lock(m_d3dMutex);

// initialize GPU device and context
#ifdef _DEBUG
    UINT deviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
    const UINT deviceFlags = 0;
#endif

    HRESULT hr {};
    hr = D3D11CreateDevice(nullptr, // Adapter (configured to use default)
                           D3D_DRIVER_TYPE_HARDWARE, // User Hardware
                           nullptr, // Irrelevant for hardware driver type
                           deviceFlags, // Device flags (enables debug if needed)
                           &s_featureLevel, // Feature levels this app can support
                           1, // Just 1 feature level (D3D11)
                           D3D11_SDK_VERSION, // Set to D3D11 SDK version
                           &m_ptrDevice, // Sets the instance device
                           nullptr, // Returns feature level of device created (not needed)
                           &m_ptrContext // Sets the instance device immediate context
    );

    return !FAILED(hr);
#else
    // No GPU needed on Linux - CPU fallback is used
    return true;
#endif
}

auto PGD3D::initShaders() -> bool
{
#ifdef _WIN32
    // Initialize shaders
    return initShader("CountAlphaValues.hlsl", m_shaderCountAlphaValues);
#else
    return true;
#endif
}

auto PGD3D::initShader(const std::filesystem::path& filename,
                       ShaderHandle& outShader) -> bool
{
#ifdef _WIN32
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Load shader
    ComPtr<ID3DBlob> shaderBlob;
    HRESULT hr {};
#ifdef _DEBUG
    const DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG;
#else
    const DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#endif

    const filesystem::path shaderAbsPath = m_shaderPath / filename;

    // Compile shader
    ComPtr<ID3DBlob> ptrErrorBlob;
    hr = D3DCompileFromFile(shaderAbsPath.c_str(),
                            nullptr,
                            nullptr,
                            "main",
                            "cs_5_0",
                            dwShaderFlags,
                            0,
                            shaderBlob.ReleaseAndGetAddressOf(),
                            ptrErrorBlob.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // Create compute shader on GPU
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateComputeShader(
            shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, outShader.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
#else
    // On Linux: store the shader filename as the handle for CPU dispatch
    outShader = filename.filename().string();
    return true;
#endif
}

//
// GPU Helpers
//
auto PGD3D::isPowerOfTwo(unsigned int x) -> bool { return (x != 0U) && ((x & (x - 1)) == 0U); }

#ifdef _WIN32
auto PGD3D::createTexture2D(const DirectX::ScratchImage& texture,
                            ComPtr<ID3D11Texture2D>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Verify dimention
    auto textureMeta = texture.GetMetadata();
    if (!isPowerOfTwo(static_cast<unsigned int>(textureMeta.width))
        || !isPowerOfTwo(static_cast<unsigned int>(textureMeta.height))) {
        return false;
    }

    // Create texture
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = DirectX::CreateTexture(m_ptrDevice.Get(),
                                    texture.GetImages(),
                                    texture.GetImageCount(),
                                    texture.GetMetadata(),
                                    reinterpret_cast<ID3D11Resource**>(dest.ReleaseAndGetAddressOf()));
    }

    return !FAILED(hr);
}

auto PGD3D::createTexture2D(ComPtr<ID3D11Texture2D>& existingTexture,
                            ComPtr<ID3D11Texture2D>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Smart Pointer to hold texture for output
    D3D11_TEXTURE2D_DESC textureOutDesc;
    existingTexture->GetDesc(&textureOutDesc);
    if (textureOutDesc.Width % 2 != 0 || textureOutDesc.Height % 2 != 0) {
        return false;
    }

    HRESULT hr {};

    // Create texture
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateTexture2D(&textureOutDesc, nullptr, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

auto PGD3D::createTexture2D(D3D11_TEXTURE2D_DESC& desc,
                            ComPtr<ID3D11Texture2D>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Create texture
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateTexture2D(&desc, nullptr, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

auto PGD3D::createShaderResourceView(const ComPtr<ID3D11Texture2D>& texture,
                                     ComPtr<ID3D11ShaderResourceView>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Create SRV for texture
    D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc = {};
    shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shaderDesc.Texture2D.MipLevels = -1;

    // Create SRV
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateShaderResourceView(texture.Get(), &shaderDesc, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

auto PGD3D::createUnorderedAccessView(const ComPtr<ID3D11Texture2D>& texture,
                                      ComPtr<ID3D11UnorderedAccessView>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Create UAV for texture
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    // Create UAV
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateUnorderedAccessView(texture.Get(), &uavDesc, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

auto PGD3D::createUnorderedAccessView(const ComPtr<ID3D11Resource>& gpuResource,
                                      const D3D11_UNORDERED_ACCESS_VIEW_DESC& desc,
                                      ComPtr<ID3D11UnorderedAccessView>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    HRESULT hr {};

    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateUnorderedAccessView(gpuResource.Get(), &desc, &dest);
    }
    return !FAILED(hr);
}

auto PGD3D::createBuffer(const void* data,
                         D3D11_BUFFER_DESC& desc,
                         ComPtr<ID3D11Buffer>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    HRESULT hr {};
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateBuffer(&desc, &initData, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

auto PGD3D::createConstantBuffer(const void* data,
                                 const UINT& size,
                                 ComPtr<ID3D11Buffer>& dest) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Create buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = (size + GPU_BUFFER_SIZE_MULTIPLE) - (size % GPU_BUFFER_SIZE_MULTIPLE);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    // Create init data
    D3D11_SUBRESOURCE_DATA cbInitData;
    cbInitData.pSysMem = data;
    cbInitData.SysMemPitch = 0;
    cbInitData.SysMemSlicePitch = 0;

    // Create buffer
    {
        const std::scoped_lock lock(m_d3dMutex);
        hr = m_ptrDevice->CreateBuffer(&cbDesc, &cbInitData, dest.ReleaseAndGetAddressOf());
    }
    return !FAILED(hr);
}

void PGD3D::copyResource(const ComPtr<ID3D11Resource>& src,
                         const ComPtr<ID3D11Resource>& dest)
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const std::scoped_lock lock(m_d3dMutex);
    m_ptrContext->CopyResource(dest.Get(), src.Get());
}

void PGD3D::generateMips(const ComPtr<ID3D11ShaderResourceView>& srv)
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const std::scoped_lock lock(m_d3dMutex);
    m_ptrContext->GenerateMips(srv.Get());
}

void PGD3D::flushGPU()
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const std::scoped_lock lock(m_d3dMutex);
    m_ptrContext->Flush();
}

auto PGD3D::blockingDispatch(const Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader,
                             const std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>& srvs,
                             const std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>>& uavs,
                             const std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>& constantBuffers,
                             UINT threadGroupCountX,
                             UINT threadGroupCountY,
                             UINT threadGroupCountZ) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const std::scoped_lock lock(m_d3dMutex);

    m_ptrContext->CSSetShader(shader.Get(), nullptr, 0);
    for (UINT i = 0; i < srvs.size(); i++) {
        m_ptrContext->CSSetShaderResources(i, 1, srvs[i].GetAddressOf());
    }
    for (UINT i = 0; i < uavs.size(); i++) {
        m_ptrContext->CSSetUnorderedAccessViews(i, 1, uavs[i].GetAddressOf(), nullptr);
    }
    for (UINT i = 0; i < constantBuffers.size(); i++) {
        m_ptrContext->CSSetConstantBuffers(i, 1, constantBuffers[i].GetAddressOf());
    }

    // Error object
    HRESULT hr {};

    // query
    ComPtr<ID3D11Query> ptrQuery = nullptr;
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    hr = m_ptrDevice->CreateQuery(&queryDesc, ptrQuery.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // Run the shader
    m_ptrContext->Dispatch((threadGroupCountX + NUM_GPU_THREADS - 1) / NUM_GPU_THREADS,
                           (threadGroupCountY + NUM_GPU_THREADS - 1) / NUM_GPU_THREADS,
                           (threadGroupCountZ + NUM_GPU_THREADS - 1) / NUM_GPU_THREADS);

    // end query
    m_ptrContext->End(ptrQuery.Get());

    // wait for shader to complete
    BOOL queryData = FALSE;
    hr = m_ptrContext->GetData(ptrQuery.Get(),
                               &queryData,
                               sizeof(queryData),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH); // block until complete
    if (FAILED(hr)) {
        return false;
    }
    ptrQuery.Reset();

    // Clean up
    static const array<ID3D11ShaderResourceView*, 1> nullSRV = {nullptr};
    static const array<ID3D11UnorderedAccessView*, 1> nullUAV = {nullptr};
    static const array<ID3D11Buffer*, 1> nullBuffer = {nullptr};

    m_ptrContext->CSSetShader(nullptr, nullptr, 0);
    for (UINT i = 0; i < srvs.size(); i++) {
        m_ptrContext->CSSetShaderResources(i, 1, nullSRV.data());
    }
    for (UINT i = 0; i < uavs.size(); i++) {
        m_ptrContext->CSSetUnorderedAccessViews(i, 1, nullUAV.data(), nullptr);
    }
    for (UINT i = 0; i < constantBuffers.size(); i++) {
        m_ptrContext->CSSetConstantBuffers(i, 1, nullBuffer.data());
    }

    // return success
    return true;
}

auto PGD3D::readBack(const ComPtr<ID3D11Texture2D>& gpuResource,
                     DirectX::ScratchImage& outImage) -> bool
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    // Error object
    HRESULT hr {};

    // Grab texture description
    D3D11_TEXTURE2D_DESC stagingTex2DDesc;
    gpuResource->GetDesc(&stagingTex2DDesc);
    const UINT mipLevels = stagingTex2DDesc.MipLevels; // Number of mip levels to read back

    // Enable flags for CPU access
    stagingTex2DDesc.Usage = D3D11_USAGE_STAGING;
    stagingTex2DDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingTex2DDesc.BindFlags = 0;
    stagingTex2DDesc.MiscFlags = 0;

    // Find bytes per pixel based on format
    UINT bytesPerChannel = 1; // Default to 8-bit
    UINT numChannels = 4; // Default to RGBA
    switch (stagingTex2DDesc.Format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        bytesPerChannel = 4; // 32 bits per channel
        numChannels = 4; // RGBA
        break;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        bytesPerChannel = 4; // 32 bits per channel
        numChannels = 3; // RGB
        break;
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        bytesPerChannel = 4; // 32 bits per channel
        numChannels = 2; // RG
        break;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
        bytesPerChannel = 4; // 32 bits per channel
        numChannels = 1; // R
        break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        bytesPerChannel = 2; // 16 bits per channel
        numChannels = 4; // RGBA
        break;
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
        bytesPerChannel = 2; // 16 bits per channel
        numChannels = 2; // RG
        break;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        bytesPerChannel = 2; // 16 bits per channel
        numChannels = 1; // R
        break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        bytesPerChannel = 1; // 8 bits per channel
        numChannels = 4; // RGBA
        break;
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
        bytesPerChannel = 1; // 8 bits per channel
        numChannels = 2; // RG
        break;
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
        bytesPerChannel = 1; // 8 bits per channel
        numChannels = 1; // R
        break;
    // Add more cases for other formats if needed
    default:
        bytesPerChannel = 1; // Assume 8-bit for unsupported formats
        numChannels = 4; // Assume RGBA for unsupported formats
    }
    const UINT bytesPerPixel = bytesPerChannel * numChannels;

    // Create staging texture
    ComPtr<ID3D11Texture2D> stagingTex2D;
    if (!createTexture2D(stagingTex2DDesc, stagingTex2D)) {
        return false;
    }

    // Copy resource to staging texture
    copyResource(gpuResource, stagingTex2D);

    std::vector<unsigned char> outputData;

    // Iterate over each mip level and read back
    {
        const std::scoped_lock lock(m_d3dMutex);

        for (UINT mipLevel = 0; mipLevel < mipLevels; ++mipLevel) {
            // Get dimensions for the current mip level
            const UINT mipWidth = std::max(1U, stagingTex2DDesc.Width >> mipLevel);
            const UINT mipHeight = std::max(1U, stagingTex2DDesc.Height >> mipLevel);

            // Map the mip level to the CPU
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = m_ptrContext->Map(stagingTex2D.Get(), mipLevel, D3D11_MAP_READ, 0, &mappedResource);

            if (FAILED(hr)) {
                return false;
            }

            // Copy the data from the mapped resource to the output vector
            auto* srcData = reinterpret_cast<unsigned char*>(mappedResource.pData);
            for (UINT row = 0; row < mipHeight; ++row) {
                unsigned char* rowStart = srcData + (row * mappedResource.RowPitch);
                outputData.insert(outputData.end(), rowStart, rowStart + (mipWidth * bytesPerPixel));
            }

            // Unmap the resource for this mip level
            m_ptrContext->Unmap(stagingTex2D.Get(), mipLevel);
        }
    }

    stagingTex2D.Reset();

    // Import into directx scratchimage
    outImage = loadRawPixelsToScratchImage(
        outputData, stagingTex2DDesc.Width, stagingTex2DDesc.Height, mipLevels, stagingTex2DDesc.Format);

    return true;
}

template <typename T>
auto PGD3D::readBack(const ComPtr<ID3D11Buffer>& gpuResource,
                     vector<T>& outData) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("Device not initialized");
    }

    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    // Error object
    HRESULT hr {};

    // grab edge detection results
    D3D11_BUFFER_DESC bufferDesc;
    gpuResource->GetDesc(&bufferDesc);
    // Enable flags for CPU access
    bufferDesc.Usage = D3D11_USAGE_STAGING;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bufferDesc.BindFlags = 0;
    bufferDesc.MiscFlags = 0;
    bufferDesc.StructureByteStride = 0;

    // Create the staging buffer
    ComPtr<ID3D11Buffer> stagingBuffer;

    hr = m_ptrDevice->CreateBuffer(&bufferDesc, nullptr, stagingBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // copy resource
    copyResource(gpuResource, stagingBuffer);

    // map resource to CPU
    {
        const std::scoped_lock lock(m_d3dMutex);

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = m_ptrContext->Map(stagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            return false;
        }

        // Access the texture data from MappedResource.pData
        const size_t numElements = bufferDesc.ByteWidth / sizeof(T);
        vector<T> outputData(reinterpret_cast<T*>(mappedResource.pData),
                             reinterpret_cast<T*>(mappedResource.pData) + numElements);
        outData = std::move(outputData);

        // Cleaup
        m_ptrContext->Unmap(stagingBuffer.Get(), 0); // cleanup map
    }

    stagingBuffer.Reset();

    return true;
}

#endif // _WIN32

//
// Texture Helpers
//

auto PGD3D::getDDS(const filesystem::path& ddsPath, // NOLINT(readability-convert-member-functions-to-static)
                   DirectX::ScratchImage& dds) const -> bool
{
    auto* const pgd = PGGlobals::getPGD();

    HRESULT hr {};

    if (pgd->isLooseFile(ddsPath)) {
        const filesystem::path fullPath = pgd->getLooseFileFullPath(ddsPath);

        // Load DDS file
        hr = DirectX::LoadFromDDSFile(fullPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, dds);
    } else if (pgd->isBSAFile(ddsPath)) {
        vector<std::byte> ddsBytes;
        try {
            ddsBytes = pgd->getFile(ddsPath);
        } catch (...) {
            Logger::error(L"Failed to read DDS file from BSA: {}", ddsPath.wstring());
            return false;
        }

        // Load DDS file
        hr = DirectX::LoadFromDDSMemory(ddsBytes.data(), ddsBytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, dds);
    } else {
        return false;
    }

    if (FAILED(hr)) {
        return false;
    }

    return true;
}

auto PGD3D::getDDSMetadata(const filesystem::path& ddsPath,
                           DirectX::TexMetadata& ddsMeta) -> bool
{
    auto* const pgd = PGGlobals::getPGD();

    {
        const shared_lock lock(m_ddsMetaDataMutex);
        // TODO set cache to something on failure
        if (m_ddsMetaDataCache.contains(ddsPath)) {
            ddsMeta = m_ddsMetaDataCache.at(ddsPath);
            return true;
        }
    }

    HRESULT hr {};

    if (pgd->isLooseFile(ddsPath)) {
        const filesystem::path fullPath = pgd->getLooseFileFullPath(ddsPath);

        // Load DDS file
        hr = DirectX::GetMetadataFromDDSFile(fullPath.c_str(), DirectX::DDS_FLAGS_NONE, ddsMeta);
    } else if (pgd->isBSAFile(ddsPath)) {
        vector<std::byte> ddsBytes;
        try {
            ddsBytes = pgd->getFile(ddsPath);
        } catch (...) {
            Logger::error(L"Failed to read DDS file from BSA: {}", ddsPath.wstring());
            return false;
        }

        // Load DDS file
        hr = DirectX::GetMetadataFromDDSMemory(ddsBytes.data(), ddsBytes.size(), DirectX::DDS_FLAGS_NONE, ddsMeta);
    } else {
        return false;
    }

    if (FAILED(hr)) {
        return false;
    }

    // update cache
    {
        const unique_lock lock(m_ddsMetaDataMutex);
        if (!m_ddsMetaDataCache.contains(ddsPath)) {
            m_ddsMetaDataCache[ddsPath] = ddsMeta;
        }
    }

    return true;
}

auto PGD3D::applyShaderToTexture(const DirectX::ScratchImage& inTexture,
                                 DirectX::ScratchImage& outTexture,
                                 const ShaderHandle& shader,
                                 const DXGI_FORMAT& outFormat,
                                 unsigned int outWidth,
                                 unsigned int outHeight,
                                 const void* shaderParams,
                                 unsigned int shaderParamsSize) -> bool
{
#ifdef _WIN32
    if (shader == nullptr) {
        throw runtime_error("Shader was not initialized");
    }

    if (inTexture.GetImageCount() < 1) {
        return false;
    }

    const DirectX::TexMetadata inputMeta = inTexture.GetMetadata();

    // Load texture on GPU
    ComPtr<ID3D11Texture2D> inputDDSGPU;
    if (!createTexture2D(inTexture, inputDDSGPU)) {
        return false;
    }

    // Create shader resource view
    ComPtr<ID3D11ShaderResourceView> inputDDSSRV;
    if (!createShaderResourceView(inputDDSGPU, inputDDSSRV)) {
        return false;
    }

    // Create constant parameter buffer
    ComPtr<ID3D11Buffer> constantBuffer;
    if (shaderParams != nullptr && !createConstantBuffer(shaderParams, shaderParamsSize, constantBuffer)) {
        return false;
    }

    // Create output texture
    D3D11_TEXTURE2D_DESC outputDDSDesc = {};
    inputDDSGPU->GetDesc(&outputDDSDesc);
    if (outWidth > 0) {
        outputDDSDesc.Width = outWidth;
    }
    if (outHeight > 0) {
        outputDDSDesc.Height = outHeight;
    }
    outputDDSDesc.Format = outFormat;
    outputDDSDesc.MipLevels = 0; // Generate full mip chain
    outputDDSDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> outputDDSGPU;
    if (!createTexture2D(outputDDSDesc, outputDDSGPU)) {
        return false;
    }

    ComPtr<ID3D11UnorderedAccessView> outputDDSUAV;
    if (!createUnorderedAccessView(outputDDSGPU, outputDDSUAV)) {
        return false;
    }

    // Dispatch shader
    vector<ComPtr<ID3D11Buffer>> constantBuffers;
    if (shaderParams != nullptr) {
        constantBuffers.push_back(constantBuffer);
    }

    if (!blockingDispatch(shader,
                          {inputDDSSRV},
                          {outputDDSUAV},
                          constantBuffers,
                          static_cast<UINT>(inputMeta.width),
                          static_cast<UINT>(inputMeta.height),
                          1)) {
        return false;
    }

    // Release some objects
    inputDDSGPU.Reset();
    inputDDSSRV.Reset();
    constantBuffer.Reset();

    // Generate mips
    D3D11_TEXTURE2D_DESC outputDDSMipsDesc = {};
    outputDDSGPU->GetDesc(&outputDDSMipsDesc);
    outputDDSMipsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    outputDDSMipsDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> outputDDSMipsGPU;
    if (!createTexture2D(outputDDSMipsDesc, outputDDSMipsGPU)) {
        return false;
    }

    ComPtr<ID3D11ShaderResourceView> outputMipsSRV;
    if (!createShaderResourceView(outputDDSMipsGPU, outputMipsSRV)) {
        return false;
    }

    // Copy texture
    copyResource(outputDDSGPU.Get(), outputDDSMipsGPU.Get());
    generateMips(outputMipsSRV.Get());
    copyResource(outputDDSMipsGPU.Get(), outputDDSGPU.Get());

    // cleanup
    outputDDSMipsGPU.Reset();
    outputMipsSRV.Reset();

    // Read back texture
    if (!readBack(outputDDSGPU, outTexture)) {
        return false;
    }

    // More cleaning
    outputDDSGPU.Reset();
    outputDDSUAV.Reset();

    // Flush GPU to avoid leaks
    flushGPU();

    return true;
#else
    // Linux CPU fallback: dispatch per-shader CPU implementation
    if (shader.empty()) {
        throw runtime_error("Shader was not initialized");
    }

    if (inTexture.GetImageCount() < 1) {
        return false;
    }

    const DirectX::TexMetadata inputMeta = inTexture.GetMetadata();

    // Decompress input to RGBA32F for processing
    DirectX::ScratchImage decompressed;
    const DirectX::ScratchImage* src = &inTexture;
    if (DirectX::IsCompressed(inputMeta.format)) {
        HRESULT hr = DirectX::Decompress(inTexture.GetImages(), inTexture.GetImageCount(),
                                         inputMeta, DXGI_FORMAT_R32G32B32A32_FLOAT, decompressed);
        if (FAILED(hr)) { return false; }
        src = &decompressed;
    } else if (inputMeta.format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
        HRESULT hr = DirectX::Convert(inTexture.GetImages(), inTexture.GetImageCount(),
                                      inputMeta, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                      DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
                                      decompressed);
        if (FAILED(hr)) { return false; }
        src = &decompressed;
    }

    const DirectX::Image* srcImg = src->GetImage(0, 0, 0);
    if (srcImg == nullptr) { return false; }

    const size_t srcW = srcImg->width;
    const size_t srcH = srcImg->height;

    // Determine output dimensions
    const size_t dstW = (outWidth  > 0) ? outWidth  : srcW;
    const size_t dstH = (outHeight > 0) ? outHeight : srcH;

    // Allocate output image
    DirectX::ScratchImage processedImage;
    HRESULT hr = processedImage.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, dstW, dstH, 1, 1);
    if (FAILED(hr)) { return false; }

    DirectX::Image* dstImg = processedImage.GetImage(0, 0, 0);
    if (dstImg == nullptr) { return false; }

    const auto* srcPixels = reinterpret_cast<const float*>(srcImg->pixels);
    auto* dstPixels       = reinterpret_cast<float*>(dstImg->pixels);

    if (shader == "ParallaxToCM.hlsl") {
        // Copy red channel to alpha, zero RGB
        for (size_t y = 0; y < dstH; ++y) {
            for (size_t x = 0; x < dstW; ++x) {
                const size_t si = (y * srcW + x) * 4;
                const size_t di = (y * dstW + x) * 4;
                dstPixels[di + 0] = 0.0F;
                dstPixels[di + 1] = 0.0F;
                dstPixels[di + 2] = 0.0F;
                dstPixels[di + 3] = srcPixels[si + 0]; // red -> alpha
            }
        }
    } else if (shader == "ConvertToHDR.hlsl") {
        // Multiply RGB by luminance multiplier (from shaderParams)
        float luminanceMult = 1.0F;
        if (shaderParams != nullptr && shaderParamsSize >= sizeof(float)) {
            memcpy(&luminanceMult, shaderParams, sizeof(float));
        }
        for (size_t y = 0; y < dstH; ++y) {
            for (size_t x = 0; x < dstW; ++x) {
                const size_t i = (y * dstW + x) * 4;
                dstPixels[i + 0] = srcPixels[i + 0] * luminanceMult;
                dstPixels[i + 1] = srcPixels[i + 1] * luminanceMult;
                dstPixels[i + 2] = srcPixels[i + 2] * luminanceMult;
                dstPixels[i + 3] = srcPixels[i + 3];
            }
        }
    } else if (shader == "SSSFix.hlsl") {
        // 2x downscale + saturation power on RGB
        struct SSSParams { float fAlbedoSatPower; float fAlbedoNorm; };
        SSSParams params {0.5F, 1.8F};
        if (shaderParams != nullptr && shaderParamsSize >= sizeof(SSSParams)) {
            memcpy(&params, shaderParams, sizeof(SSSParams));
        }
        constexpr int scaleFactor = 2;
        for (size_t y = 0; y < dstH; ++y) {
            for (size_t x = 0; x < dstW; ++x) {
                // Average scaleFactor x scaleFactor block
                float r = 0, g = 0, b = 0, a = 0;
                for (int dy = 0; dy < scaleFactor; ++dy) {
                    for (int dx = 0; dx < scaleFactor; ++dx) {
                        const size_t sy = y * scaleFactor + dy;
                        const size_t sx = x * scaleFactor + dx;
                        const size_t si = (sy * srcW + sx) * 4;
                        r += srcPixels[si + 0];
                        g += srcPixels[si + 1];
                        b += srcPixels[si + 2];
                        a += srcPixels[si + 3];
                    }
                }
                const float n = static_cast<float>(scaleFactor * scaleFactor);
                r /= n; g /= n; b /= n; a /= n;

                // Saturation power: albedo = pow(max(0.001, color), satPower * length(color))
                const float len = std::sqrt(r*r + g*g + b*b);
                const float exp = params.fAlbedoSatPower * len;
                float ar = std::pow(std::max(0.001F, r), exp);
                float ag = std::pow(std::max(0.001F, g), exp);
                float ab = std::pow(std::max(0.001F, b), exp);

                // lerp(albedo, normalize(albedo), norm), saturate
                const float alen = std::sqrt(ar*ar + ag*ag + ab*ab);
                if (alen > 0.0F) {
                    ar = std::min(1.0F, ar + (ar/alen - ar) * params.fAlbedoNorm);
                    ag = std::min(1.0F, ag + (ag/alen - ag) * params.fAlbedoNorm);
                    ab = std::min(1.0F, ab + (ab/alen - ab) * params.fAlbedoNorm);
                }
                ar = std::max(0.0F, ar);
                ag = std::max(0.0F, ag);
                ab = std::max(0.0F, ab);

                const size_t di = (y * dstW + x) * 4;
                dstPixels[di + 0] = ar;
                dstPixels[di + 1] = ag;
                dstPixels[di + 2] = ab;
                dstPixels[di + 3] = a;
            }
        }
    } else {
        // Unknown shader - identity copy
        for (size_t y = 0; y < dstH; ++y) {
            for (size_t x = 0; x < dstW; ++x) {
                const size_t i = (y * dstW + x) * 4;
                dstPixels[i + 0] = srcPixels[i + 0];
                dstPixels[i + 1] = srcPixels[i + 1];
                dstPixels[i + 2] = srcPixels[i + 2];
                dstPixels[i + 3] = srcPixels[i + 3];
            }
        }
    }

    // Convert to the requested output format
    if (outFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) {
        outTexture = std::move(processedImage);
    } else {
        hr = DirectX::Convert(processedImage.GetImages(), processedImage.GetImageCount(),
                              processedImage.GetMetadata(), outFormat,
                              DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
                              outTexture);
        if (FAILED(hr)) { return false; }
    }

    // Generate mip chain
    DirectX::ScratchImage mippedImage;
    hr = DirectX::GenerateMipMaps(outTexture.GetImages(), outTexture.GetImageCount(),
                                  outTexture.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mippedImage);
    if (SUCCEEDED(hr)) {
        outTexture = std::move(mippedImage);
    }

    return true;
#endif
}

auto PGD3D::loadRawPixelsToScratchImage(const vector<unsigned char>& rawPixels,
                                        const size_t& width,
                                        const size_t& height,
                                        const size_t& mips,
                                        DXGI_FORMAT format) -> DirectX::ScratchImage
{
    // Initialize a ScratchImage
    DirectX::ScratchImage image;
    const HRESULT hr = image.Initialize2D(format, width, height, 1,
                                          mips); // 1 array slice, 1 mipmap level
    if (FAILED(hr)) {
        return {};
    }

    // Get the image data
    const DirectX::Image* img = image.GetImage(0, 0, 0);
    if (img == nullptr) {
        return {};
    }

    // Copy the raw pixel data into the image
    memcpy(img->pixels, rawPixels.data(), rawPixels.size());

    return image;
}

#ifdef _WIN32
auto PGD3D::getHRESULTErrorMessage(HRESULT hr) -> wstring
{
    // Get error message
    const _com_error err(hr);
    return err.ErrorMessage();
}
#endif

auto PGD3D::getDXGIFormatFromString(const string& format) -> DXGI_FORMAT
{
    if (format == "rgba16f") {
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    if (format == "rgba32f") {
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }

    return DXGI_FORMAT_UNKNOWN;
}

#ifdef _WIN32
// NOLINTEND(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
#endif
