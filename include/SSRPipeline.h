#pragma once
#include <volk.h>
#include <vector>
#include <glm/glm.hpp>
class VulkanContext;

class SSRPipeline
{
public:
    struct SSRPush {
        glm::mat4 invProj;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec2 screenSize;
        float nearZ;
        float farZ;
        int   maxSteps;
        float stepSize;
        float thickness;
        int   hizMipCount;
    };

    struct CompPush {
        glm::mat4 invProj;
        glm::mat4 invView;
        glm::vec4 cameraPos;
        float reflectivity;
        float _p0, _p1, _p2;
    };

    void Init(VulkanContext& context, VkFormat ssrFormat, VkFormat hdrFormat,
        VkShaderModule fullscreenVert, VkShaderModule ssrFrag, VkShaderModule compFrag,
        const std::vector<VkImageView>& hdrViews,
        const std::vector<VkImageView>& depthViews,
        const std::vector<VkImageView>& normalViews,
        const std::vector<VkImageView>& ssrViews,
        const std::vector<VkImageView>& ssaoViews,
        VkImageView hizSampleView,
        VkImageView prefilteredCubeView, VkSampler cubeSampler,
        const std::vector<VkImageView>& materialViews);

    void Cleanup();

    void BindSSR(VkCommandBuffer cmd, uint32_t imageIndex, const SSRPush& pc) const;
    void BindComposite(VkCommandBuffer cmd, uint32_t imageIndex, const CompPush& pc) const;

private:
    VkPipeline CreateFullscreenPipeline(VkShaderModule vert, VkShaderModule frag,
        VkFormat colorFormat, VkPipelineLayout layout, uint32_t pushSize);

    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // SSR (3 sampled inputs)
    VkDescriptorSetLayout m_ssrSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_ssrPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ssrPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_ssrSets;

    // Composite (2 sampled inputs)
    VkDescriptorSetLayout m_compSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_compPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_compPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_compSets;

    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};