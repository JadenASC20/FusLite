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
        // COLOR_ATTACHMENT: the scene renders into it. SAMPLED: the tonemap pass reads it.
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

void RenderPass::Cleanup()
{
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
}