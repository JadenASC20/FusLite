#pragma once
#include <volk.h>
#include <vector>

struct GLFWwindow;
class VulkanContext;

class TonemapPipeline
{
public:
    TonemapPipeline();
    ~TonemapPipeline();

    void Init(VulkanContext& context, GLFWwindow* window, VkFormat swapchainFormat,
        VkShaderModule vertShader, VkShaderModule fragShader,
        const std::vector<VkImageView>& hdrImageViews);
    void Cleanup();

    void Bind(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;

private:
    void CreateDescriptorSetLayout();
    void CreateDescriptorPoolAndSets(const std::vector<VkImageView>& hdrImageViews);
    void CreateSampler();

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets; // one per swapchain image

    VkSampler m_sampler = VK_NULL_HANDLE;
};