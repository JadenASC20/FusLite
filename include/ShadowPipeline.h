#pragma once
#include <volk.h>
#include <glm/glm.hpp>

class VulkanContext;

struct ShadowPushConstants
{
    glm::mat4 lightViewProj;
    glm::mat4 model;
};

class ShadowPipeline
{
public:
    void Init(VulkanContext& context, VkFormat depthFormat, uint32_t resolution,
        VkShaderModule vertShader, VkShaderModule fragShader);
    void Cleanup();

    void Bind(VkCommandBuffer commandBuffer) const;
    void PushConstants(VkCommandBuffer commandBuffer, const ShadowPushConstants& pc) const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
};