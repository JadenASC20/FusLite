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

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"


static bool DecodeCgltfImage(VulkanContext& context, const cgltf_data* data,
    const cgltf_image* image, const std::filesystem::path& modelDir,
    bool isColorData, VulkanTexture& outTex)
{
    if (!image) return false;

    // embedded in a buffer view (for .glb)
    if (image->buffer_view) {
        const cgltf_buffer_view* bv = image->buffer_view;
        const uint8_t* base = static_cast<const uint8_t*>(bv->buffer->data);
        const uint8_t* imgData = base + bv->offset;
        // These bytes are an encoded PNG/JPG blob use your memory (compressed) path.
        outTex = context.CreateTextureFromMemory(imgData,
            static_cast<uint32_t>(bv->size), isColorData);
        return true;
    }

    // external / data-URI file referenced by uri
    if (image->uri) {
        // data: URIs would need base64 decode; for this asset (glb) we expect file paths.
        std::filesystem::path fullPath = modelDir / image->uri;
        outTex = context.CreateTexture(fullPath.string().c_str(), isColorData);
        return true;
    }
    return false;
}

VulkanTexture Model::LoadCgltfTexture(VulkanContext& context, const cgltf_data* data,
    const cgltf_texture_view* texView, const std::filesystem::path& modelDir,
    const char* debugLabel, bool isColorData)
{
    if (texView && texView->texture && texView->texture->image) {
        VulkanTexture tex;
        printf("Loading %s texture\n", debugLabel);
        if (DecodeCgltfImage(context, data, texView->texture->image, modelDir, isColorData, tex))
            return tex;
    }

    // Defaults matching your Assimp loader's fallbacks.
    if (std::string(debugLabel) == "metallic-roughness") {
        printf("Material has no %s texture, using default (roughness 1.0, non-metal)\n", debugLabel);
        // R=unused, G=roughness=1, B=metallic=0 scalar drives via multiply
        return context.CreateSolidColorTexture(0.0f, 1.0f, 0.0f, 1.0f, false);
    }
    if (std::string(debugLabel) == "normal") {
        return context.CreateSolidColorTexture(0.5f, 0.5f, 1.0f, 1.0f, false);
    }
    // diffuse fallback: flat white (role table / factors will tint)
    printf("Material has no %s texture, using flat white\n", debugLabel);
    return context.CreateSolidColorTexture(1.0f, 1.0f, 1.0f, 1.0f, true);
}

void Model::LoadFromFile(VulkanContext& context, const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse_file(&options, path.c_str(), &data);
    if (res != cgltf_result_success) {
        throw std::runtime_error("cgltf failed to parse: " + path);
    }
    res = cgltf_load_buffers(&options, data, path.c_str());
    if (res != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cgltf failed to load buffers: " + path);
    }

    std::filesystem::path modelDir = std::filesystem::path(path).parent_path();

    // Materials
    size_t matCount = data->materials_count;
    m_diffuseTextures.resize(matCount);
    m_normalTextures.resize(matCount);
    m_metallicRoughnessTextures.resize(matCount);
    m_materials.resize(matCount);

    for (size_t m = 0; m < matCount; m++) {
        const cgltf_material& mat = data->materials[m];
        const char* matName = mat.name ? mat.name : "";
        printf("Material %zu ('%s'):\n", m, matName);

        const cgltf_pbr_metallic_roughness& pbr = mat.pbr_metallic_roughness;

        // Diffuse / base color (sRGB)
        m_diffuseTextures[m] = LoadCgltfTexture(context, data,
            &pbr.base_color_texture, modelDir, "diffuse", true);

        // Metallic-roughness (linear). glTF packs it as one texture: G=roughness, B=metallic.
        m_metallicRoughnessTextures[m] = LoadCgltfTexture(context, data,
            &pbr.metallic_roughness_texture, modelDir, "metallic-roughness", false);

        // Normal (linear)
        m_normalTextures[m] = LoadCgltfTexture(context, data,
            &mat.normal_texture, modelDir, "normal", false);

        MaterialParams mp;
        mp.name = (matName && matName[0]) ? matName : ("Material " + std::to_string(m));

        auto nameHas = [&](const char* s) { return mp.name.find(s) != std::string::npos; };

        mp.isGlass = (nameHas("Glass") || nameHas("Window")) && !nameHas("Opaque");
        if (mp.isGlass) printf("  -> tagged GLASS: %s\n", mp.name.c_str());
        mp.hasDiffuseTexture = (pbr.base_color_texture.texture != nullptr);
        mp.hasMRTexture = (pbr.metallic_roughness_texture.texture != nullptr);
        mp.hasNormalMap = (mat.normal_texture.texture != nullptr);

        // Seed scalar factors from the asset (role table may overwrite these — keeping role table for now).
        mp.roughness = pbr.roughness_factor;
        mp.metallic = pbr.metallic_factor;
        mp.colorTint = glm::vec3(pbr.base_color_factor[0],
            pbr.base_color_factor[1],
            pbr.base_color_factor[2]);

        // KHR_texture_transform on the NORMAL texture your existing per-material UV transform.
        if (mat.normal_texture.has_transform) {
            const cgltf_texture_transform& tt = mat.normal_texture.transform;
            mp.normalUVScale = glm::vec2(tt.scale[0], tt.scale[1]);
            mp.normalUVOffset = glm::vec2(tt.offset[0], tt.offset[1]);
        }
        else {
            mp.normalUVScale = glm::vec2(1.0f);
            mp.normalUVOffset = glm::vec2(0.0f);
        }

        m_materials[m] = mp;
    }

    // Helper to map a cgltf_material* back to its index in data->materials.
    auto materialIndex = [&](const cgltf_material* mp) -> int {
        if (!mp) return 0;
        return static_cast<int>(mp - data->materials);
    };

    // Geometry (flat: one submesh per primitive, node transforms ignored)
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    for (size_t mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; pi++) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            SubMesh sub{};
            sub.firstIndex = static_cast<uint32_t>(allIndices.size());
            sub.vertexOffset = static_cast<int32_t>(allVertices.size());
            sub.materialIndex = materialIndex(prim.material);

            // Find attribute accessors.
            const cgltf_accessor* posAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
            const cgltf_accessor* nrmAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* uv0Acc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* uv1Acc = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 1);
            const cgltf_accessor* tanAcc = cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0);

            if (!posAcc) continue;
            size_t vcount = posAcc->count;

            // Prefer UV channel 1 when present (matches your Assimp-era heuristic).
            const cgltf_accessor* uvAcc = uv1Acc ? uv1Acc : uv0Acc;

            for (size_t i = 0; i < vcount; i++) {
                Vertex v{};

                float p[3] = { 0,0,0 };
                cgltf_accessor_read_float(posAcc, i, p, 3);
                v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];

                if (nrmAcc) {
                    float n[3] = { 0,1,0 };
                    cgltf_accessor_read_float(nrmAcc, i, n, 3);
                    v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
                }
                else {
                    v.normal[0] = 0; v.normal[1] = 1; v.normal[2] = 0;
                }

                v.color[0] = 1; v.color[1] = 1; v.color[2] = 1;

                if (uvAcc) {
                    float uv[2] = { 0,0 };
                    cgltf_accessor_read_float(uvAcc, i, uv, 2);
                    v.texCoord[0] = uv[0]; v.texCoord[1] = uv[1];
                }
                else {
                    v.texCoord[0] = 0; v.texCoord[1] = 0;
                }

                if (tanAcc) {
                    float t[4] = { 1,0,0,1 };
                    cgltf_accessor_read_float(tanAcc, i, t, 4);
                    // glTF TANGENT is xyz + w handedness — exactly your Vertex.tangent layout.
                    v.tangent[0] = t[0]; v.tangent[1] = t[1];
                    v.tangent[2] = t[2]; v.tangent[3] = t[3];
                }
                else {
                    v.tangent[0] = 1; v.tangent[1] = 0; v.tangent[2] = 0; v.tangent[3] = 1;
                }

                allVertices.push_back(v);
            }

            // Indices (local to this primitive; vertexOffset handles the base).
            if (prim.indices) {
                size_t icount = prim.indices->count;
                for (size_t i = 0; i < icount; i++) {
                    allIndices.push_back(static_cast<uint32_t>(
                        cgltf_accessor_read_index(prim.indices, i)));
                }
            }
            else {
                // Non-indexed: sequential.
                for (size_t i = 0; i < vcount; i++)
                    allIndices.push_back(static_cast<uint32_t>(i));
            }

            sub.indexCount = static_cast<uint32_t>(allIndices.size()) - sub.firstIndex;
            m_subMeshes.push_back(sub);
        }
    }

    cgltf_free(data);

    m_vertexBuffer = context.CreateVertexBuffer(allVertices.data(), sizeof(Vertex) * allVertices.size());
    m_indexBuffer = context.CreateIndexBuffer(allIndices.data(), sizeof(uint32_t) * allIndices.size());
    m_vertexCount = static_cast<uint32_t>(allVertices.size());
    m_triangleCount = static_cast<uint32_t>(allIndices.size() / 3);

    printf("Model loaded (cgltf): %s (%zu submeshes, %zu materials, %u vertices, %u tris)\n",
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

// Shared body with a glass filter. wantGlass=false opaque only; true glass only.
void Model::DrawFilteredImpl(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
    uint32_t imageIndex, RenderParams base, const std::vector<MaterialParams>& mats,
    bool wantGlass) const
{
    VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& sub : m_subMeshes) {
        const MaterialParams& mp =
            (sub.materialIndex >= 0 && sub.materialIndex < static_cast<int>(mats.size()))
            ? mats[sub.materialIndex]
            : mats.front();

        if (mp.isGlass != wantGlass) continue;

        float alpha = mp.isGlass ? 0.25f : 1.0f;
        base.colorTint = glm::vec4(mp.colorTint, alpha);
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
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(RenderParams), &base);
        vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
    }
}

void Model::DrawOpaque(VkCommandBuffer cb, VkPipelineLayout layout, uint32_t imageIndex,
    RenderParams base, const std::vector<MaterialParams>& mats) const
{
    DrawFilteredImpl(cb, layout, imageIndex, base, mats, false);
}

void Model::DrawTransparent(VkCommandBuffer cb, VkPipelineLayout layout, uint32_t imageIndex,
    RenderParams base, const std::vector<MaterialParams>& mats) const
{
    DrawFilteredImpl(cb, layout, imageIndex, base, mats, true);
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