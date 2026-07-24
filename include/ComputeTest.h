#pragma once
#include <volk.h>
#include "Buffer.h"

class VulkanContext;

class ComputeTest
{
public:
    void Init(VulkanContext& context);
    void Run(VulkanContext& context, uint32_t elementCount);
    void Cleanup(VkDevice device);

    const BufferAndMemory& GetOutputBuffer() const { return m_outputBuffer; }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    BufferAndMemory m_outputBuffer;
};