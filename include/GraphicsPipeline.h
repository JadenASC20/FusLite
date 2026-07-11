#pragma once
#include <volk.h>
#include <vector>

struct GLFWwindow;
class VulkanContext;
class RenderPass;
struct BufferAndMemory;

class GraphicsPipeline
{
public:
    GraphicsPipeline();
    ~GraphicsPipeline();

    void Init(VulkanContext& context, GLFWwindow* window, const RenderPass& renderPass,
        VkShaderModule vertShader, VkShaderModule fragShader,
        const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize);
    void Cleanup();

    void Bind(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

private:
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool(uint32_t numImages);
    void CreateDescriptorSets(const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
};