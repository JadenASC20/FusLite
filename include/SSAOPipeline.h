#pragma once
#include <volk.h>
#include <vector>
#include <glm/glm.hpp>
#include "Buffer.h"
class VulkanContext;

class SSAOPipeline
{
public:
    struct SSAOPush {
        glm::mat4 proj;
        glm::mat4 invProj;
        glm::mat4 view;
        glm::vec2 screenSize;
        float radius;
        float bias;
        float power;
        float _p0, _p1, _p2;
    };
    void Init(VulkanContext& context, VkFormat ssaoFormat,
        VkShaderModule fullscreenVert, VkShaderModule ssaoFrag,
        const std::vector<VkImageView>& depthViews,
        const std::vector<VkImageView>& normalViews,
        VkImageView noiseView, VkSampler noiseSampler,
        const BufferAndMemory& kernelBuffer, size_t kernelSize);
    void Cleanup();
    void Bind(VkCommandBuffer cmd, uint32_t imageIndex, const SSAOPush& pc) const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};