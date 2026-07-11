#include "Swapchain.h"
#include "VulkanContext.h"

#include <stdexcept>
#include <algorithm>
#include <cstdio>

Swapchain::Swapchain() {}

Swapchain::~Swapchain()
{
    // Cleanup() should be called explicitly before destruction (needs a valid
    // VkDevice, which we don't own) — this destructor is a safety net only.
}

void Swapchain::Init(VulkanContext& context, uint32_t windowWidth, uint32_t windowHeight)
{
    m_context = &context;

    const PhysicalDeviceInfo& selected = context.GetSelectedDevice();
    const VkSurfaceCapabilitiesKHR& surfaceCaps = selected.surfaceCapabilities;

    uint32_t numImages = ChooseNumImages(surfaceCaps);
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(selected.surfaceFormats);
    VkPresentModeKHR presentMode = ChoosePresentMode(selected.presentModes);
    VkExtent2D extent = ChooseExtent(surfaceCaps, windowWidth, windowHeight);

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;

    const QueueFamilyIndices& indices = selected.queueFamilyIndices;
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context.GetSurface();
    createInfo.minImageCount = numImages;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (indices.graphicsFamily.value() != indices.presentFamily.value()) {
        // Graphics and present are different queue families — images need to
        // be shared across both without explicit ownership transfers.
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        // Same family (the common case on NVIDIA) — exclusive is more efficient,
        // no ownership transfer logic needed.
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = surfaceCaps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(context.GetDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    // Vulkan may create more images than we requested (minImageCount is a
    // minimum, not exact) — query the real count before fetching handles.
    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(context.GetDevice(), m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(context.GetDevice(), m_swapchain, &actualImageCount, m_images.data());

    CreateImageViews();

    printf("Swapchain created: %u images, %ux%u, format %d\n",
        actualImageCount, extent.width, extent.height, static_cast<int>(m_imageFormat));
}

uint32_t Swapchain::ChooseNumImages(const VkSurfaceCapabilitiesKHR& capabilities)
{
    // One extra than the minimum avoids waiting on the driver in some cases
    // (standard double/triple-buffering recommendation from the spec).
    uint32_t requestedImages = capabilities.minImageCount + 1;

    // maxImageCount == 0 means "no upper limit"
    if (capabilities.maxImageCount > 0 && requestedImages > capabilities.maxImageCount) {
        requestedImages = capabilities.maxImageCount;
    }

    return requestedImages;
}

VkSurfaceFormatKHR Swapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // Fallback: just take whatever's first if our preferred format isn't available
    return availableFormats[0];
}

VkPresentModeKHR Swapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            // Mailbox = triple buffering, lowest latency without tearing
            return mode;
        }
    }

    // FIFO is guaranteed to always be available (standard vsync behavior)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t windowWidth, uint32_t windowHeight)
{
    // If currentExtent isn't the "special" max-uint value, the surface size
    // is fixed and we must match it exactly.
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    // Otherwise we're free to choose, clamped to what the surface allows
    VkExtent2D actualExtent = { windowWidth, windowHeight };

    actualExtent.width = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

void Swapchain::CreateImageViews()
{
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_imageFormat;

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &createInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }
}

void Swapchain::Cleanup()
{
    if (!m_context) return;

    for (auto imageView : m_imageViews) {
        vkDestroyImageView(m_context->GetDevice(), imageView, nullptr);
    }
    m_imageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_context->GetDevice(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}