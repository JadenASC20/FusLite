#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include "Buffer.h"
#include "ClusterConfig.h"

class VulkanContext;

class ClusterBuilder
{
public:
    void Init(VulkanContext& context);
    void BuildClusters(VulkanContext& context, const glm::mat4& invProj, float screenWidth, float screenHeight, float nearZ, float farZ);
    void Cleanup(VkDevice device);

    const BufferAndMemory& GetClusterBuffer() const { return m_clusterBuffer; }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    BufferAndMemory m_clusterBuffer;
};