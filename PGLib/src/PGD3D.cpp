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

#ifndef _WIN32
#include <fstream>
#include <vulkan/vulkan.h>
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

#ifndef _WIN32
// Map DXGI_FORMAT to VkFormat (only the formats we actually use)
static auto dxgiToVkFormat(DXGI_FORMAT fmt) -> VkFormat {
    switch (fmt) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:   return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:   return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:                               return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// Find a memory type index supporting the required property flags
static auto findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter,
                           VkMemoryPropertyFlags props) -> uint32_t {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("Vulkan: no suitable memory type");
}

// Create a VkBuffer + VkDeviceMemory with given size, usage, and memory properties
static auto createVkBuffer(VkDevice device, VkPhysicalDevice physDev,
                           VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags memProps,
                           VkBuffer& outBuf, VkDeviceMemory& outMem) -> void {
    VkBufferCreateInfo ci{};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &outBuf) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: vkCreateBuffer failed");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, outBuf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physDev, req.memoryTypeBits, memProps);
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: vkAllocateMemory failed");
    vkBindBufferMemory(device, outBuf, outMem, 0);
}

// Create a VkImage + VkDeviceMemory
static auto createVkImage(VkDevice device, VkPhysicalDevice physDev,
                          uint32_t w, uint32_t h, VkFormat fmt,
                          VkImageUsageFlags usage,
                          VkImage& outImg, VkDeviceMemory& outMem) -> void {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = fmt;
    ci.extent        = {w, h, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ci, nullptr, &outImg) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: vkCreateImage failed");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, outImg, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physDev, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
        throw std::runtime_error("Vulkan: vkAllocateMemory failed");
    vkBindImageMemory(device, outImg, outMem, 0);
}

// One-shot command buffer submission
static auto beginOneShot(VkDevice device, VkCommandPool pool) -> VkCommandBuffer {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(device, &ai, &cb);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

static auto endOneShot(VkDevice device, VkQueue queue,
                       VkCommandPool pool, VkCommandBuffer cb) -> void {
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cb);
}

// Transition image layout
static auto transitionImage(VkCommandBuffer cb, VkImage img,
                            VkImageLayout oldLayout, VkImageLayout newLayout,
                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags srcStage,
                            VkPipelineStageFlags dstStage) -> void {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &b);
}
#endif

PGD3D::PGD3D(filesystem::path shaderPath)
    : m_shaderPath(std::move(shaderPath))
{
}

PGD3D::~PGD3D()
{
#ifndef _WIN32
    for (auto& [name, pd] : m_vkPipelines) {
        if (pd.pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_vkDevice, pd.pipeline, nullptr);
        if (pd.pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_vkDevice, pd.pipelineLayout, nullptr);
        if (pd.descriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_vkDevice, pd.descriptorSetLayout, nullptr);
        if (pd.shaderModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_vkDevice, pd.shaderModule, nullptr);
    }
    if (m_vkCommandPool  != VK_NULL_HANDLE) vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
    if (m_vkDevice       != VK_NULL_HANDLE) vkDestroyDevice(m_vkDevice, nullptr);
    if (m_vkInstance     != VK_NULL_HANDLE) vkDestroyInstance(m_vkInstance, nullptr);
#endif
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
    // Vulkan path: upload to GPU, dispatch CountAlphaValues.comp, readback
    const auto pipeIt = m_vkPipelines.find("CountAlphaValues");
    if (pipeIt == m_vkPipelines.end()) {
        spdlog::error("Vulkan: CountAlphaValues pipeline not initialized");
        return false;
    }
    const VKPipelineData& pd = pipeIt->second;

    const std::scoped_lock lock(m_d3dMutex);

    // Decompress input to RGBA8 if needed
    DirectX::ScratchImage rgbaImage;
    const DirectX::ScratchImage* src = &image;
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        if (FAILED(DirectX::Decompress(image.GetImages(), image.GetImageCount(),
                                       image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, rgbaImage)))
            return false;
        src = &rgbaImage;
    } else if (image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(image.GetImages(), image.GetImageCount(),
                                    image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM,
                                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgbaImage)))
            return false;
        src = &rgbaImage;
    }

    const DirectX::Image* img = src->GetImage(0, 0, 0);
    if (!img) return false;

    const uint32_t imgW    = static_cast<uint32_t>(img->width);
    const uint32_t imgH    = static_cast<uint32_t>(img->height);
    const VkDeviceSize imgBytes = static_cast<VkDeviceSize>(img->rowPitch * imgH);

    // Staging buffer for upload
    VkBuffer stagingBuf = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createVkBuffer(m_vkDevice, m_vkPhysicalDevice, imgBytes,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   stagingBuf, stagingMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_vkDevice, stagingMem, 0, imgBytes, 0, &mapped);
        std::memcpy(mapped, img->pixels, static_cast<size_t>(imgBytes));
        vkUnmapMemory(m_vkDevice, stagingMem);
    }

    // Device-local image for input
    VkImage inputImg = VK_NULL_HANDLE; VkDeviceMemory inputMem = VK_NULL_HANDLE;
    createVkImage(m_vkDevice, m_vkPhysicalDevice, imgW, imgH, VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                  inputImg, inputMem);

    // Output buffer (4 x uint32)
    VkBuffer outBuf = VK_NULL_HANDLE; VkDeviceMemory outBufMem = VK_NULL_HANDLE;
    createVkBuffer(m_vkDevice, m_vkPhysicalDevice, sizeof(uint32_t) * 4,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   outBuf, outBufMem);

    // Readback buffer
    VkBuffer readBuf = VK_NULL_HANDLE; VkDeviceMemory readBufMem = VK_NULL_HANDLE;
    createVkBuffer(m_vkDevice, m_vkPhysicalDevice, sizeof(uint32_t) * 4,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   readBuf, readBufMem);

    // Initialize output buffer to 0
    {
        auto cb = beginOneShot(m_vkDevice, m_vkCommandPool);
        vkCmdFillBuffer(cb, outBuf, 0, sizeof(uint32_t) * 4, 0);
        endOneShot(m_vkDevice, m_vkComputeQueue, m_vkCommandPool, cb);
    }

    // Upload input image
    {
        auto cb = beginOneShot(m_vkDevice, m_vkCommandPool);

        transitionImage(cb, inputImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {imgW, imgH, 1};
        vkCmdCopyBufferToImage(cb, stagingBuf, inputImg,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transitionImage(cb, inputImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        endOneShot(m_vkDevice, m_vkComputeQueue, m_vkCommandPool, cb);
    }

    // Create image view for input
    VkImageView inputView = VK_NULL_HANDLE;
    {
        VkImageViewCreateInfo ivCI{};
        ivCI.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCI.image            = inputImg;
        ivCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
        ivCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_vkDevice, &ivCI, nullptr, &inputView);
    }

    // Descriptor pool + set
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo dpCI{};
    dpCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCI.maxSets       = 1;
    dpCI.poolSizeCount = 2;
    dpCI.pPoolSizes    = poolSizes;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(m_vkDevice, &dpCI, nullptr, &descPool);

    VkDescriptorSetAllocateInfo dsAI{};
    dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAI.descriptorPool     = descPool;
    dsAI.descriptorSetCount = 1;
    dsAI.pSetLayouts        = &pd.descriptorSetLayout;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_vkDevice, &dsAI, &descSet);

    // Update descriptors
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = inputView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = outBuf;
    bufInfo.offset = 0;
    bufInfo.range  = sizeof(uint32_t) * 4;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = descSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo      = &imgInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = descSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo     = &bufInfo;

    vkUpdateDescriptorSets(m_vkDevice, 2, writes, 0, nullptr);

    // Dispatch compute shader
    {
        auto cb = beginOneShot(m_vkDevice, m_vkCommandPool);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pd.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pd.pipelineLayout, 0, 1, &descSet, 0, nullptr);
        const uint32_t gx = (imgW + 15) / 16;
        const uint32_t gy = (imgH + 15) / 16;
        vkCmdDispatch(cb, gx, gy, 1);

        // Memory barrier: shader write -> transfer read
        VkBufferMemoryBarrier bmb{};
        bmb.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bmb.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        bmb.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bmb.buffer              = outBuf;
        bmb.size                = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 1, &bmb, 0, nullptr);

        VkBufferCopy copyRegion{0, 0, sizeof(uint32_t) * 4};
        vkCmdCopyBuffer(cb, outBuf, readBuf, 1, &copyRegion);
        endOneShot(m_vkDevice, m_vkComputeQueue, m_vkCommandPool, cb);
    }

    // Read back
    {
        void* mapped = nullptr;
        vkMapMemory(m_vkDevice, readBufMem, 0, sizeof(uint32_t) * 4, 0, &mapped);
        uint32_t rawCounts[4]{};
        std::memcpy(rawCounts, mapped, sizeof(rawCounts));
        vkUnmapMemory(m_vkDevice, readBufMem);
        outData = { static_cast<int>(rawCounts[0]), static_cast<int>(rawCounts[1]),
                    static_cast<int>(rawCounts[2]), static_cast<int>(rawCounts[3]) };
    }

    // Cleanup
    vkDestroyDescriptorPool(m_vkDevice, descPool, nullptr);
    vkDestroyImageView(m_vkDevice, inputView, nullptr);
    vkDestroyImage(m_vkDevice, inputImg, nullptr);    vkFreeMemory(m_vkDevice, inputMem, nullptr);
    vkDestroyBuffer(m_vkDevice, stagingBuf, nullptr); vkFreeMemory(m_vkDevice, stagingMem, nullptr);
    vkDestroyBuffer(m_vkDevice, outBuf, nullptr);     vkFreeMemory(m_vkDevice, outBufMem, nullptr);
    vkDestroyBuffer(m_vkDevice, readBuf, nullptr);    vkFreeMemory(m_vkDevice, readBufMem, nullptr);
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
    // Vulkan GPU initialization
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "PGPatcher";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instCI{};
    instCI.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&instCI, nullptr, &m_vkInstance) != VK_SUCCESS) {
        spdlog::error("Vulkan: vkCreateInstance failed");
        return false;
    }

    // Pick physical device (prefer discrete)
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        spdlog::error("Vulkan: no physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> physDevices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, physDevices.data());

    for (const auto& pd : physDevices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_vkPhysicalDevice = pd;
            break;
        }
    }
    if (m_vkPhysicalDevice == VK_NULL_HANDLE)
        m_vkPhysicalDevice = physDevices[0];

    // Find compute queue family
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &qfCount, qfProps.data());

    m_vkComputeQueueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            m_vkComputeQueueFamily = i;
            break;
        }
    }
    if (m_vkComputeQueueFamily == UINT32_MAX) {
        spdlog::error("Vulkan: no compute queue family found");
        return false;
    }

    // Create logical device
    const float queuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = m_vkComputeQueueFamily;
    queueCI.queueCount       = 1;
    queueCI.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo devCI{};
    devCI.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos    = &queueCI;

    if (vkCreateDevice(m_vkPhysicalDevice, &devCI, nullptr, &m_vkDevice) != VK_SUCCESS) {
        spdlog::error("Vulkan: vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(m_vkDevice, m_vkComputeQueueFamily, 0, &m_vkComputeQueue);

    // Create command pool
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = m_vkComputeQueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_vkDevice, &poolCI, nullptr, &m_vkCommandPool) != VK_SUCCESS) {
        spdlog::error("Vulkan: vkCreateCommandPool failed");
        return false;
    }

    return true;
#endif
}

auto PGD3D::initShaders() -> bool
{
#ifdef _WIN32
    // Initialize shaders
    return initShader("CountAlphaValues.hlsl", m_shaderCountAlphaValues);
#else
    return initShader("CountAlphaValues.comp", m_shaderCountAlphaValues);
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
    if (m_vkDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Vulkan: GPU not initialized");
    }

    // The .comp shader is compiled to .spv at build time.
    const std::filesystem::path spvPath = m_shaderPath / (filename.stem().string() + ".spv");

    // Read SPIR-V bytecode
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::error("Vulkan: cannot open SPIR-V file: {}", spvPath.string());
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<char> spvData(static_cast<size_t>(size));
    file.read(spvData.data(), size);
    file.close();

    const std::string shaderName = filename.stem().string();
    VKPipelineData pd;
    pd.hasCounterBuffer = (shaderName == "CountAlphaValues");
    pd.hasPushConstants = (shaderName == "ConvertToHDR" || shaderName == "SSSFix");

    // Create shader module
    VkShaderModuleCreateInfo smCI{};
    smCI.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCI.codeSize = static_cast<size_t>(size);
    smCI.pCode    = reinterpret_cast<const uint32_t*>(spvData.data()); // NOLINT
    if (vkCreateShaderModule(m_vkDevice, &smCI, nullptr, &pd.shaderModule) != VK_SUCCESS) {
        return false;
    }

    // Descriptor set layout
    // Binding 0: storage image (input)
    // Binding 1: storage image (output) OR storage buffer (CountAlpha)
    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = pd.hasCounterBuffer
                                    ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                    : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslCI{};
    dslCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(m_vkDevice, &dslCI, nullptr, &pd.descriptorSetLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_vkDevice, pd.shaderModule, nullptr);
        return false;
    }

    // Pipeline layout (with optional push constant range)
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset     = 0;
    pcRange.size       = (shaderName == "SSSFix") ? sizeof(float) * 2 : sizeof(float);

    VkPipelineLayoutCreateInfo plCI{};
    plCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCI.setLayoutCount         = 1;
    plCI.pSetLayouts            = &pd.descriptorSetLayout;
    plCI.pushConstantRangeCount = pd.hasPushConstants ? 1 : 0;
    plCI.pPushConstantRanges    = pd.hasPushConstants ? &pcRange : nullptr;
    if (vkCreatePipelineLayout(m_vkDevice, &plCI, nullptr, &pd.pipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(m_vkDevice, pd.descriptorSetLayout, nullptr);
        vkDestroyShaderModule(m_vkDevice, pd.shaderModule, nullptr);
        return false;
    }

    // Compute pipeline
    VkPipelineShaderStageCreateInfo stageCI{};
    stageCI.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCI.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageCI.module = pd.shaderModule;
    stageCI.pName  = "main";

    VkComputePipelineCreateInfo cpCI{};
    cpCI.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpCI.stage  = stageCI;
    cpCI.layout = pd.pipelineLayout;
    if (vkCreateComputePipelines(m_vkDevice, VK_NULL_HANDLE, 1, &cpCI, nullptr, &pd.pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(m_vkDevice, pd.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_vkDevice, pd.descriptorSetLayout, nullptr);
        vkDestroyShaderModule(m_vkDevice, pd.shaderModule, nullptr);
        return false;
    }

    outShader = shaderName;
    m_vkPipelines[shaderName] = std::move(pd);
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
    if (shader.empty()) throw std::runtime_error("Shader not initialized");

    const auto pipeIt = m_vkPipelines.find(shader);
    if (pipeIt == m_vkPipelines.end()) {
        spdlog::error("Vulkan: pipeline '{}' not initialized", shader);
        return false;
    }
    const VKPipelineData& pd = pipeIt->second;

    const std::scoped_lock lock(m_d3dMutex);

    // Decompress input to RGBA32F
    DirectX::ScratchImage tmpIn;
    const DirectX::ScratchImage* src = &inTexture;
    if (DirectX::IsCompressed(inTexture.GetMetadata().format)) {
        if (FAILED(DirectX::Decompress(inTexture.GetImages(), inTexture.GetImageCount(),
                                       inTexture.GetMetadata(), DXGI_FORMAT_R32G32B32A32_FLOAT, tmpIn)))
            return false;
        src = &tmpIn;
    } else if (inTexture.GetMetadata().format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
        if (FAILED(DirectX::Convert(inTexture.GetImages(), inTexture.GetImageCount(),
                                    inTexture.GetMetadata(), DXGI_FORMAT_R32G32B32A32_FLOAT,
                                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, tmpIn)))
            return false;
        src = &tmpIn;
    }

    const DirectX::Image* srcImg0 = src->GetImage(0, 0, 0);
    if (!srcImg0) return false;

    const uint32_t srcW  = static_cast<uint32_t>(srcImg0->width);
    const uint32_t srcH  = static_cast<uint32_t>(srcImg0->height);
    const uint32_t dstW  = (outWidth  > 0) ? outWidth  : srcW;
    const uint32_t dstH  = (outHeight > 0) ? outHeight : srcH;
    const VkDeviceSize srcBytes = static_cast<VkDeviceSize>(srcImg0->rowPitch * srcH);

    // Upload input
    VkBuffer stgInBuf = VK_NULL_HANDLE; VkDeviceMemory stgInMem = VK_NULL_HANDLE;
    createVkBuffer(m_vkDevice, m_vkPhysicalDevice, srcBytes,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   stgInBuf, stgInMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_vkDevice, stgInMem, 0, srcBytes, 0, &mapped);
        std::memcpy(mapped, srcImg0->pixels, static_cast<size_t>(srcBytes));
        vkUnmapMemory(m_vkDevice, stgInMem);
    }

    // Device-local input image (RGBA32F)
    VkImage inputImg = VK_NULL_HANDLE; VkDeviceMemory inputMem = VK_NULL_HANDLE;
    createVkImage(m_vkDevice, m_vkPhysicalDevice, srcW, srcH, VK_FORMAT_R32G32B32A32_SFLOAT,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                  inputImg, inputMem);

    // Device-local output image (RGBA32F)
    VkImage outputImg = VK_NULL_HANDLE; VkDeviceMemory outputMem = VK_NULL_HANDLE;
    createVkImage(m_vkDevice, m_vkPhysicalDevice, dstW, dstH, VK_FORMAT_R32G32B32A32_SFLOAT,
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  outputImg, outputMem);

    // Readback staging buffer
    const VkDeviceSize dstBytes = static_cast<VkDeviceSize>(dstW * dstH * sizeof(float) * 4);
    VkBuffer stgOutBuf = VK_NULL_HANDLE; VkDeviceMemory stgOutMem = VK_NULL_HANDLE;
    createVkBuffer(m_vkDevice, m_vkPhysicalDevice, dstBytes,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   stgOutBuf, stgOutMem);

    // Upload input + transitions
    {
        auto cb = beginOneShot(m_vkDevice, m_vkCommandPool);

        transitionImage(cb, inputImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        transitionImage(cb, outputImg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        0, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {srcW, srcH, 1};
        vkCmdCopyBufferToImage(cb, stgInBuf, inputImg,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transitionImage(cb, inputImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        endOneShot(m_vkDevice, m_vkComputeQueue, m_vkCommandPool, cb);
    }

    // Create image views
    auto makeView = [&](VkImage img, VkFormat fmt) -> VkImageView {
        VkImageViewCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image            = img;
        ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ci.format           = fmt;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(m_vkDevice, &ci, nullptr, &view);
        return view;
    };
    VkImageView inputView  = makeView(inputImg,  VK_FORMAT_R32G32B32A32_SFLOAT);
    VkImageView outputView = makeView(outputImg, VK_FORMAT_R32G32B32A32_SFLOAT);

    // Descriptor pool + set
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
    VkDescriptorPoolCreateInfo dpCI{};
    dpCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCI.maxSets       = 1;
    dpCI.poolSizeCount = 1;
    dpCI.pPoolSizes    = &poolSize;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(m_vkDevice, &dpCI, nullptr, &descPool);

    VkDescriptorSetAllocateInfo dsAI{};
    dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAI.descriptorPool     = descPool;
    dsAI.descriptorSetCount = 1;
    dsAI.pSetLayouts        = &pd.descriptorSetLayout;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_vkDevice, &dsAI, &descSet);

    VkDescriptorImageInfo inImgInfo  {VK_NULL_HANDLE, inputView,  VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo outImgInfo {VK_NULL_HANDLE, outputView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; i++) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descSet;
        writes[i].dstBinding      = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo      = (i == 0) ? &inImgInfo : &outImgInfo;
    }
    vkUpdateDescriptorSets(m_vkDevice, 2, writes, 0, nullptr);

    // Dispatch
    {
        auto cb = beginOneShot(m_vkDevice, m_vkCommandPool);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pd.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pd.pipelineLayout, 0, 1, &descSet, 0, nullptr);

        if (pd.hasPushConstants && shaderParams != nullptr && shaderParamsSize > 0) {
            vkCmdPushConstants(cb, pd.pipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, shaderParamsSize, shaderParams);
        }

        const uint32_t gx = (dstW + 15) / 16;
        const uint32_t gy = (dstH + 15) / 16;
        vkCmdDispatch(cb, gx, gy, 1);

        // Barrier: shader write -> transfer read
        VkImageMemoryBarrier imb{};
        imb.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        imb.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image               = outputImg;
        imb.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        imb.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        imb.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {dstW, dstH, 1};
        vkCmdCopyImageToBuffer(cb, outputImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               stgOutBuf, 1, &region);

        endOneShot(m_vkDevice, m_vkComputeQueue, m_vkCommandPool, cb);
    }

    // Build output ScratchImage from raw RGBA32F pixels
    {
        void* mapped = nullptr;
        vkMapMemory(m_vkDevice, stgOutMem, 0, dstBytes, 0, &mapped);

        DirectX::ScratchImage rawOut;
        rawOut.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, dstW, dstH, 1, 1);
        const DirectX::Image* rawImg = rawOut.GetImage(0, 0, 0);
        if (rawImg) {
            std::memcpy(rawImg->pixels, mapped, static_cast<size_t>(dstBytes));
        }
        vkUnmapMemory(m_vkDevice, stgOutMem);

        if (outFormat == DXGI_FORMAT_R32G32B32A32_FLOAT) {
            outTexture = std::move(rawOut);
        } else {
            DirectX::Convert(rawOut.GetImages(), rawOut.GetImageCount(), rawOut.GetMetadata(),
                             outFormat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
                             outTexture);
        }
    }

    // Generate mip chain
    DirectX::ScratchImage mippedImage;
    if (SUCCEEDED(DirectX::GenerateMipMaps(outTexture.GetImages(), outTexture.GetImageCount(),
                                           outTexture.GetMetadata(), DirectX::TEX_FILTER_DEFAULT,
                                           0, mippedImage))) {
        outTexture = std::move(mippedImage);
    }

    // Cleanup
    vkDestroyDescriptorPool(m_vkDevice, descPool, nullptr);
    vkDestroyImageView(m_vkDevice, inputView, nullptr);
    vkDestroyImageView(m_vkDevice, outputView, nullptr);
    vkDestroyImage(m_vkDevice, inputImg, nullptr);     vkFreeMemory(m_vkDevice, inputMem, nullptr);
    vkDestroyImage(m_vkDevice, outputImg, nullptr);    vkFreeMemory(m_vkDevice, outputMem, nullptr);
    vkDestroyBuffer(m_vkDevice, stgInBuf, nullptr);    vkFreeMemory(m_vkDevice, stgInMem, nullptr);
    vkDestroyBuffer(m_vkDevice, stgOutBuf, nullptr);   vkFreeMemory(m_vkDevice, stgOutMem, nullptr);
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
