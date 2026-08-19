#include "SSRPipeline.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <cstdio>

static VkDescriptorSetLayout MakeSampledLayout(VkDevice device, uint32_t count)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings(count);
    for (uint32_t i = 0; i < count; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = count;
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSR/composite set layout");
    return layout;
}

VkPipeline SSRPipeline::CreateFullscreenPipeline(VkShaderModule vert, VkShaderModule frag,
    VkFormat colorFormat, VkPipelineLayout layout, uint32_t)
{
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2; dynInfo.pDynamicStates = dyn;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = VK_CULL_MODE_NONE;
    rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineRenderingCreateInfo rInfo{};
    rInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rInfo.colorAttachmentCount = 1; rInfo.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext = &rInfo; gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rast;
    gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.pDynamicState = &dynInfo; gp.layout = layout; gp.renderPass = VK_NULL_HANDLE;

    VkPipeline pipe;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSR/composite pipeline");
    return pipe;
}

void SSRPipeline::Init(VulkanContext& context, VkFormat ssrFormat, VkFormat hdrFormat,
    VkShaderModule fullscreenVert, VkShaderModule ssrFrag, VkShaderModule compFrag,
    const std::vector<VkImageView>& hdrViews,
    const std::vector<VkImageView>& depthViews,
    const std::vector<VkImageView>& normalViews,
    const std::vector<VkImageView>& ssrViews,
    const std::vector<VkImageView>& ssaoViews,
    VkImageView hizSampleView,
    VkImageView prefilteredCubeView, VkSampler cubeSampler,
    const std::vector<VkImageView>& materialViews)
{
    m_device = context.GetDevice();
    uint32_t n = (uint32_t)hdrViews.size();

    // Nearest-clamp sampler (nearest for depth correctness; SSR reconstructs from raw depth)
    VkSamplerCreateInfo s{};
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = VK_FILTER_NEAREST; 
    s.minFilter = VK_FILTER_NEAREST;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.minLod = 0.0f;
    s.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(m_device, &s, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSR sampler");

    // hdr, depth, normal, hiz, material
    m_ssrSetLayout = MakeSampledLayout(m_device, 5); 

    m_compSetLayout = MakeSampledLayout(m_device, 7); // hdr, ssr, normal, depth, cube, material, ao

    // Pool: sampled descriptors per swapchain image
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = n * 12; // (5 SSR + 7 Composite)
    VkDescriptorPoolCreateInfo pInfo{};
    pInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pInfo.poolSizeCount = 1; 
    pInfo.pPoolSizes = &ps; 
    pInfo.maxSets = n * 2;
    if (vkCreateDescriptorPool(m_device, &pInfo, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSR pool");

    // Allocate + write SSR sets (hdr, depth, normal, hiz)
    // SSR sets — 4 bindings (hdr, depth, normal, hiz)
    {
        std::vector<VkDescriptorSetLayout> layouts(n, m_ssrSetLayout);
        VkDescriptorSetAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        a.descriptorPool = m_pool; 
        a.descriptorSetCount = n; 
        a.pSetLayouts = layouts.data();
        m_ssrSets.resize(n);
        
        if (vkAllocateDescriptorSets(m_device, &a, m_ssrSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate SSR sets");
        
        for (uint32_t i = 0; i < n; i++) {
            VkDescriptorImageInfo imgs[5]{};                                    // was 4
            VkImageView views[5] = { hdrViews[i], depthViews[i],               // was 4
                                     normalViews[i], hizSampleView,
                                     materialViews[i] };                        // NEW binding 4
            VkWriteDescriptorSet w[5]{};                                        // was 4

            for (int b = 0; b < 5; b++) {                                       // was 4
                imgs[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgs[b].imageView = views[b];
                imgs[b].sampler = m_sampler;
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = m_ssrSets[i];
                w[b].dstBinding = b;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[b].descriptorCount = 1;
                w[b].pImageInfo = &imgs[b];
            }
            vkUpdateDescriptorSets(m_device, 5, w, 0, nullptr);                 // was 4
        }
    }

    // Composite set — 6 bindings (hdr, ssr, normal, depth, cube, material)
    {
        std::vector<VkDescriptorSetLayout> layouts(n, m_compSetLayout);
        VkDescriptorSetAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        a.descriptorPool = m_pool; 
        a.descriptorSetCount = n; 
        a.pSetLayouts = layouts.data();
        m_compSets.resize(n);
        
        if (vkAllocateDescriptorSets(m_device, &a, m_compSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate composite sets");
        
        for (uint32_t i = 0; i < n; i++) {
            VkDescriptorImageInfo imgs[7]{};
            VkImageView views[7] = { hdrViews[i], ssrViews[i], normalViews[i], depthViews[i],
                                     prefilteredCubeView, materialViews[i], ssaoViews[i]};
            VkSampler samps[7] = { m_sampler, m_sampler, m_sampler, m_sampler, cubeSampler, m_sampler, m_sampler };
            VkWriteDescriptorSet w[7]{};
            
            for (int b = 0; b < 7; b++) {
                imgs[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgs[b].imageView = views[b]; 
                imgs[b].sampler = samps[b];
                w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[b].dstSet = m_compSets[i];
                w[b].dstBinding = b;
                w[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[b].descriptorCount = 1; 
                w[b].pImageInfo = &imgs[b];
            }
            vkUpdateDescriptorSets(m_device, 7, w, 0, nullptr);
        }
    }


    // Pipeline layouts (push constants)
    VkPushConstantRange ssrPC{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSRPush) };
    VkPipelineLayoutCreateInfo l1{};
    l1.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    l1.setLayoutCount = 1; l1.pSetLayouts = &m_ssrSetLayout;
    l1.pushConstantRangeCount = 1; l1.pPushConstantRanges = &ssrPC;
    if (vkCreatePipelineLayout(m_device, &l1, nullptr, &m_ssrPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create SSR pipeline layout");

    VkPushConstantRange compPC{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompPush) };
    VkPipelineLayoutCreateInfo l2{};
    l2.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    l2.setLayoutCount = 1; l2.pSetLayouts = &m_compSetLayout;
    l2.pushConstantRangeCount = 1; l2.pPushConstantRanges = &compPC;
    if (vkCreatePipelineLayout(m_device, &l2, nullptr, &m_compPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create composite pipeline layout");

    m_ssrPipeline = CreateFullscreenPipeline(fullscreenVert, ssrFrag, ssrFormat, m_ssrPipelineLayout, sizeof(SSRPush));
    m_compPipeline = CreateFullscreenPipeline(fullscreenVert, compFrag, hdrFormat, m_compPipelineLayout, sizeof(CompPush));
    printf("SSR + composite pipelines created.\n");
}

void SSRPipeline::BindSSR(VkCommandBuffer cmd, uint32_t imageIndex, const SSRPush& pc) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssrPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssrPipelineLayout, 0, 1, &m_ssrSets[imageIndex], 0, nullptr);
    vkCmdPushConstants(cmd, m_ssrPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSRPush), &pc);
}

void SSRPipeline::BindComposite(VkCommandBuffer cmd, uint32_t imageIndex, const CompPush& pc) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compPipelineLayout, 0, 1, &m_compSets[imageIndex], 0, nullptr);
    vkCmdPushConstants(cmd, m_compPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompPush), &pc);
}

void SSRPipeline::Cleanup()
{
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_ssrPipeline) vkDestroyPipeline(m_device, m_ssrPipeline, nullptr);
    if (m_compPipeline) vkDestroyPipeline(m_device, m_compPipeline, nullptr);
    if (m_ssrPipelineLayout) vkDestroyPipelineLayout(m_device, m_ssrPipelineLayout, nullptr);
    if (m_compPipelineLayout) vkDestroyPipelineLayout(m_device, m_compPipelineLayout, nullptr);
    if (m_ssrSetLayout) vkDestroyDescriptorSetLayout(m_device, m_ssrSetLayout, nullptr);
    if (m_compSetLayout) vkDestroyDescriptorSetLayout(m_device, m_compSetLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
}