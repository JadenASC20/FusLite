#include <RenderPass.h>
#include <VulkanContext.h>
#include <Swapchain.h>

#include <stdexcept>
#include <cstdio>

RenderPass::RenderPass() {}
RenderPass::~RenderPass() {}

void RenderPass::Init(VulkanContext& context, const Swapchain& swapchain)
{
    m_context = &context;
    CreateRenderPass(swapchain.GetImageFormat());
    CreateFramebuffers(swapchain);
}

void RenderPass::CreateRenderPass(VkFormat swapchainFormat)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // clear at the start of the pass
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep the result for presenting
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;      // don't care what it was before
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // ready to present when the pass ends

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0; // index into the attachments array below
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // layout DURING the subpass

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_context->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }

    printf("Render pass created.\n");
}

void RenderPass::CreateFramebuffers(const Swapchain& swapchain)
{
    const auto& imageViews = swapchain.GetImageViews();
    VkExtent2D extent = swapchain.GetExtent();

    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_context->GetDevice(), &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

    printf("%zu framebuffer(s) created.\n", m_framebuffers.size());
}

void RenderPass::Cleanup()
{
    if (!m_context) return;

    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_context->GetDevice(), framebuffer, nullptr);
    }
    m_framebuffers.clear();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_context->GetDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}