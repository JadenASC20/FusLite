#include "Model.h"
#include "VulkanContext.h"
#include "GraphicsPipeline.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <stdexcept>
#include <cstdio>
#include <filesystem>

VulkanTexture Model::LoadMaterialTexture(VulkanContext& context, const aiScene* scene, aiMaterial* material,
    aiTextureType type, const std::filesystem::path& modelDir, const char* debugLabel, bool isColorData)
{
    aiString texPath;
    bool found = material->GetTexture(type, 0, &texPath) == AI_SUCCESS;

    if (found) {
        const aiTexture* embeddedTex = scene->GetEmbeddedTexture(texPath.C_Str());

        if (embeddedTex) {
            printf("Loading embedded %s texture: %s\n", debugLabel, texPath.C_Str());

            if (embeddedTex->mHeight == 0) {
                return context.CreateTextureFromMemory(
                    reinterpret_cast<const unsigned char*>(embeddedTex->pcData),
                    embeddedTex->mWidth, isColorData);
            }
            else {
                return context.CreateTextureFromRawRGBA(
                    reinterpret_cast<const unsigned char*>(embeddedTex->pcData),
                    embeddedTex->mWidth, embeddedTex->mHeight, isColorData);
            }
        }
        else {
            std::filesystem::path fullPath = modelDir / texPath.C_Str();
            printf("Loading %s texture: %s\n", debugLabel, fullPath.string().c_str());
            return context.CreateTexture(fullPath.string().c_str(), isColorData);
        }
    }

    // Sensible defaults when the material has no texture for this slot
    if (type == aiTextureType_METALNESS || type == aiTextureType_DIFFUSE_ROUGHNESS) {
        // Neutral metallic-roughness: R=unused, G=roughness=0.5, B=metallic=0.0
        printf("Material has no %s texture, using default (roughness 0.5, non-metal)\n", debugLabel);
        return context.CreateSolidColorTexture(0.0f, 0.5f, 0.0f, 1.0f, false);
    }

    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    material->Get(AI_MATKEY_BASE_COLOR, baseColor);
    material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
    printf("Material has no %s texture, using flat color [%.2f %.2f %.2f]\n",
        debugLabel, baseColor.r, baseColor.g, baseColor.b);
    return context.CreateSolidColorTexture(baseColor.r, baseColor.g, baseColor.b, baseColor.a, true);
}

void Model::LoadFromFile(VulkanContext& context, const std::string& path)
{
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error("Assimp failed to load: " + std::string(importer.GetErrorString()));
    }

    std::filesystem::path modelDir = std::filesystem::path(path).parent_path();

    m_diffuseTextures.resize(scene->mNumMaterials);
    m_metallicRoughnessTextures.resize(scene->mNumMaterials);

    for (unsigned int m = 0; m < scene->mNumMaterials; m++) {
        aiMaterial* material = scene->mMaterials[m];
        printf("Material %u ('%s'):\n", m, material->GetName().C_Str());

        // Diffuse/base color — sRGB, since it's a visual color
        aiString diffusePath;
        aiTextureType diffuseType = material->GetTexture(aiTextureType_BASE_COLOR, 0, &diffusePath) == AI_SUCCESS
            ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE;
        m_diffuseTextures[m] = LoadMaterialTexture(context, scene, material, diffuseType, modelDir, "diffuse", true);

        // Metallic-roughness — linear data, NOT sRGB (it's not a visual color, it's raw values)
        m_metallicRoughnessTextures[m] = LoadMaterialTexture(context, scene, material,
            aiTextureType_METALNESS, modelDir, "metallic-roughness", false);
    }

    // --- Geometry (unchanged from before) ---
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        const aiMesh* mesh = scene->mMeshes[m];

        SubMesh sub{};
        sub.firstIndex = static_cast<uint32_t>(allIndices.size());
        sub.vertexOffset = static_cast<int32_t>(allVertices.size());
        sub.materialIndex = static_cast<int>(mesh->mMaterialIndex);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};
            v.pos[0] = mesh->mVertices[i].x;
            v.pos[1] = mesh->mVertices[i].y;
            v.pos[2] = mesh->mVertices[i].z;

            if (mesh->HasNormals()) {
                v.normal[0] = mesh->mNormals[i].x;
                v.normal[1] = mesh->mNormals[i].y;
                v.normal[2] = mesh->mNormals[i].z;
            }
            else {
                v.normal[0] = 0.0f; v.normal[1] = 1.0f; v.normal[2] = 0.0f;
            }

            v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;

            if (mesh->HasTextureCoords(0)) {
                v.texCoord[0] = mesh->mTextureCoords[0][i].x;
                v.texCoord[1] = mesh->mTextureCoords[0][i].y;
            }
            else {
                v.texCoord[0] = 0.0f; v.texCoord[1] = 0.0f;
            }

            allVertices.push_back(v);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++) {
                allIndices.push_back(face.mIndices[idx]);
            }
        }

        sub.indexCount = static_cast<uint32_t>(allIndices.size()) - sub.firstIndex;
        m_subMeshes.push_back(sub);
    }

    m_vertexBuffer = context.CreateVertexBuffer(allVertices.data(), sizeof(Vertex) * allVertices.size());
    m_indexBuffer = context.CreateIndexBuffer(allIndices.data(), sizeof(uint32_t) * allIndices.size());

    printf("Model loaded: %s (%zu submeshes, %zu materials, %zu vertices, %zu indices)\n",
        path.c_str(), m_subMeshes.size(), m_diffuseTextures.size(), allVertices.size(), allIndices.size());
}

void Model::CreateDescriptorSets(GraphicsPipeline& pipeline,
    const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
    const VulkanContext::IBLTextures& iblTextures, const BufferAndMemory& lightBuffer)
{
    m_descriptorSets.resize(m_diffuseTextures.size());

    for (size_t matIdx = 0; matIdx < m_diffuseTextures.size(); matIdx++) {
        m_descriptorSets[matIdx] = pipeline.CreateDescriptorSetsForMaterial(
            uniformBuffers, uniformDataSize, m_diffuseTextures[matIdx], m_metallicRoughnessTextures[matIdx],
            iblTextures.irradiance, iblTextures.prefilteredSpecular, iblTextures.brdfLUT, lightBuffer);
    }
}

void Model::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t imageIndex) const
{
    VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& sub : m_subMeshes) {
        VkDescriptorSet set = m_descriptorSets[sub.materialIndex][imageIndex];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            0, 1, &set, 0, nullptr);

        vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
    }
}

void Model::Cleanup(VkDevice device)
{
    m_vertexBuffer.Destroy(device);
    m_indexBuffer.Destroy(device);
    for (auto& tex : m_diffuseTextures) tex.Destroy(device);
    for (auto& tex : m_metallicRoughnessTextures) tex.Destroy(device);
}