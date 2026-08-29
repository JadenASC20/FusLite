#pragma once
#include <volk.h>
#include <cstdint>
#include <functional>

class VulkanContext;

// Image layout transitions
void TransitionImage(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect,
    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess, VkAccessFlags dstAccess);

// One-shot command buffer: allocate, record, submit, wait, free.
// Use for setup work that must complete before the render loop starts.
void ImmediateSubmit(VulkanContext& context,
    const std::function<void(VkCommandBuffer)>& record);

// Sampler factory for the simple cases (no anisotropy, no compare, full mips).
VkSampler CreateSimpleSampler(VkDevice device, VkFilter filter,
    VkSamplerAddressMode addressMode, VkSamplerMipmapMode mipmapMode);

// RAII wrapper so shader modules don't need a matching vkDestroyShaderModule
// call at every construction site.
class ScopedShader
{
public:
    ScopedShader(VkDevice device, const char* spirvPath);
    ~ScopedShader();

    ScopedShader(const ScopedShader&) = delete;
    ScopedShader& operator=(const ScopedShader&) = delete;

    operator VkShaderModule() const { return m_module; }
    VkShaderModule Get() const { return m_module; }

private:
    VkDevice       m_device = VK_NULL_HANDLE;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

// HDR readback: the auto-exposure staging buffer holds R16G16B16A16_SFLOAT.
float HalfToFloat(uint16_t h);
float Luminance(float r, float g, float b);