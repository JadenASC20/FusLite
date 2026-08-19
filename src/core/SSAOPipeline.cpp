#include "SSAOPipeline.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <cstdio>

void SSAOPipeline::Init(VulkanContext& context, VkFormat ssaoFormat,
    VkShaderModule fullscreenVert, VkShaderModule ssaoFrag,
    const std::vector<VkImageView>& depthViews,
    const std::vector<VkImageView>& normalViews,
    VkImageView noiseView, VkSampler noiseSampler,
    const BufferAndMemory& kernelBuffer, size_t kernelSize)
{
    m_device = context.GetDevice();
    uint32_t n = (uint32_t)depthViews.size();

    // Nearest-clamp sampler for depth + normal.
    VkSamplerCreateInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_NEAREST; s.minFilter = VK_FILTER_NEAREST;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.minLod = 0.0f; s.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(m_device, &s, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSAO sampler");

    // Descriptor set layout: 0,1,2 = combined image samplers, 3 = uniform buffer.
    VkDescriptorSetLayoutBinding b[4]{};
    for (int i = 0; i < 3; i++) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    b[3].binding = 3;
    b[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[3].descriptorCount = 1;
    b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 4; li.pBindings = b;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSAO set layout");

    // Pool: n sets, each with 3 samplers + 1 UBO.
    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = n * 3;
    ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         ps[1].descriptorCount = n * 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 2; pi.pPoolSizes = ps; pi.maxSets = n;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSAO pool");

    // Allocate + write sets.
    std::vector<VkDescriptorSetLayout> layouts(n, m_setLayout);
    VkDescriptorSetAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    a.descriptorPool = m_pool; a.descriptorSetCount = n; a.pSetLayouts = layouts.data();
    m_sets.resize(n);
    if (vkAllocateDescriptorSets(m_device, &a, m_sets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate SSAO sets");

    for (uint32_t i = 0; i < n; i++) {
        VkDescriptorImageInfo imgs[3]{};
        VkImageView views[3] = { depthViews[i], normalViews[i], noiseView };
        VkSampler samps[3] = { m_sampler, m_sampler, noiseSampler };
        for (int k = 0; k < 3; k++) {
            imgs[k].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgs[k].imageView = views[k];
            imgs[k].sampler = samps[k];
        }
        VkDescriptorBufferInfo kb{};
        kb.buffer = kernelBuffer.buffer; kb.offset = 0;
        kb.range = sizeof(glm::vec4) * kernelSize;

        VkWriteDescriptorSet w[4]{};
        for (int k = 0; k < 3; k++) {
            w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[k].dstSet = m_sets[i]; w[k].dstBinding = k;
            w[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[k].descriptorCount = 1; w[k].pImageInfo = &imgs[k];
        }
        w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[3].dstSet = m_sets[i]; w[3].dstBinding = 3;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[3].descriptorCount = 1; w[3].pBufferInfo = &kb;
        vkUpdateDescriptorSets(m_device, 4, w, 0, nullptr);
    }

    // Pipeline layout.
    VkPushConstantRange pc{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pl, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSAO pipeline layout");

    // Fullscreen pipeline (single R8 color target, no depth, no blend).
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = fullscreenVert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = ssaoFrag; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{}; dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2; dynInfo.pDynamicStates = dyn;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rast{}; rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = VK_CULL_MODE_NONE;
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rast.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{}; cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;   // single channel
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineRenderingCreateInfo rInfo{}; rInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rInfo.colorAttachmentCount = 1; rInfo.pColorAttachmentFormats = &ssaoFormat;

    VkGraphicsPipelineCreateInfo gp{}; gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext = &rInfo; gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
    gp.pRasterizationState = &rast; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.pDynamicState = &dynInfo; gp.layout = m_pipelineLayout; gp.renderPass = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSAO pipeline");

    printf("SSAO pipeline created.\n");
}

void SSAOPipeline::Bind(VkCommandBuffer cmd, uint32_t imageIndex, const SSAOPush& pc) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_sets[imageIndex], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPush), &pc);
}

void SSAOPipeline::Cleanup()
{
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
}