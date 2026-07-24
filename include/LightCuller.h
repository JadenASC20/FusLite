#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include "Buffer.h"
#include "ClusterConfig.h"

class VulkanContext;

class LightCuller
{
public:
    void Init(VulkanContext& context, const BufferAndMemory& clusterBuffer, const BufferAndMemory& lightBuffer);
    void CullLights(VulkanContext& context, const glm::mat4& view, int numLights);
    void Cleanup(VkDevice device);

    const BufferAndMemory& GetClusterLightInfoBuffer() const { return m_clusterLightInfoBuffer; }
    const BufferAndMemory& GetLightIndexBuffer() const { return m_lightIndexBuffer; }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    BufferAndMemory m_clusterLightInfoBuffer;
    BufferAndMemory m_lightIndexBuffer;
    BufferAndMemory m_globalCounterBuffer;
};