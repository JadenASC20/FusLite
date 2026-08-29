#include "VulkanHelpers.h"
#include "VulkanContext.h"
#include "ShaderModule.h"

#include <cstring>
#include <stdexcept>

void TransitionImage(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect,
    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess, VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspect, 0, 1, 0, 1 };
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void ImmediateSubmit(VulkanContext& context,
    const std::function<void(VkCommandBuffer)>& record)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context.GetCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    record(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VkQueue gfxQueue;
    vkGetDeviceQueue(context.GetDevice(),
        context.GetSelectedDevice().queueFamilyIndices.graphicsFamily.value(), 0, &gfxQueue);

    vkQueueSubmit(gfxQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);
    vkFreeCommandBuffers(context.GetDevice(), context.GetCommandPool(), 1, &cmd);
}

VkSampler CreateSimpleSampler(VkDevice device, VkFilter filter,
    VkSamplerAddressMode addressMode, VkSamplerMipmapMode mipmapMode)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.addressModeU = addressMode;
    info.addressModeV = addressMode;
    info.addressModeW = addressMode;
    info.mipmapMode = mipmapMode;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;

    VkSampler sampler;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create sampler");
    }
    return sampler;
}

ScopedShader::ScopedShader(VkDevice device, const char* spirvPath)
    : m_device(device)
    , m_module(CreateShaderModuleFromBinary(device, spirvPath))
{
}

ScopedShader::~ScopedShader()
{
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, m_module, nullptr);
    }
}

float HalfToFloat(uint16_t h)
{
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h & 0x7C00u) >> 10;
    uint32_t mant = h & 0x03FFu;
    uint32_t f;

    if (exp == 0) {
        if (mant == 0) {
            f = sign;
        }
        else { // subnormal
            exp = 127 - 15 + 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
            mant &= 0x03FFu;
            f = sign | (exp << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (mant << 13);
    }
    else {
        f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }

    float out;
    memcpy(&out, &f, 4);
    return out;
}

float Luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}