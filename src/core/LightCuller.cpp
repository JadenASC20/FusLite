#include "LightCuller.h"
#include "VulkanContext.h"
#include "ShaderModule.h"

#include <stdexcept>
#include <cstdio>
#include <cstring>

struct CullPushConstants
{
    glm::mat4 view;
    int gridX;
    int gridY;
    int gridZ;
    int numLights;
};

void LightCuller::Init(VulkanContext& context, const BufferAndMemory& clusterBuffer, const BufferAndMemory& lightBuffer)
{
    VkDevice device = context.GetDevice();

    m_clusterLightInfoBuffer = context.CreateStorageBuffer(sizeof(ClusterLightInfo) * NUM_CLUSTERS);
    m_lightIndexBuffer = context.CreateStorageBuffer(sizeof(uint32_t) * MAX_LIGHT_INDICES);
    m_globalCounterBuffer = context.CreateStorageBuffer(sizeof(uint32_t));

    VkDescriptorSetLayoutBinding bindings[5]{};
    for (int i = 0; i < 5; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culler descriptor set layout");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culler descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate light culler descriptor set");
    }

    VkDescriptorBufferInfo bufferInfos[5];
    bufferInfos[0] = { clusterBuffer.buffer, 0, VK_WHOLE_SIZE };
    bufferInfos[1] = { lightBuffer.buffer, 0, VK_WHOLE_SIZE };
    bufferInfos[2] = { m_clusterLightInfoBuffer.buffer, 0, VK_WHOLE_SIZE };
    bufferInfos[3] = { m_lightIndexBuffer.buffer, 0, VK_WHOLE_SIZE };
    bufferInfos[4] = { m_globalCounterBuffer.buffer, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet writes[5]{};
    for (int i = 0; i < 5; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(CullPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culler pipeline layout");
    }

    VkShaderModule computeShader = CreateShaderModuleFromBinary(device, "shaders/cull_lights.comp.spv");

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeShader;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culler pipeline");
    }

    vkDestroyShaderModule(device, computeShader, nullptr);

    printf("Light culler initialized.\n");
}

void LightCuller::CullLights(VulkanContext& context, const glm::mat4& view, int numLights)
{
    VkDevice device = context.GetDevice();

    // Reset the global atomic counter to 0 before each culling pass
    void* counterData;
    vkMapMemory(device, m_globalCounterBuffer.memory, 0, sizeof(uint32_t), 0, &counterData);
    uint32_t zero = 0;
    memcpy(counterData, &zero, sizeof(uint32_t));
    vkUnmapMemory(device, m_globalCounterBuffer.memory);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = context.GetCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
        0, 1, &m_descriptorSet, 0, nullptr);

    CullPushConstants pc{};
    pc.view = view;
    pc.gridX = CLUSTER_GRID_X;
    pc.gridY = CLUSTER_GRID_Y;
    pc.gridZ = CLUSTER_GRID_Z;
    pc.numLights = numLights;

    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPushConstants), &pc);

    vkCmdDispatch(cmd, 1, 1, CLUSTER_GRID_Z);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkQueue graphicsQueue = context.GetQueue()->GetHandle();
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, context.GetCommandPool(), 1, &cmd);
}

void LightCuller::Cleanup(VkDevice device)
{
    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    m_clusterLightInfoBuffer.Destroy(device);
    m_lightIndexBuffer.Destroy(device);
    m_globalCounterBuffer.Destroy(device);
}