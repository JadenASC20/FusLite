#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include "Buffer.h"
#include "Vertex.h"
#include "VulkanTexture.h"
#include "VulkanContext.h"
#include "RenderParams.h"  
#include "MaterialParams.h"


class VulkanContext;
class GraphicsPipeline;

struct SubMesh
{
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    int      materialIndex;
};

struct cgltf_data;
struct cgltf_texture_view;

class Model
{
public:
    void LoadFromFile(VulkanContext& context, const std::string& path);
    void CreateDescriptorSets(GraphicsPipeline& pipeline,
        const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
        const VulkanContext::IBLTextures& iblTextures, const BufferAndMemory& lightBuffer,
        const BufferAndMemory& clusterLightInfoBuffer, const BufferAndMemory& lightIndexBuffer,
        VkImageView shadowMapView, VkSampler shadowMapSampler, VkSampler shadowCompareSampler, const BufferAndMemory& rampBuffer);

    void DrawOpaque(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t imageIndex,
        RenderParams base, const std::vector<MaterialParams>& mats) const;
    void DrawTransparent(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t imageIndex,
        RenderParams base, const std::vector<MaterialParams>& mats) const;

    void DrawGeometryOnly(VkCommandBuffer commandBuffer) const;
    void Cleanup(VkDevice device);

    // a SceneModel copies these into its own editable vector so two instances of the same model can differ.
    const std::vector<MaterialParams>& GetDefaultMaterials() const { return m_materials; }

    // Stats for the Model Property window.
    uint32_t GetVertexCount() const { return m_vertexCount; }
    uint32_t GetTriangleCount() const { return m_triangleCount; }

private:
    VulkanTexture LoadCgltfTexture(VulkanContext& context,
        const cgltf_texture_view* texView, const std::filesystem::path& modelDir,
        const char* debugLabel, bool isColorData, glm::vec4 fallbackColor);

    void DrawFilteredImpl(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
        uint32_t imageIndex, RenderParams base, const std::vector<MaterialParams>& mats,
        bool wantGlass) const;

    BufferAndMemory m_vertexBuffer;
    BufferAndMemory m_indexBuffer;
    std::vector<SubMesh> m_subMeshes;
    std::vector<VulkanTexture> m_diffuseTextures;
    std::vector<VulkanTexture> m_normalTextures;
    std::vector<VulkanTexture> m_metallicRoughnessTextures;
    std::vector<MaterialParams> m_materials;
    std::vector<std::vector<VkDescriptorSet>> m_descriptorSets;

    uint32_t m_vertexCount = 0;
    uint32_t m_triangleCount = 0;
};