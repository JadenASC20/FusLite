#pragma once

#include <volk.h>
#include <vector>

struct GLFWwindow;
class VulkanContext;

class ResolvePipeline {
public:
    struct PushConstants {
        float texelSize[2];
        float blendAlpha;
        int   firstFrame;
    };

    ResolvePipeline();
    ~ResolvePipeline();

    // hdrViews / motionViews are per-swapchain-image (indexed by imageIndex).
    // historyViews has exactly 2 entries (the ping-pong pair).
    void Init(VulkanContext& context, GLFWwindow* window, VkFormat hdrFormat,
        VkShaderModule vertShader, VkShaderModule fragShader,
        const std::vector<VkImageView>& hdrViews,
        const std::vector<VkImageView>& motionViews,
        const std::vector<VkImageView>& historyViews);

    // Binds the pipeline and the descriptor set that reads history[historyReadIndex],
    // sampling the current frame's hdr/motion (imageIndex), then pushes constants.
    void Bind(VkCommandBuffer cmd, uint32_t imageIndex, int historyReadIndex,
        const PushConstants& pc) const;

    void Cleanup();

private:
    void CreateSampler();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPoolAndSets(const std::vector<VkImageView>& hdrViews,
        const std::vector<VkImageView>& motionViews,
        const std::vector<VkImageView>& historyViews);

    VkDevice              m_device = VK_NULL_HANDLE;
    VkSampler             m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;

    // m_descriptorSets[imageIndex * 2 + historyReadIndex]
    std::vector<VkDescriptorSet> m_descriptorSets;
    uint32_t                     m_numImages = 0;
};