#include "HiZPipeline.h"
#include "VulkanContext.h"
#include "GpuLabel.h"
#include <stdexcept>
#include <cstdio>
#include <algorithm>

void HiZPipeline::Init(VulkanContext& context, VkShaderModule comp, VkSampler sampler,
    const std::vector<VkImageView>& depthViews,
    const std::vector<VkImageView>& hizMipViews,
    uint32_t mipLevels)
{
    m_device = context.GetDevice();
    m_sampler = sampler;
    m_mipLevels = mipLevels;
    uint32_t nImg = (uint32_t)depthViews.size();

    // Set layout: binding 0 = sampled source, binding 1 = storage dest.
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding = 1; b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[1].descriptorCount = 1; b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2; li.pBindings = b;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z set layout");

    // Pool: (nImg * mipLevels) sets, each with 1 sampler + 1 storage image.
    uint32_t totalSets = nImg * mipLevels;
    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[0].descriptorCount = totalSets;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          ps[1].descriptorCount = totalSets;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 2; pi.pPoolSizes = ps; pi.maxSets = totalSets;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z pool");

    // Pipeline layout + pipeline.
    VkPushConstantRange pc{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZPush) };
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z pipeline layout");
    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = comp; cpi.stage.pName = "main";
    cpi.layout = m_pipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Hi-Z pipeline");

    // Allocate + write sets. m_sets[img][mip].
    m_sets.resize(nImg);
    for (uint32_t img = 0; img < nImg; img++) {
        m_sets[img].resize(mipLevels);
        std::vector<VkDescriptorSetLayout> layouts(mipLevels, m_setLayout);
        VkDescriptorSetAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        a.descriptorPool = m_pool; a.descriptorSetCount = mipLevels; a.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(m_device, &a, m_sets[img].data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Hi-Z sets");

        for (uint32_t m = 0; m < mipLevels; m++) {
            // Source: mip 0 reads depth (per image); mip m>0 reads hiz mip m-1 (single image).
            VkDescriptorImageInfo src{};
            src.sampler = m_sampler;
            src.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            src.imageView = (m == 0) ? depthViews[img] : hizMipViews[m - 1];

            // Dest: this mip as storage image (GENERAL layout).
            VkDescriptorImageInfo dst{};
            dst.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            dst.imageView = hizMipViews[m];

            VkWriteDescriptorSet w[2]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = m_sets[img][m]; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].descriptorCount = 1; w[0].pImageInfo = &src;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = m_sets[img][m]; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w[1].descriptorCount = 1; w[1].pImageInfo = &dst;
            vkUpdateDescriptorSets(m_device, 2, w, 0, nullptr);
        }
    }
    printf("Hi-Z compute pipeline created (%u mips, %u images).\n", mipLevels, nImg);
}

void HiZPipeline::Build(VkCommandBuffer cmd, uint32_t imageIndex, VkImage hizImage,
    VkExtent2D baseExtent, uint32_t mipLevels,
    float nearZ, float farZ) const
{

    GpuLabel _lbl(cmd, "Hi-Z Build", 0.2f, 0.6f, 0.9f);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);

    for (uint32_t m = 0; m < mipLevels; m++) {
        uint32_t w = std::max(1u, baseExtent.width >> m);
        uint32_t h = std::max(1u, baseExtent.height >> m);

        {
            VkImageMemoryBarrier toGeneral{};
            toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGeneral.image = hizImage;
            toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1 };
            toGeneral.srcAccessMask = 0;
            toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toGeneral);
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_pipelineLayout, 0, 1, &m_sets[imageIndex][m], 0, nullptr);

        HiZPush pc{};
        pc.dstSize = glm::ivec2((int)w, (int)h);
        pc.mode = (m == 0) ? 0 : 1;
        pc.nearZ = nearZ; pc.farZ = farZ;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(HiZPush), &pc);

        uint32_t gx = (w + 7) / 8;
        uint32_t gy = (h + 7) / 8;
        vkCmdDispatch(cmd, gx, gy, 1);

        // This mip: GENERAL -> SHADER_READ so the NEXT mip can sample it,
        // and so the march can sample the whole chain after
        {
            VkImageMemoryBarrier toRead{};
            toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.image = hizImage;
            toRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1 };
            toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toRead);
        }
    }
}

void HiZPipeline::Cleanup()
{
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
}