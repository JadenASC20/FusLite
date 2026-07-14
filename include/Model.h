#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <assimp/material.h>
#include "Buffer.h"
#include "Vertex.h"
#include "VulkanTexture.h"

class VulkanContext;
class GraphicsPipeline;
struct aiScene;
struct aiMaterial;

struct SubMesh
{
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    int      materialIndex;
};

class Model
{
public:
    void LoadFromFile(VulkanContext& context, const std::string& path);
    void CreateDescriptorSets(GraphicsPipeline& pipeline,
        const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize);
    void Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t imageIndex) const;
    void Cleanup(VkDevice device);

private:
    VulkanTexture LoadMaterialTexture(VulkanContext& context, const aiScene* scene, aiMaterial* material,
        aiTextureType type, const std::filesystem::path& modelDir, const char* debugLabel, bool isColorData);

    BufferAndMemory m_vertexBuffer;
    BufferAndMemory m_indexBuffer;
    std::vector<SubMesh> m_subMeshes;
    std::vector<VulkanTexture> m_diffuseTextures;
    std::vector<VulkanTexture> m_metallicRoughnessTextures;

    // m_descriptorSets[materialIndex][imageIndex]
    std::vector<std::vector<VkDescriptorSet>> m_descriptorSets;
};