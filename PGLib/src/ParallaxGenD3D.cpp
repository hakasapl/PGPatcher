#include "ParallaxGenD3D.hpp"

#include "PGGlobals.hpp"
#include "ParallaxGenDirectory.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"

#include <DirectXMath.h>
#include <DirectXTex.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <comdef.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <dxgi.h>
#include <dxgiformat.h>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using namespace std;
using namespace ParallaxGenUtil;
using Microsoft::WRL::ComPtr;

// We need to access unions as part of certain DX11 structures
// reinterpret cast is needed often for type casting with DX11
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)

ParallaxGenD3D::ParallaxGenD3D(filesystem::path shaderPath)
    : m_shaderPath(std::move(shaderPath))
{
}

auto ParallaxGenD3D::extendedTexClassify(const std::vector<std::wstring>& bsaExcludes) -> bool
{
    auto* const pgd = PGGlobals::getPGD();

    auto& envMasks = pgd->getTextureMap(NIFUtil::TextureSlots::ENVMASK);

    // loop through maps
    for (auto& envSlot : envMasks) {
        vector<tuple<NIFUtil::PGTexture, bool, bool, bool>> cmMaps;

        for (const auto& envMask : envSlot.second) {
            if (envMask.type != NIFUtil::TextureType::ENVIRONMENTMASK) {
                continue;
            }

            bool result = false;
            bool hasMetalness = false;
            bool hasGlosiness = false;
            bool hasEnvMask = false;

            const bool bFileInVanillaBSA = pgd->isFileInBSA(envMask.path, bsaExcludes);
            if (!bFileInVanillaBSA) {
                try {
                    if (!checkIfCM(envMask.path, result, hasEnvMask, hasGlosiness, hasMetalness)) {
                        Logger::error(L"Failed to check if {} is complex material", envMask.path.wstring());
                        continue;
                    }
                } catch (...) {
                    Logger::error(L"Failed to check if {} is complex material", envMask.path.wstring());
                    continue;
                }
            }

            if (result) {
                // remove old env mask
                cmMaps.emplace_back(envMask, hasEnvMask, hasGlosiness, hasMetalness);
            }
        }

        // update map
        for (const auto& [cmMap, hasEnvMask, hasGlosiness, hasMetalness] : cmMaps) {
            envSlot.second.erase(cmMap);
            envSlot.second.insert({ cmMap.path, NIFUtil::TextureType::COMPLEXMATERIAL });
            pgd->setTextureType(cmMap.path, NIFUtil::TextureType::COMPLEXMATERIAL);

            if (hasEnvMask) {
                pgd->addTextureAttribute(cmMap.path, NIFUtil::TextureAttribute::CM_ENVMASK);
            }

            if (hasGlosiness) {
                pgd->addTextureAttribute(cmMap.path, NIFUtil::TextureAttribute::CM_GLOSSINESS);
            }

            if (hasMetalness) {
                pgd->addTextureAttribute(cmMap.path, NIFUtil::TextureAttribute::CM_METALNESS);
            }
        }
    }

    return true;
}

auto ParallaxGenD3D::checkIfCM(
    const filesystem::path& ddsPath, bool& result, bool& hasEnvMask, bool& hasGlosiness, bool& hasMetalness) -> bool
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

auto ParallaxGenD3D::countPixelValues(const DirectX::ScratchImage& image, array<int, 4>& outData) -> bool
{
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

    array<unsigned int, 4> outputData = { 0, 0, 0, 0 };
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
    if (!blockingDispatch(m_shaderCountAlphaValues, { inputSRV }, { outputBufferUAV }, {},
            static_cast<UINT>(imageMeta.width), static_cast<UINT>(imageMeta.height), 1)) {
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
}

auto ParallaxGenD3D::checkIfAspectRatioMatches(
    const std::filesystem::path& ddsPath1, const std::filesystem::path& ddsPath2) -> bool
{
    // get metadata (should only pull headers, which is much faster)
    DirectX::TexMetadata ddsImageMeta1 {};
    if (!getDDSMetadata(ddsPath1, ddsImageMeta1)) {
        Logger::error(L"Failed to load texture header: {}", ddsPath1.wstring());
        return false;
    }

    DirectX::TexMetadata ddsImageMeta2 {};
    if (!getDDSMetadata(ddsPath2, ddsImageMeta2)) {
        Logger::error(L"Failed to load texture header: {}", ddsPath2.wstring());
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

auto ParallaxGenD3D::initGPU() -> bool
{
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
}

auto ParallaxGenD3D::initShaders() -> bool
{
    // Initialize shaders
    return initShader("CountAlphaValues.hlsl", m_shaderCountAlphaValues);
}

auto ParallaxGenD3D::initShader(const std::filesystem::path& filename, ComPtr<ID3D11ComputeShader>& outShader) const
    -> bool
{
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
    hr = D3DCompileFromFile(shaderAbsPath.c_str(), nullptr, nullptr, "main", "cs_5_0", dwShaderFlags, 0,
        shaderBlob.ReleaseAndGetAddressOf(), ptrErrorBlob.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    // Create compute shader on GPU
    hr = m_ptrDevice->CreateComputeShader(
        shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, outShader.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

//
// GPU Helpers
//
auto ParallaxGenD3D::isPowerOfTwo(unsigned int x) -> bool { return (x != 0U) && ((x & (x - 1)) == 0U); }

auto ParallaxGenD3D::createTexture2D(const DirectX::ScratchImage& texture, ComPtr<ID3D11Texture2D>& dest) const -> bool
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
    hr = DirectX::CreateTexture(m_ptrDevice.Get(), texture.GetImages(), texture.GetImageCount(), texture.GetMetadata(),
        reinterpret_cast<ID3D11Resource**>(dest.ReleaseAndGetAddressOf()));

    return !FAILED(hr);
}

auto ParallaxGenD3D::createTexture2D(ComPtr<ID3D11Texture2D>& existingTexture, ComPtr<ID3D11Texture2D>& dest) const
    -> bool
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
    hr = m_ptrDevice->CreateTexture2D(&textureOutDesc, nullptr, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

auto ParallaxGenD3D::createTexture2D(D3D11_TEXTURE2D_DESC& desc, ComPtr<ID3D11Texture2D>& dest) const -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    // Define error object
    HRESULT hr {};

    // Create texture
    hr = m_ptrDevice->CreateTexture2D(&desc, nullptr, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

auto ParallaxGenD3D::createShaderResourceView(
    const ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& dest) const -> bool
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
    hr = m_ptrDevice->CreateShaderResourceView(texture.Get(), &shaderDesc, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

auto ParallaxGenD3D::createUnorderedAccessView(
    const ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11UnorderedAccessView>& dest) const -> bool
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
    hr = m_ptrDevice->CreateUnorderedAccessView(texture.Get(), &uavDesc, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

auto ParallaxGenD3D::createUnorderedAccessView(const ComPtr<ID3D11Resource>& gpuResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC& desc, ComPtr<ID3D11UnorderedAccessView>& dest) const -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    HRESULT hr {};

    hr = m_ptrDevice->CreateUnorderedAccessView(gpuResource.Get(), &desc, &dest);
    return !FAILED(hr);
}

auto ParallaxGenD3D::createBuffer(const void* data, D3D11_BUFFER_DESC& desc, ComPtr<ID3D11Buffer>& dest) const -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    const HRESULT hr = m_ptrDevice->CreateBuffer(&desc, &initData, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

auto ParallaxGenD3D::createConstantBuffer(const void* data, const UINT& size, ComPtr<ID3D11Buffer>& dest) const -> bool
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
    hr = m_ptrDevice->CreateBuffer(&cbDesc, &cbInitData, dest.ReleaseAndGetAddressOf());
    return !FAILED(hr);
}

void ParallaxGenD3D::copyResource(const ComPtr<ID3D11Resource>& src, const ComPtr<ID3D11Resource>& dest)
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const lock_guard<mutex> lock(m_gpuOperationMutex);
    m_ptrContext->CopyResource(dest.Get(), src.Get());
}

void ParallaxGenD3D::generateMips(const ComPtr<ID3D11ShaderResourceView>& srv)
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const lock_guard<mutex> lock(m_gpuOperationMutex);
    m_ptrContext->GenerateMips(srv.Get());
}

void ParallaxGenD3D::flushGPU()
{
    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const lock_guard<mutex> lock(m_gpuOperationMutex);
    m_ptrContext->Flush();
}

auto ParallaxGenD3D::blockingDispatch(const Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader,
    const std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>& srvs,
    const std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>>& uavs,
    const std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>& constantBuffers, UINT threadGroupCountX,
    UINT threadGroupCountY, UINT threadGroupCountZ) -> bool
{
    if (m_ptrDevice == nullptr) {
        throw runtime_error("GPU not initialized");
    }

    if (m_ptrContext == nullptr) {
        throw runtime_error("Context not initialized");
    }

    const lock_guard<mutex> lock(m_gpuOperationMutex);

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
    hr = m_ptrContext->GetData(ptrQuery.Get(), &queryData, sizeof(queryData),
        D3D11_ASYNC_GETDATA_DONOTFLUSH); // block until complete
    if (FAILED(hr)) {
        return false;
    }
    ptrQuery.Reset();

    // Clean up
    static const array<ID3D11ShaderResourceView*, 1> nullSRV = { nullptr };
    static const array<ID3D11UnorderedAccessView*, 1> nullUAV = { nullptr };
    static const array<ID3D11Buffer*, 1> nullBuffer = { nullptr };

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

auto ParallaxGenD3D::readBack(const ComPtr<ID3D11Texture2D>& gpuResource, DirectX::ScratchImage& outImage) -> bool
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
        const lock_guard<mutex> lock(m_gpuOperationMutex);

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

template <typename T> auto ParallaxGenD3D::readBack(const ComPtr<ID3D11Buffer>& gpuResource, vector<T>& outData) -> bool
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
        const lock_guard<mutex> lock(m_gpuOperationMutex);

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = m_ptrContext->Map(stagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            return false;
        }

        // Access the texture data from MappedResource.pData
        const size_t numElements = bufferDesc.ByteWidth / sizeof(T);
        vector<T> outputData(
            reinterpret_cast<T*>(mappedResource.pData), reinterpret_cast<T*>(mappedResource.pData) + numElements);
        outData = std::move(outputData);

        // Cleaup
        m_ptrContext->Unmap(stagingBuffer.Get(), 0); // cleanup map
    }

    stagingBuffer.Reset();

    return true;
}

//
// Texture Helpers
//

auto ParallaxGenD3D::getDDS(const filesystem::path& ddsPath, // NOLINT(readability-convert-member-functions-to-static)
    DirectX::ScratchImage& dds) const -> bool
{
    auto* const pgd = PGGlobals::getPGD();

    HRESULT hr {};

    if (pgd->isLooseFile(ddsPath)) {
        const filesystem::path fullPath = pgd->getLooseFileFullPath(ddsPath);

        // Load DDS file
        hr = DirectX::LoadFromDDSFile(fullPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, dds);
    } else if (pgd->isBSAFile(ddsPath)) {
        vector<std::byte> ddsBytes = pgd->getFile(ddsPath);

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

auto ParallaxGenD3D::getDDSMetadata(const filesystem::path& ddsPath, DirectX::TexMetadata& ddsMeta) -> bool
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
        vector<std::byte> ddsBytes = pgd->getFile(ddsPath);

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

auto ParallaxGenD3D::applyShaderToTexture(const DirectX::ScratchImage& inTexture, DirectX::ScratchImage& outTexture,
    const Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader, const DXGI_FORMAT& outFormat, const UINT& outWidth,
    const UINT& outHeight, const void* shaderParams, const UINT& shaderParamsSize) -> bool
{
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

    if (!blockingDispatch(shader, { inputDDSSRV }, { outputDDSUAV }, constantBuffers,
            static_cast<UINT>(inputMeta.width), static_cast<UINT>(inputMeta.height), 1)) {
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
}

auto ParallaxGenD3D::loadRawPixelsToScratchImage(const vector<unsigned char>& rawPixels, const size_t& width,
    const size_t& height, const size_t& mips, DXGI_FORMAT format) -> DirectX::ScratchImage
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

auto ParallaxGenD3D::getHRESULTErrorMessage(HRESULT hr) -> string
{
    // Get error message
    const _com_error err(hr);
    return err.ErrorMessage();
}

auto ParallaxGenD3D::getDXGIFormatFromString(const string& format) -> DXGI_FORMAT
{
    if (format == "rgba16f") {
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    if (format == "rgba32f") {
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }

    return DXGI_FORMAT_UNKNOWN;
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
