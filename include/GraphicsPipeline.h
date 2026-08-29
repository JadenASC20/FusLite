#pragma once
#include <volk.h>
#include <vector>
#include <RenderParams.h>

struct GLFWwindow;
class VulkanContext;
struct BufferAndMemory;
struct VulkanTexture;

struct MaterialBindings
{
    const VulkanTexture* diffuse = nullptr;
    const VulkanTexture* metallicRoughness = nullptr;
    const VulkanTexture* normal = nullptr;
    const VulkanTexture* irradiance = nullptr;
    const VulkanTexture* prefiltered = nullptr;
    const VulkanTexture* brdfLUT = nullptr;

    VkBuffer lightBuffer = VK_NULL_HANDLE;
    VkBuffer clusterLightInfoBuffer = VK_NULL_HANDLE;
    VkBuffer lightIndexBuffer = VK_NULL_HANDLE;
    VkBuffer rampBuffer = VK_NULL_HANDLE;

    VkImageView shadowMapView = VK_NULL_HANDLE;
    VkSampler shadowMapSampler = VK_NULL_HANDLE;  // linear, binding 9
    VkSampler shadowCompareSampler = VK_NULL_HANDLE;  // compare, binding 12
};

class GraphicsPipeline
{
public:
    GraphicsPipeline();
    ~GraphicsPipeline();

    void Init(VulkanContext& context, GLFWwindow* window,
        VkFormat colorFormat, VkFormat depthFormat,
        VkShaderModule vertShader, VkShaderModule fragShader, 
        uint32_t maxDescriptorSets, VkFormat motionFormat, VkFormat normalFormat,
        VkFormat materialFormat);

    void Cleanup();

    void Bind(VkCommandBuffer commandBuffer) const;
    VkPipelineLayout GetLayout() const { return m_pipelineLayout; }

    std::vector<VkDescriptorSet> CreateDescriptorSetsForMaterial(
        const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
        const MaterialBindings& bindings);

    void PushParams(VkCommandBuffer commandBuffer, const RenderParams& params) const;
    void BindTransparent(VkCommandBuffer cb) const {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_transparentPipeline);
    }
private:
    void CreateDescriptorSetLayout();
    void CreateDescriptorPool(uint32_t maxSets);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    VkPipeline m_transparentPipeline = VK_NULL_HANDLE;

};