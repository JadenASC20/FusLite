#pragma once
#include <volk.h>
#include <vector>

struct GLFWwindow;
class VulkanContext;
struct BufferAndMemory;
struct VulkanTexture;

class GraphicsPipeline
{
public:
    GraphicsPipeline();
    ~GraphicsPipeline();

    void Init(VulkanContext& context, GLFWwindow* window,
        VkFormat colorFormat, VkFormat depthFormat,
        VkShaderModule vertShader, VkShaderModule fragShader, uint32_t maxDescriptorSets);
    void Cleanup();

    void Bind(VkCommandBuffer commandBuffer) const;
    VkPipelineLayout GetLayout() const { return m_pipelineLayout; }

    std::vector<VkDescriptorSet> CreateDescriptorSetsForMaterial(
        const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
        const VulkanTexture& diffuseTexture, const VulkanTexture& metallicRoughnessTexture);

private:
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool(uint32_t maxSets);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};