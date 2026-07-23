#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>
#include "Buffer.h"
#include "VulkanTexture.h"

struct GLFWwindow;
class VulkanContext;

class Skybox
{
public:
    Skybox();
    ~Skybox();

    void Init(VulkanContext& context, GLFWwindow* window, VkFormat colorFormat, VkFormat depthFormat,
        const char* equirectFilename, uint32_t numImages);
    void Update(uint32_t imageIndex, const glm::mat4& viewProjNoTranslate);
    void Draw(VkCommandBuffer commandBuffer, uint32_t imageIndex) const;
    void Cleanup(VkDevice device);

private:
    void CreateDescriptorSetLayout(VkDevice device);
    void CreateDescriptorSets(VkDevice device, uint32_t numImages);
    void CreatePipeline(VulkanContext& context, GLFWwindow* window, VkFormat colorFormat, VkFormat depthFormat);

    VkDevice m_device = VK_NULL_HANDLE;
    VulkanTexture m_cubemap;

    std::vector<BufferAndMemory> m_uniformBuffers;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
};