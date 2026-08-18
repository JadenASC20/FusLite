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

// NOTE: added a trailing out-param `foundTexture` so the caller can record whether a
// real texture backed this slot (drives the "show slider only if no map" UI rule)
// without issuing a second GetTexture query.
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
        return context.CreateSolidColorTexture(0.0f, 1.0f, 1.0f, 1.0f, false);
    }

    if (type == aiTextureType_NORMALS) {
        return context.CreateSolidColorTexture(0.5f, 0.5f, 1.0f, 1.0f, false);
    }
    
    // TEMP DIAGNOSTIC: dump every texture slot Assimp sees for this material
    printf("  --- texture slots for '%s' ---\n", material->GetName().C_Str());
    for (int t = aiTextureType_NONE; t <= aiTextureType_TRANSMISSION; t++) {
        unsigned int count = material->GetTextureCount((aiTextureType)t);
        if (count > 0) {
            aiString p;
            material->GetTexture((aiTextureType)t, 0, &p);
            printf("    type %d: count=%u path='%s'\n", t, count, p.C_Str());
        }
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
    m_normalTextures.resize(scene->mNumMaterials);
    m_metallicRoughnessTextures.resize(scene->mNumMaterials);
    m_materials.resize(scene->mNumMaterials);

    for (unsigned int m = 0; m < scene->mNumMaterials; m++) {
        aiMaterial* material = scene->mMaterials[m];
        const char* matName = material->GetName().C_Str();
        printf("Material %u ('%s'):\n", m, matName);
       
        
        // Detect presence of a real texture up front (drives UI + params).
        aiString tmp;
        bool hasBaseColorTex = material->GetTexture(aiTextureType_BASE_COLOR, 0, &tmp) == AI_SUCCESS;
        bool hasDiffuseTex = material->GetTexture(aiTextureType_DIFFUSE, 0, &tmp) == AI_SUCCESS;
        bool hasMetalTex = material->GetTexture(aiTextureType_METALNESS, 0, &tmp) == AI_SUCCESS;
        bool hasRoughTex = material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tmp) == AI_SUCCESS;
        bool hasNormalTex = material->GetTexture(aiTextureType_NORMALS, 0, &tmp) == AI_SUCCESS;

        // Diffuse/base color — sRGB, since it's a visual color
        aiTextureType diffuseType = hasBaseColorTex ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE;
        m_diffuseTextures[m] = LoadMaterialTexture(context, scene, material, diffuseType, modelDir, "diffuse", true);

        // Metallic-roughness — linear data, NOT sRGB
        m_metallicRoughnessTextures[m] = LoadMaterialTexture(context, scene, material,
            aiTextureType_METALNESS, modelDir, "metallic-roughness", false);

        m_normalTextures[m] = LoadMaterialTexture(context, scene, material,
            aiTextureType_NORMALS, modelDir, "normal", false);  // linear, NOT sRGB

        // Seed editable params from the asset.
        MaterialParams mp;
        mp.name = (matName && matName[0]) ? matName : ("Material " + std::to_string(m));
        mp.hasDiffuseTexture = hasBaseColorTex || hasDiffuseTex;
        mp.hasMRTexture = hasMetalTex || hasRoughTex;
        mp.hasNormalMap = hasNormalTex;
        
        // Pull scalar factors so sliders start at the asset's authored values.
        // (These are ignored by the shader where a texture is bound, but give sane
        // starting points for materials that have no map.)
        float rough = 0.5f, metal = 0.0f;
        material->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough);
        material->Get(AI_MATKEY_METALLIC_FACTOR, metal);
        mp.roughness = rough;
        mp.metallic = metal;

        aiUVTransform transform;
        if (material->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_NORMALS, 0), transform) == AI_SUCCESS) {
            mp.normalUVScale = glm::vec2(transform.mScaling.x, transform.mScaling.y);
            mp.normalUVOffset = glm::vec2(transform.mTranslation.x, transform.mTranslation.y);
        }
        else {
            mp.normalUVScale = glm::vec2(1.0f);
            mp.normalUVOffset = glm::vec2(0.0f);
        }

        aiColor4D base(1.0f, 1.0f, 1.0f, 1.0f);
        if (material->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS ||
            material->Get(AI_MATKEY_COLOR_DIFFUSE, base) == AI_SUCCESS) {
            mp.colorTint = glm::vec3(base.r, base.g, base.b);
        }

        printf("  mat %u UVtransform stored: scale(%.1f,%.1f) offset(%.1f,%.1f)\n",
            m, mp.normalUVScale.x, mp.normalUVScale.y, mp.normalUVOffset.x, mp.normalUVOffset.y);

        m_materials[m] = mp;
    }

    // Geometry
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
        const aiMesh* mesh = scene->mMeshes[m];
        printf("Mesh %u: %u UV channels\n", m, mesh->GetNumUVChannels());
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

            v.color[0] = 1.0f; 
            v.color[1] = 1.0f; 
            v.color[2] = 1.0f;

            if (mesh->HasTextureCoords(1)) {
                v.texCoord[0] = mesh->mTextureCoords[1][i].x;
                v.texCoord[1] = mesh->mTextureCoords[1][i].y;
            }
            else if (mesh->HasTextureCoords(0)) {
                v.texCoord[0] = mesh->mTextureCoords[0][i].x;
                v.texCoord[1] = mesh->mTextureCoords[0][i].y;
            }
            else {
                v.texCoord[0] = 0.0f; v.texCoord[1] = 0.0f;
            }

            if (mesh->HasTangentsAndBitangents()) {
                v.tangent[0] = mesh->mTangents[i].x;
                v.tangent[1] = mesh->mTangents[i].y;
                v.tangent[2] = mesh->mTangents[i].z;
                // Handedness: sign of dot(cross(N,T), bitangent). Determines which way B points.
                aiVector3D n = mesh->mNormals[i];
                aiVector3D t = mesh->mTangents[i];
                aiVector3D b = mesh->mBitangents[i];
                aiVector3D nCrossT(
                    n.y * t.z - n.z * t.y,
                    n.z * t.x - n.x * t.z,
                    n.x * t.y - n.y * t.x);
                float handed = (nCrossT.x * b.x + nCrossT.y * b.y + nCrossT.z * b.z) < 0.0f ? -1.0f : 1.0f;
                v.tangent[3] = handed;
            }
            else {
                v.tangent[0] = 1.0f; v.tangent[1] = 0.0f; v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
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

    m_vertexCount = static_cast<uint32_t>(allVertices.size());
    m_triangleCount = static_cast<uint32_t>(allIndices.size() / 3);

    printf("Model loaded: %s (%zu submeshes, %zu materials, %u vertices, %u tris)\n",
        path.c_str(), m_subMeshes.size(), m_materials.size(), m_vertexCount, m_triangleCount);
}

void Model::CreateDescriptorSets(GraphicsPipeline& pipeline,
    const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
    const VulkanContext::IBLTextures& iblTextures, const BufferAndMemory& lightBuffer,
    const BufferAndMemory& clusterLightInfoBuffer, const BufferAndMemory& lightIndexBuffer,
    VkImageView shadowMapView, VkSampler shadowMapSampler, const BufferAndMemory& rampBuffer) {
    m_descriptorSets.resize(m_diffuseTextures.size());
    for (size_t matIdx = 0; matIdx < m_diffuseTextures.size(); matIdx++) {
        m_descriptorSets[matIdx] = pipeline.CreateDescriptorSetsForMaterial(
            uniformBuffers, uniformDataSize, m_diffuseTextures[matIdx], m_metallicRoughnessTextures[matIdx],
            m_normalTextures[matIdx],
            iblTextures.irradiance, iblTextures.prefilteredSpecular, iblTextures.brdfLUT, lightBuffer,
            clusterLightInfoBuffer, lightIndexBuffer, shadowMapView, shadowMapSampler, rampBuffer);
    }
}

void Model::Draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, uint32_t imageIndex,
    RenderParams base, const std::vector<MaterialParams>& mats) const
{
    VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& sub : m_subMeshes) {
        // Guard: submesh material index must be in range of the provided params.
        const MaterialParams& mp =
            (sub.materialIndex >= 0 && sub.materialIndex < static_cast<int>(mats.size()))
            ? mats[sub.materialIndex]
            : mats.front();

        // Overwrite only the per-material fields; leave frame params in `base` intact.
        base.colorTint = glm::vec4(mp.colorTint, 0.0f);
        base.roughness = mp.roughness;
        base.metallic = mp.metallic;
        base.clearcoatFactor = mp.clearcoatFactor;
        base.clearcoatRoughness = mp.clearcoatRoughness;
        base.flakeStrength = mp.flakeStrength;
        base.flakeScale = mp.flakeScale;
        base.normalUVTransform = glm::vec4(
            mp.normalUVScale.x, mp.normalUVScale.y,
            mp.normalUVOffset.x, mp.normalUVOffset.y);


        VkDescriptorSet set = m_descriptorSets[sub.materialIndex][imageIndex];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            0, 1, &set, 0, nullptr);

        // Same push range as GraphicsPipeline::PushParams: FRAGMENT stage, offset 0.
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(RenderParams), &base);

        vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
    }
}

void Model::DrawGeometryOnly(VkCommandBuffer commandBuffer) const
{
    // Depth-only shadow path: already loops all submeshes, no materials needed. Unchanged.
    VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    for (const auto& sub : m_subMeshes) {
        vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
    }
}

void Model::Cleanup(VkDevice device)
{
    m_vertexBuffer.Destroy(device);
    m_indexBuffer.Destroy(device);
    for (auto& tex : m_diffuseTextures) tex.Destroy(device);
    for (auto& tex : m_metallicRoughnessTextures) tex.Destroy(device);
    for (auto& tex : m_normalTextures) tex.Destroy(device);
}