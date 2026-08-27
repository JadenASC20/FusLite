#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>

class VulkanContext;
class Model;
struct SceneObject;

class ShadowMap
{
public:
    void Init(VulkanContext& context, uint32_t resolution);
    void Cleanup(VkDevice device);

    // Computes the light's view-projection matrix for a directional light
    // covering a given world-space area centered on the origin.
    static glm::mat4 ComputeLightViewProj(const glm::vec3& lightDir, float extent, float nearZ, float farZ);

    VkImage GetImage() const { return m_depthImage; }
    VkImageView GetImageView() const { return m_depthImageView; }
    VkSampler GetSampler() const { return m_sampler; }
    VkSampler GetCompareSampler() const { return m_compareSampler; }
    VkFormat GetFormat() const { return m_format; }
    uint32_t GetResolution() const { return m_resolution; }

private:
    VkFormat m_format = VK_FORMAT_D32_SFLOAT;
    uint32_t m_resolution = 2048;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkSampler m_compareSampler = VK_NULL_HANDLE;

};