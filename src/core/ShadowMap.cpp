#include "ShadowMap.h"
#include "VulkanContext.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstdio>

void ShadowMap::Init(VulkanContext& context, uint32_t resolution)
{
    m_resolution = resolution;
    VkDevice device = context.GetDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { m_resolution, m_resolution, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // DEPTH_STENCIL_ATTACHMENT: we render into it. SAMPLED: the main shader reads it.
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context.FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow map memory");
    }
    vkBindImageMemory(device, m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map image view");
    }

    // Sampler: CLAMP_TO_EDGE so sampling outside the map returns the border depth
    // rather than wrapping around and producing bogus shadows.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map sampler");
    }

    // Comparison sampler for hardware PCF (sampler2DShadow). Same image, compare-enabled.
    VkSamplerCreateInfo cmpInfo{};
    cmpInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    cmpInfo.magFilter = VK_FILTER_LINEAR;      // LINEAR + compare = 2x2 hardware PCF
    cmpInfo.minFilter = VK_FILTER_LINEAR;
    cmpInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cmpInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cmpInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cmpInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    cmpInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // off-map = far = lit
    cmpInfo.compareEnable = VK_TRUE;
    cmpInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;   // fragment depth <= stored => lit
    cmpInfo.minLod = 0.0f;
    cmpInfo.maxLod = VK_LOD_CLAMP_NONE;

    if (vkCreateSampler(device, &cmpInfo, nullptr, &m_compareSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow compare sampler");
    }

    printf("Shadow map created (%ux%u, D32_SFLOAT).\n", m_resolution, m_resolution);
}

glm::mat4 ShadowMap::ComputeLightViewProj(const glm::vec3& lightDir, float extent, float nearZ, float farZ)
{
    // Directional light: position the "camera" back along the light direction,
    // far enough to see the whole scene volume, using an orthographic projection.
    glm::vec3 dir = glm::normalize(lightDir);
    glm::vec3 lightPos = dir * (extent * 1.5f); // pull back along the light dir
    glm::vec3 target = glm::vec3(0.0f);
    glm::vec3 up = (fabsf(dir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    glm::mat4 lightView = glm::lookAt(lightPos, target, up);
    glm::mat4 lightProj = glm::ortho(-extent, extent, -extent, extent, nearZ, farZ);

    return lightProj * lightView;
}

void ShadowMap::Cleanup(VkDevice device)
{
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
    if (m_depthImageView != VK_NULL_HANDLE) vkDestroyImageView(device, m_depthImageView, nullptr);
    if (m_depthImage != VK_NULL_HANDLE) vkDestroyImage(device, m_depthImage, nullptr);
    if (m_depthMemory != VK_NULL_HANDLE) vkFreeMemory(device, m_depthMemory, nullptr);
    if (m_compareSampler != VK_NULL_HANDLE) vkDestroySampler(device, m_compareSampler, nullptr);
}