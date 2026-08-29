#include "RenderPass.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include <stdexcept>
#include <algorithm>
#include <cstdio>

RenderPass::RenderPass() {}
RenderPass::~RenderPass() {}

void RenderPass::Init(VulkanContext& context, const Swapchain& swapchain)
{
    m_context = &context;
    m_depthFormat = context.FindDepthFormat();
    CreateDepthResources(swapchain);
    CreateHdrResources(swapchain);
    CreateMotionResources(swapchain);
    CreateNormalResources(swapchain);
    CreateMaterialResources(swapchain);
    CreateSSRResources(swapchain);
    CreateCompositeResources(swapchain);
    CreateSSAOResources(swapchain);
    CreateHiZResources(swapchain);
    CreateHistoryResources(swapchain);
    CreateLuminanceResources();
}

void RenderPass::CreateDepthResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();

    m_depthImages.resize(numImages);
    m_depthImageMemories.resize(numImages);
    m_depthImageViews.resize(numImages);

    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_depthImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_depthImages[i], &memRequirements);

        VkPhysicalDeviceMemoryProperties memProperties = m_context->GetSelectedDevice().memoryProperties;
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t j = 0; j < memProperties.memoryTypeCount; j++) {
            if ((memRequirements.memoryTypeBits & (1 << j)) &&
                (memProperties.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memTypeIndex = j;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) throw std::runtime_error("Failed to find memory type for depth image");

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memTypeIndex;

        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_depthImageMemories[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate depth image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_depthImages[i], m_depthImageMemories[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_depthImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view");
        }
    }

    printf("%zu depth resource(s) created.\n", numImages);
}

void RenderPass::CreateHdrResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();

    m_hdrImages.resize(numImages);
    m_hdrImageMemories.resize(numImages);
    m_hdrImageViews.resize(numImages);

    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_hdrFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // COLOR_ATTACHMENT: the scene renders into it. 
        // SAMPLED: the tonemap pass reads it.
        // TRANSFER_DST: the TAA resolve copies the resolved result back into it.
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_hdrImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HDR color image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_hdrImages[i], &memRequirements);

        VkPhysicalDeviceMemoryProperties memProperties = m_context->GetSelectedDevice().memoryProperties;
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t j = 0; j < memProperties.memoryTypeCount; j++) {
            if ((memRequirements.memoryTypeBits & (1 << j)) &&
                (memProperties.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memTypeIndex = j;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) throw std::runtime_error("Failed to find memory type for HDR image");

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memTypeIndex;

        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_hdrImageMemories[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate HDR image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_hdrImages[i], m_hdrImageMemories[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_hdrImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_hdrFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_hdrImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create HDR image view");
        }
    }

    printf("%zu HDR color resource(s) created (R16G16B16A16_SFLOAT).\n", numImages);
}

void RenderPass::CreateMotionResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();

    m_motionImages.resize(numImages);
    m_motionMemory.resize(numImages);
    m_motionImageViews.resize(numImages);

    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_motionFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_motionImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create motion vector image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_motionImages[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_motionMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate motion vector image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_motionImages[i], m_motionMemory[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_motionImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_motionFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_motionImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create motion vector image view");
        }
    }

    printf("%zu motion vector resource(s) created (R16G16_SFLOAT).\n", numImages);
}

void RenderPass::CreateNormalResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();
    m_normalImages.resize(numImages);
    m_normalMemory.resize(numImages);
    m_normalImageViews.resize(numImages);
    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_normalFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_normalImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal G-buffer image");
        }
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_normalImages[i], &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_normalMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate normal G-buffer image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_normalImages[i], m_normalMemory[i], 0);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_normalImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_normalFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_normalImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create normal G-buffer image view");
        }
    }
    printf("%zu normal G-buffer resource(s) created (R16G16B16A16_SFLOAT).\n", numImages);
}

void RenderPass::CreateHistoryResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    const size_t kHistoryCount = 2;                 // temporal ping-pong, NOT per-swapchain-image

    m_historyImages.resize(kHistoryCount);
    m_historyMemory.resize(kHistoryCount);
    m_historyImageViews.resize(kHistoryCount);

    for (size_t i = 0; i < kHistoryCount; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_hdrFormat;              // MUST match HDR, feedback loop precision
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // COLOR_ATTACHMENT: resolve renders into it. SAMPLED: next frame reads it as history.
        // TRANSFER_SRC: CopyImage the resolved result into the HDR target for tonemap.
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_historyImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create history image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_historyImages[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_historyMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate history image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_historyImages[i], m_historyMemory[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_historyImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_hdrFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_historyImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create history image view");
        }
    }
    printf("%zu history resource(s) created (ping-pong, HDR format).\n", kHistoryCount);
}

void RenderPass::CreateLuminanceResources()
{
    // 1x1 image, blit the whole HDR frame down
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_hdrFormat;                 // matches HDR for blit compatibility
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_lumImage) != VK_SUCCESS)
        throw std::runtime_error("Failed to create luminance image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_context->GetDevice(), m_lumImage, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_context->FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_lumImageMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate luminance image memory");
    vkBindImageMemory(m_context->GetDevice(), m_lumImage, m_lumImageMemory, 0);

    // Host-visible staging buffers, one per frame-in-flight
    VkDeviceSize texelBytes = 8; // R16G16B16A16_SFLOAT
    for (int f = 0; f < 2; f++) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = texelBytes;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_context->GetDevice(), &bufInfo, nullptr, &m_lumStagingBuffers[f]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create luminance staging buffer");

        VkMemoryRequirements bufReq;
        vkGetBufferMemoryRequirements(m_context->GetDevice(), m_lumStagingBuffers[f], &bufReq);
        VkMemoryAllocateInfo bufAlloc{};
        bufAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bufAlloc.allocationSize = bufReq.size;
        bufAlloc.memoryTypeIndex = m_context->FindMemoryType(bufReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &bufAlloc, nullptr, &m_lumStagingMemories[f]) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate luminance staging memory");
        vkBindBufferMemory(m_context->GetDevice(), m_lumStagingBuffers[f], m_lumStagingMemories[f], 0);
        vkMapMemory(m_context->GetDevice(), m_lumStagingMemories[f], 0, texelBytes, 0, &m_lumStagingMapped[f]);
    }
    printf("Luminance readback resources created (1x1).\n");
}

void RenderPass::CreateSSRResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();
    m_ssrImages.resize(numImages);
    m_ssrMemory.resize(numImages);
    m_ssrImageViews.resize(numImages);
    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_ssrFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_ssrImages[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSR image");
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_ssrImages[i], &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_ssrMemory[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate SSR image memory");
        vkBindImageMemory(m_context->GetDevice(), m_ssrImages[i], m_ssrMemory[i], 0);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_ssrImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_ssrFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_ssrImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSR image view");
    }
    printf("%zu SSR resource(s) created.\n", numImages);
}

void RenderPass::CreateCompositeResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();
    m_compositeImages.resize(numImages);
    m_compositeMemory.resize(numImages);
    m_compositeImageViews.resize(numImages);
    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_compositeFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_compositeImages[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create composite image");
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_compositeImages[i], &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_compositeMemory[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate composite image memory");
        vkBindImageMemory(m_context->GetDevice(), m_compositeImages[i], m_compositeMemory[i], 0);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_compositeImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_compositeFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_compositeImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create composite image view");
    }
    printf("%zu composite resource(s) created.\n", numImages);
}

void RenderPass::CreateSSAOResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();

    auto makeTargets = [&](std::vector<VkImage>& images,
        std::vector<VkDeviceMemory>& memories,
        std::vector<VkImageView>& views)
    {
        images.resize(numImages);
        memories.resize(numImages);
        views.resize(numImages);
        for (size_t i = 0; i < numImages; i++) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { extent.width, extent.height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = m_ssaoFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &images[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create SSAO image");

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(m_context->GetDevice(), images[i], &memReq);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = m_context->FindMemoryType(
                memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &memories[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate SSAO image memory");
            vkBindImageMemory(m_context->GetDevice(), images[i], memories[i], 0);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_ssaoFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &views[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create SSAO image view");
        }
    };

    makeTargets(m_ssaoImages, m_ssaoMemory, m_ssaoImageViews);
    makeTargets(m_ssaoBlurImages, m_ssaoBlurMemory, m_ssaoBlurImageViews);

    printf("%zu SSAO resource(s) created x2 (raw + blur, R8_UNORM).\n", numImages);
}

void RenderPass::CreateHiZResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    m_hizBaseExtent = extent;

    // Full mip chain down to 1x1
    uint32_t maxDim = std::max(extent.width, extent.height);
    m_hizMipLevels = 1;
    while ((maxDim >> (m_hizMipLevels - 1)) > 1) m_hizMipLevels++;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { extent.width, extent.height, 1 };
    imageInfo.mipLevels = m_hizMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_hizFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // STORAGE: compute writes each mip. SAMPLED: the march + debug view read it
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_hizImage) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z image");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_context->GetDevice(), m_hizImage, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_context->FindMemoryType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_hizMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate Hi-Z memory");
    vkBindImageMemory(m_context->GetDevice(), m_hizImage, m_hizMemory, 0);

    // Full-chain sample view (all mips -- for the march + debug view)
    VkImageViewCreateInfo sv{};
    sv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    sv.image = m_hizImage;
    sv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    sv.format = m_hizFormat;
    sv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_hizMipLevels, 0, 1 };
    if (vkCreateImageView(m_context->GetDevice(), &sv, nullptr, &m_hizSampleView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z sample view");

    // each views exactly one mip level, for storage-image writes
    m_hizMipViews.resize(m_hizMipLevels);
    for (uint32_t m = 0; m < m_hizMipLevels; m++) {
        VkImageViewCreateInfo mv{};
        mv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        mv.image = m_hizImage;
        mv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        mv.format = m_hizFormat;
        mv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1 };
        if (vkCreateImageView(m_context->GetDevice(), &mv, nullptr, &m_hizMipViews[m]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Hi-Z mip view");
    }
    printf("Hi-Z sample view: %p\n", (void*)m_hizSampleView);
    printf("Hi-Z pyramid created (%ux%u, %u mips, R32G32_SFLOAT).\n",
        extent.width, extent.height, m_hizMipLevels);
}

void RenderPass::CreateMaterialResources(const Swapchain& swapchain)
{
    VkExtent2D extent = swapchain.GetExtent();
    size_t numImages = swapchain.GetImageViews().size();
    
    m_materialImages.resize(numImages);
    m_materialMemory.resize(numImages);
    m_materialImageViews.resize(numImages);
    
    for (size_t i = 0; i < numImages; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_materialFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_context->GetDevice(), &imageInfo, nullptr, &m_materialImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material G-buffer image");
        }
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(m_context->GetDevice(), m_materialImages[i], &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_context->FindMemoryType(
            memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_context->GetDevice(), &allocInfo, nullptr, &m_materialMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate material G-buffer image memory");
        }
        vkBindImageMemory(m_context->GetDevice(), m_materialImages[i], m_materialMemory[i], 0);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_materialImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_materialFormat;                        // <-- material format
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_materialImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material G-buffer image view");
        }
    }
    printf("%zu material G-buffer resource(s) created (R8G8_UNORM).\n", numImages);
}

void RenderPass::Cleanup()
{
    if (!m_context) return;

    for (int f = 0; f < 2; f++) {
        if (m_lumStagingMapped[f]) { vkUnmapMemory(m_context->GetDevice(), m_lumStagingMemories[f]); m_lumStagingMapped[f] = nullptr; }
        if (m_lumStagingBuffers[f]) vkDestroyBuffer(m_context->GetDevice(), m_lumStagingBuffers[f], nullptr);
        if (m_lumStagingMemories[f]) vkFreeMemory(m_context->GetDevice(), m_lumStagingMemories[f], nullptr);
    }
    if (m_lumImage)       vkDestroyImage(m_context->GetDevice(), m_lumImage, nullptr);
    if (m_lumImageMemory) vkFreeMemory(m_context->GetDevice(), m_lumImageMemory, nullptr);

    for (size_t i = 0; i < m_depthImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_depthImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_depthImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_depthImageMemories[i], nullptr);
    }
    m_depthImages.clear();
    m_depthImageMemories.clear();
    m_depthImageViews.clear();

    for (size_t i = 0; i < m_hdrImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_hdrImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_hdrImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_hdrImageMemories[i], nullptr);
    }
    m_hdrImages.clear();
    m_hdrImageMemories.clear();
    m_hdrImageViews.clear();

    for (size_t i = 0; i < m_motionImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_motionImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_motionImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_motionMemory[i], nullptr);
    }
    m_motionImages.clear();
    m_motionMemory.clear();
    m_motionImageViews.clear();

    for (size_t i = 0; i < m_normalImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_normalImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_normalImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_normalMemory[i], nullptr);
    }
    m_normalImages.clear();
    m_normalMemory.clear();
    m_normalImageViews.clear();

    for (size_t i = 0; i < m_historyImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_historyImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_historyImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_historyMemory[i], nullptr);
    }
    m_historyImages.clear();
    m_historyMemory.clear();
    m_historyImageViews.clear();

    for (size_t i = 0; i < m_ssrImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_ssrImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_ssrImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_ssrMemory[i], nullptr);
    }
    m_ssrImages.clear(); 
    m_ssrMemory.clear(); 
    m_ssrImageViews.clear();

    for (size_t i = 0; i < m_compositeImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_compositeImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_compositeImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_compositeMemory[i], nullptr);
    }
    m_compositeImages.clear(); 
    m_compositeMemory.clear(); 
    m_compositeImageViews.clear();

    for (auto view : m_ssaoImageViews) vkDestroyImageView(m_context->GetDevice(), view, nullptr);
    for (auto img : m_ssaoImages) vkDestroyImage(m_context->GetDevice(), img, nullptr);
    for (auto mem : m_ssaoMemory) vkFreeMemory(m_context->GetDevice(), mem, nullptr);
    for (auto view : m_ssaoBlurImageViews) vkDestroyImageView(m_context->GetDevice(), view, nullptr);
    for (auto img : m_ssaoBlurImages) vkDestroyImage(m_context->GetDevice(), img, nullptr);
    for (auto mem : m_ssaoBlurMemory) vkFreeMemory(m_context->GetDevice(), mem, nullptr);

    for (auto v : m_hizMipViews) vkDestroyImageView(m_context->GetDevice(), v, nullptr);
    m_hizMipViews.clear();
    if (m_hizSampleView) vkDestroyImageView(m_context->GetDevice(), m_hizSampleView, nullptr);
    if (m_hizImage) vkDestroyImage(m_context->GetDevice(), m_hizImage, nullptr);
    if (m_hizMemory) vkFreeMemory(m_context->GetDevice(), m_hizMemory, nullptr);

    for (auto view : m_materialImageViews) vkDestroyImageView(m_context->GetDevice(), view, nullptr);
    for (auto img : m_materialImages) vkDestroyImage(m_context->GetDevice(), img, nullptr);
    for (auto mem : m_materialMemory) vkFreeMemory(m_context->GetDevice(), mem, nullptr);
}