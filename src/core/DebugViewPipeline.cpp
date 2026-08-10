#include "DebugViewPipeline.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <cstdio>

void DebugViewPipeline::Init(VulkanContext& context, VkFormat swapchainFormat,
    VkShaderModule fullscreenVert, VkShaderModule debugFrag)
{
    m_device = context.GetDevice();

    // Own nearest-clamp sampler (nearest so depth isn't interpolated across the nonlinearity)
    VkSamplerCreateInfo sInfo{};
    sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sInfo.magFilter = VK_FILTER_NEAREST;
    sInfo.minFilter = VK_FILTER_NEAREST;
    sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &sInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug view sampler");

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo slInfo{};
    slInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slInfo.bindingCount = 1;
    slInfo.pBindings = &b;
    if (vkCreateDescriptorSetLayout(m_device, &slInfo, nullptr, &m_setLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug view set layout");

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pInfo{};
    pInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pInfo.poolSizeCount = 1;
    pInfo.pPoolSizes = &ps;
    pInfo.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &pInfo, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug view pool");

    VkDescriptorSetAllocateInfo aInfo{};
    aInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    aInfo.descriptorPool = m_pool;
    aInfo.descriptorSetCount = 1;
    aInfo.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_device, &aInfo, &m_set) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate debug view set");

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_setLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug view pipeline layout");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = fullscreenVert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = debugFrag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = dyn;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode = VK_CULL_MODE_NONE;
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPipelineRenderingCreateInfo rInfo{};
    rInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rInfo.colorAttachmentCount = 1;
    rInfo.pColorAttachmentFormats = &swapchainFormat;

    VkGraphicsPipelineCreateInfo gpInfo{};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.pNext = &rInfo;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vi;
    gpInfo.pInputAssemblyState = &ia;
    gpInfo.pViewportState = &vp;
    gpInfo.pRasterizationState = &rast;
    gpInfo.pMultisampleState = &ms;
    gpInfo.pColorBlendState = &cb;
    gpInfo.pDynamicState = &dynInfo;
    gpInfo.layout = m_layout;
    gpInfo.renderPass = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create debug view pipeline");

    printf("Debug view pipeline created.\n");
}

void DebugViewPipeline::Bind(VkCommandBuffer cmd, VkImageView view, const PushConstants& pc)
{
    VkDescriptorImageInfo ii{};
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ii.imageView = view;
    ii.sampler = m_sampler;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m_set;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_set, 0, nullptr);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void DebugViewPipeline::Cleanup()
{
    if (m_sampler)   vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_pool)      vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_pipeline)  vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout)    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
}