#include "ResolvePipeline.h"
#include "VulkanContext.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdio>

ResolvePipeline::ResolvePipeline() {}
ResolvePipeline::~ResolvePipeline() {}

void ResolvePipeline::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create resolve sampler");
    }
}

void ResolvePipeline::CreateDescriptorSetLayout()
{
    // bindings 0..2 all COMBINED_IMAGE_SAMPLER, fragment stage.
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create resolve descriptor set layout");
    }
}

void ResolvePipeline::CreateDescriptorPoolAndSets(const std::vector<VkImageView>& hdrViews,
    const std::vector<VkImageView>& motionViews,
    const std::vector<VkImageView>& historyViews)
{
    m_numImages = static_cast<uint32_t>(hdrViews.size());
    const uint32_t setCount = m_numImages * 2;              // (imageIndex, historyReadIndex)

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = setCount * 3;                // 3 samplers per set

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = setCount;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create resolve descriptor pool");
    }

    std::vector<VkDescriptorSetLayout> layouts(setCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(setCount);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate resolve descriptor sets");
    }

    for (uint32_t img = 0; img < m_numImages; img++) {
        for (uint32_t hist = 0; hist < 2; hist++) {
            uint32_t setIdx = img * 2 + hist;

            VkDescriptorImageInfo curInfo{};
            curInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            curInfo.imageView = hdrViews[img];
            curInfo.sampler = m_sampler;

            VkDescriptorImageInfo motInfo{};
            motInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            motInfo.imageView = motionViews[img];
            motInfo.sampler = m_sampler;

            VkDescriptorImageInfo hisInfo{};
            hisInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            hisInfo.imageView = historyViews[hist];
            hisInfo.sampler = m_sampler;

            VkWriteDescriptorSet writes[3]{};
            VkDescriptorImageInfo* infos[3] = { &curInfo, &motInfo, &hisInfo };
            for (uint32_t b = 0; b < 3; b++) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = m_descriptorSets[setIdx];
                writes[b].dstBinding = b;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[b].descriptorCount = 1;
                writes[b].pImageInfo = infos[b];
            }
            vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
        }
    }
}

void ResolvePipeline::Init(VulkanContext& context, GLFWwindow* window, VkFormat hdrFormat,
    VkShaderModule vertShader, VkShaderModule fragShader,
    const std::vector<VkImageView>& hdrViews,
    const std::vector<VkImageView>& motionViews,
    const std::vector<VkImageView>& historyViews)
{
    m_device = context.GetDevice();
    CreateSampler();
    CreateDescriptorSetLayout();
    CreateDescriptorPoolAndSets(hdrViews, motionViews, historyViews);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    VkViewport viewport{};
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
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

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create resolve pipeline layout");
    }

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &hdrFormat;   // resolve writes into HDR-format history

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
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create resolve pipeline");
    }
    printf("Resolve (TAA) pipeline created.\n");
}

void ResolvePipeline::Bind(VkCommandBuffer cmd, uint32_t imageIndex, int historyReadIndex,
    const PushConstants& pc) const
{
    uint32_t setIdx = imageIndex * 2u + static_cast<uint32_t>(historyReadIndex & 1);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
        0, 1, &m_descriptorSets[setIdx], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushConstants), &pc);
}

void ResolvePipeline::Cleanup()
{
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
}