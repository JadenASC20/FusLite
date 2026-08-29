#include "GraphicsPipeline.h"
#include "VulkanContext.h"
#include "Vertex.h"
#include "Buffer.h"
#include "VulkanTexture.h"
#include "RenderParams.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cstdio>
#include <array>
#include <deque>
#include <algorithm>
#include <vector>


// Binding table : single source of truth for the scene descriptor set.
//
// The set layout, the pool sizes, and the per-write descriptorType are ALL
// derived from this table. Adding a binding means adding one row here plus one
// chained call in CreateDescriptorSetsForMaterial -- nothing else.
//
// Keep this in sync with triangle.frag's layout(binding = N) declarations.

namespace {

    constexpr VkShaderStageFlags kVS = VK_SHADER_STAGE_VERTEX_BIT;
    constexpr VkShaderStageFlags kFS = VK_SHADER_STAGE_FRAGMENT_BIT;

    struct BindingDesc {
        uint32_t           binding;
        VkDescriptorType   type;
        VkShaderStageFlags stages;
        uint32_t           count;
    };

    constexpr BindingDesc kSceneBindings[] = {
        {  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         kVS | kFS, 1 }, // ubo
        {  1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // diffuse
        {  2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // metallicRoughness
        {  3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // irradiance
        {  4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // prefiltered
        {  5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // brdfLUT
        {  6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kFS,       1 }, // lights
        {  7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kFS,       1 }, // clusterLightInfo
        {  8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kFS,       1 }, // lightIndices
        {  9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // shadowMap (linear)
        { 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kFS,       1 }, // ramp
        { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // normal
        { 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFS,       1 }, // shadowCmp
    };

    constexpr uint32_t kSceneBindingCount =
        static_cast<uint32_t>(std::size(kSceneBindings));

    VkDescriptorType TypeOfBinding(uint32_t binding)
    {
        for (const auto& b : kSceneBindings)
            if (b.binding == binding) return b.type;
        throw std::runtime_error("Unknown descriptor binding");
    }

}

// DescriptorWriter
//
// vkUpdateDescriptorSets takes POINTERS to the info structs, so every info must
// stay alive and at a stable address until Update() runs. std::deque never
// invalidates references to existing elements on push_back, unlike vector.
//
// descriptorType is looked up from the binding table rather than retyped at the
// call site, so a write can't silently disagree with the layout -- that class of
// mismatch produces no validation error and shows up as wrong pixels.

class DescriptorWriter {
public:
    DescriptorWriter& Buffer(uint32_t binding, VkBuffer buffer,
        VkDeviceSize range = VK_WHOLE_SIZE,
        VkDeviceSize offset = 0)
    {
        m_bufferInfos.push_back(VkDescriptorBufferInfo{ buffer, offset, range });

        m_writes.push_back(VkWriteDescriptorSet{});
        VkWriteDescriptorSet& w = m_writes.back();
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = TypeOfBinding(binding);
        w.descriptorCount = 1;
        w.pBufferInfo = &m_bufferInfos.back();
        return *this;
    }

    DescriptorWriter& Image(uint32_t binding, VkImageView view, VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        m_imageInfos.push_back(VkDescriptorImageInfo{ sampler, view, layout });

        m_writes.push_back(VkWriteDescriptorSet{});
        VkWriteDescriptorSet& w = m_writes.back();
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstBinding = binding;
        w.dstArrayElement = 0;
        w.descriptorType = TypeOfBinding(binding);
        w.descriptorCount = 1;
        w.pImageInfo = &m_imageInfos.back();
        return *this;
    }

    void Update(VkDevice device, VkDescriptorSet set)
    {
        for (auto& w : m_writes) w.dstSet = set;
        vkUpdateDescriptorSets(device,
            static_cast<uint32_t>(m_writes.size()),
            m_writes.data(), 0, nullptr);
    }

private:
    std::deque<VkDescriptorBufferInfo> m_bufferInfos;
    std::deque<VkDescriptorImageInfo>  m_imageInfos;
    std::vector<VkWriteDescriptorSet>  m_writes;
};

// Blend state factory -- opaque and transparent variants differ only in whether
// blending is enabled, so build one and flip the flag.
VkPipelineColorBlendAttachmentState MakeBlendAttachment(bool alphaBlend)
{
    VkPipelineColorBlendAttachmentState a{};
    a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    a.blendEnable = alphaBlend ? VK_TRUE : VK_FALSE;
    if (alphaBlend) {
        a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        a.colorBlendOp = VK_BLEND_OP_ADD;
        a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        a.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    return a;
}

GraphicsPipeline::GraphicsPipeline() {}
GraphicsPipeline::~GraphicsPipeline() {}

void GraphicsPipeline::CreateDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, kSceneBindingCount> bindings{};
    for (uint32_t i = 0; i < kSceneBindingCount; i++) {
        bindings[i].binding = kSceneBindings[i].binding;
        bindings[i].descriptorType = kSceneBindings[i].type;
        bindings[i].descriptorCount = kSceneBindings[i].count;
        bindings[i].stageFlags = kSceneBindings[i].stages;
        bindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kSceneBindingCount;
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void GraphicsPipeline::CreateDescriptorPool(uint32_t maxSets)
{
   // Accumulate one pool size per distinct descriptor type. Derived counts for
   // the current table: UNIFORM_BUFFER = maxSets * 1,
   // COMBINED_IMAGE_SAMPLER = maxSets * 8, STORAGE_BUFFER = maxSets * 4 
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(4);

    for (const auto& b : kSceneBindings) {
        auto it = std::find_if(poolSizes.begin(), poolSizes.end(),
            [&](const VkDescriptorPoolSize& p) { return p.type == b.type; });
        if (it == poolSizes.end()) {
            poolSizes.push_back(VkDescriptorPoolSize{ b.type, b.count * maxSets });
        }
        else {
            it->descriptorCount += b.count * maxSets;
        }
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

std::vector<VkDescriptorSet> GraphicsPipeline::CreateDescriptorSetsForMaterial(
    const std::vector<BufferAndMemory>& uniformBuffers, size_t uniformDataSize,
    const MaterialBindings& mb)
{
    const uint32_t numImages = static_cast<uint32_t>(uniformBuffers.size());
    std::vector<VkDescriptorSetLayout> layouts(numImages, m_descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = numImages;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> sets(numImages);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    for (uint32_t i = 0; i < numImages; i++) {
        DescriptorWriter w;
        w.Buffer(0, uniformBuffers[i].buffer, uniformDataSize)
            .Image(1, mb.diffuse->view, mb.diffuse->sampler)
            .Image(2, mb.metallicRoughness->view, mb.metallicRoughness->sampler)
            .Image(3, mb.irradiance->view, mb.irradiance->sampler)
            .Image(4, mb.prefiltered->view, mb.prefiltered->sampler)
            .Image(5, mb.brdfLUT->view, mb.brdfLUT->sampler)
            .Buffer(6, mb.lightBuffer)
            .Buffer(7, mb.clusterLightInfoBuffer)
            .Buffer(8, mb.lightIndexBuffer)
            .Image(9, mb.shadowMapView, mb.shadowMapSampler)
            .Buffer(10, mb.rampBuffer)
            .Image(11, mb.normal->view, mb.normal->sampler)
            // Binding 12 is the SAME image view as binding 9, paired with the
            // compare sampler for hardware PCF (sampler2DShadow).
            .Image(12, mb.shadowMapView, mb.shadowCompareSampler)
            .Update(m_device, sets[i]);
    }

    return sets;
}

void GraphicsPipeline::Init(VulkanContext& context, GLFWwindow* window,
    VkFormat colorFormat, VkFormat depthFormat,
    VkShaderModule vertShader, VkShaderModule fragShader,
    uint32_t maxDescriptorSets, VkFormat motionFormat, VkFormat normalFormat,
    VkFormat materialFormat)
{
    m_device = context.GetDevice();

    CreateDescriptorSetLayout();
    CreateDescriptorPool(maxDescriptorSets);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";

    auto bindingDescription = Vertex::GetBindingDescription();
    auto attributeDescriptions = Vertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight) };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.minSampleShading = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    std::array<VkPipelineColorBlendAttachmentState, 4> opaqueBlend;
    opaqueBlend.fill(MakeBlendAttachment(false));

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 4;
    colorBlending.pAttachments = opaqueBlend.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RenderParams);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // Dynamic rendering: describe attachment formats instead of using a VkRenderPass
    VkFormat colorFormats[4] = { colorFormat, motionFormat, normalFormat, materialFormat };

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 4;
    renderingInfo.pColorAttachmentFormats = colorFormats;
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // must be null for dynamic rendering
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    // Transparent variant: depth-write OFF + alpha blending.
    VkPipelineDepthStencilStateCreateInfo depthStencilTransparent = depthStencil;
    depthStencilTransparent.depthWriteEnable = VK_FALSE;

    // Back-face cull for transparents. With CULL_MODE_NONE every glass shell
    // blends twice (front and back face) and the veil compounds per layer.
    // If the glass vanishes entirely, the asset's winding is reversed --
    // switch to VK_CULL_MODE_FRONT_BIT.
    VkPipelineRasterizationStateCreateInfo rasterizerTransparent = rasterizer;
    rasterizerTransparent.cullMode = VK_CULL_MODE_BACK_BIT;

    std::array<VkPipelineColorBlendAttachmentState, 4> transparentBlend;
    transparentBlend.fill(MakeBlendAttachment(false));
    transparentBlend[0] = MakeBlendAttachment(true);

    VkPipelineColorBlendStateCreateInfo colorBlendingTransparent = colorBlending;
    colorBlendingTransparent.pAttachments = transparentBlend.data(); // attachmentCount stays 4

    VkGraphicsPipelineCreateInfo transparentInfo = pipelineInfo;
    transparentInfo.pDepthStencilState = &depthStencilTransparent;
    transparentInfo.pRasterizationState = &rasterizerTransparent;
    transparentInfo.pColorBlendState = &colorBlendingTransparent;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &transparentInfo, nullptr, &m_transparentPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transparent graphics pipeline");
    }
    printf("Transparent pipeline: cullMode=%u  blendEnable=[%d %d %d %d]  depthWrite=%d\n",
        rasterizerTransparent.cullMode,
        transparentBlend[0].blendEnable, transparentBlend[1].blendEnable,
        transparentBlend[2].blendEnable, transparentBlend[3].blendEnable,
        depthStencilTransparent.depthWriteEnable);
    printf("Graphics pipeline created (dynamic rendering).\n");
}

void GraphicsPipeline::Bind(VkCommandBuffer commandBuffer) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void GraphicsPipeline::Cleanup()
{
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_transparentPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_transparentPipeline, nullptr);
        m_transparentPipeline = VK_NULL_HANDLE;
    }
}

void GraphicsPipeline::PushParams(VkCommandBuffer commandBuffer, const RenderParams& params) const
{
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(RenderParams), &params);
}