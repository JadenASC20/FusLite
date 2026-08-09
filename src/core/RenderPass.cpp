#include "RenderPass.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include <stdexcept>
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
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_lumImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_hdrFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_context->GetDevice(), &viewInfo, nullptr, &m_lumImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create luminance image view");

    // Host-visible staging buffer, persistently mapped, one HDR texel = 8 bytes.
    VkDeviceSize texelBytes = 8; // R16G16B16A16_SFLOAT
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = texelBytes;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_context->GetDevice(), &bufInfo, nullptr, &m_lumStagingBuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create luminance staging buffer");

    VkMemoryRequirements bufReq;
    vkGetBufferMemoryRequirements(m_context->GetDevice(), m_lumStagingBuffer, &bufReq);
    VkMemoryAllocateInfo bufAlloc{};
    bufAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufAlloc.allocationSize = bufReq.size;
    bufAlloc.memoryTypeIndex = m_context->FindMemoryType(bufReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_context->GetDevice(), &bufAlloc, nullptr, &m_lumStagingMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate luminance staging memory");
    vkBindBufferMemory(m_context->GetDevice(), m_lumStagingBuffer, m_lumStagingMemory, 0);
    vkMapMemory(m_context->GetDevice(), m_lumStagingMemory, 0, texelBytes, 0, &m_lumStagingMapped);

    printf("Luminance readback resources created (1x1).\n");
}

void RenderPass::Cleanup()
{
    if (m_lumStagingMapped) { vkUnmapMemory(m_context->GetDevice(), m_lumStagingMemory); m_lumStagingMapped = nullptr; }
    if (m_lumStagingBuffer) vkDestroyBuffer(m_context->GetDevice(), m_lumStagingBuffer, nullptr);
    if (m_lumStagingMemory) vkFreeMemory(m_context->GetDevice(), m_lumStagingMemory, nullptr);
    if (m_lumImageView)     vkDestroyImageView(m_context->GetDevice(), m_lumImageView, nullptr);
    if (m_lumImage)         vkDestroyImage(m_context->GetDevice(), m_lumImage, nullptr);
    if (m_lumImageMemory)   vkFreeMemory(m_context->GetDevice(), m_lumImageMemory, nullptr);

    if (!m_context) return;

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

    for (size_t i = 0; i < m_historyImages.size(); i++) {
        vkDestroyImageView(m_context->GetDevice(), m_historyImageViews[i], nullptr);
        vkDestroyImage(m_context->GetDevice(), m_historyImages[i], nullptr);
        vkFreeMemory(m_context->GetDevice(), m_historyMemory[i], nullptr);
    }
    m_historyImages.clear();
    m_historyMemory.clear();
    m_historyImageViews.clear();

}