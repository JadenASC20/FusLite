#define VOLK_IMPLEMENTATION
#include "VulkanContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <string> 
#include <stdexcept>
#include <map>
#include <set>
#include <stb_image.h>
#include <VulkanTexture.h>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

// Debug callback — this is what actually prints validation layer messages
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[Validation] %s\n", pCallbackData->pMessage);
    }
    return VK_FALSE; // don't abort the call that triggered this
}

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext()
{
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void VulkanContext::Init(const char* pAppName, GLFWwindow* window)
{
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize volk");
    }

    CreateInstance(pAppName);
    volkLoadInstance(m_instance);
    SetupDebugMessenger();
    CreateSurface(window);
    EnumeratePhysicalDevices();
    SelectPhysicalDevice();
    CreateLogicalDevice();
    CreateCommandPool();

    printf("VulkanContext initialized successfully.\n");
}

bool VulkanContext::CheckValidationLayerSupport()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_validationLayers) {
        bool found = false;
        for (const auto& layerProps : availableLayers) {
            if (strcmp(layerName, layerProps.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

std::vector<const char*> VulkanContext::GetRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return extensions;
}

void VulkanContext::CreateInstance(const char* pAppName)
{
    if (m_enableValidationLayers && !CheckValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested but not available. "
            "Make sure the Vulkan SDK is installed correctly.");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = pAppName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "FusLite";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = GetRequiredExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Debug messenger info attached to pNext lets validation catch
    // errors during vkCreateInstance/vkDestroyInstance themselves
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;

        createInfo.pNext = &debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance (VkResult: " +
            std::to_string(result) + ")");
    }
}

void VulkanContext::SetupDebugMessenger()
{
    if (!m_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger");
    }
}

void VulkanContext::CreateSurface(GLFWwindow* window)
{
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }
}

QueueFamilyIndices VulkanContext::FindQueueFamilies(VkPhysicalDevice device, const std::vector<VkQueueFamilyProperties>& queueFamilies)
{
    QueueFamilyIndices indices;

    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.IsComplete()) {
            break;
        }
    }

    return indices;
}

PhysicalDeviceInfo VulkanContext::QueryPhysicalDevice(VkPhysicalDevice device)
{
    PhysicalDeviceInfo info{};
    info.device = device;

    vkGetPhysicalDeviceProperties(device, &info.properties);
    vkGetPhysicalDeviceFeatures(device, &info.features);
    vkGetPhysicalDeviceMemoryProperties(device, &info.memoryProperties);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    info.queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, info.queueFamilyProperties.data());

    info.queueFamilyIndices = FindQueueFamilies(device, info.queueFamilyProperties);

    // Swapchain support — cached now for use during swapchain creation later
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &info.surfaceCapabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount > 0) {
        info.surfaceFormats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, info.surfaceFormats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        info.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, info.presentModes.data());
    }

    return info;
}

void VulkanContext::EnumeratePhysicalDevices()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find a GPU with Vulkan support");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    m_availableDevices.reserve(deviceCount);
    for (const auto& device : devices) {
        m_availableDevices.push_back(QueryPhysicalDevice(device));
    }

    printf("Found %u Vulkan-capable device(s):\n", deviceCount);
    for (const auto& info : m_availableDevices) {
        printf("  - %s%s\n", info.properties.deviceName, info.IsSuitable() ? "" : " (not suitable)");
    }
}

int VulkanContext::RateDeviceSuitability(const PhysicalDeviceInfo& info)
{
    if (!info.IsSuitable()) {
        return 0; // disqualified — no complete queue support or no swapchain support
    }

    int score = 0;

    if (info.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    score += info.properties.limits.maxImageDimension2D;

    if (!info.features.geometryShader) {
        score -= 100;
    }

    return score;
}

void VulkanContext::SelectPhysicalDevice()
{
    std::multimap<int, int> candidates; // score -> index into m_availableDevices

    for (int i = 0; i < static_cast<int>(m_availableDevices.size()); i++) {
        int score = RateDeviceSuitability(m_availableDevices[i]);
        candidates.insert(std::make_pair(score, i));
    }

    if (candidates.rbegin()->first > 0) {
        m_selectedDeviceIndex = candidates.rbegin()->second;
    }
    else {
        throw std::runtime_error("Failed to find a suitable GPU");
    }

    const auto& selected = m_availableDevices[m_selectedDeviceIndex];
    printf("Selected GPU: %s\n", selected.properties.deviceName);
    printf("Graphics queue family index: %u\n", selected.queueFamilyIndices.graphicsFamily.value());
    printf("Present queue family index: %u\n", selected.queueFamilyIndices.presentFamily.value());
}

void VulkanContext::CreateLogicalDevice()
{
    const auto& selected = m_availableDevices[m_selectedDeviceIndex];
    const QueueFamilyIndices& indices = selected.queueFamilyIndices;

    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.independentBlend = VK_TRUE;

    // Opt into dynamic rendering — this is what removes the need for VkRenderPass/VkFramebuffer
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature{};
    dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeature.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &dynamicRenderingFeature; // chain the feature struct
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    if (vkCreateDevice(selected.device, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    volkLoadDevice(m_device);
    printf("Logical device created successfully (dynamic rendering enabled).\n");
}

void VulkanContext::CreateCommandPool()
{
    const auto& selected = m_availableDevices[m_selectedDeviceIndex];

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = selected.queueFamilyIndices.graphicsFamily.value();
    // ALLOW_RESET means individual command buffers from this pool can be
    // re-recorded (vkResetCommandBuffer) without resetting the whole pool —
    // needed since we'll re-record each frame's commands every frame.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    printf("Command pool created.\n");
}

void VulkanContext::CreateCommandBuffers(uint32_t count, VkCommandBuffer* commandBuffers)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    // PRIMARY buffers can be submitted directly to a queue (what we want).
    // SECONDARY buffers can only be called from within a primary buffer —
    // useful later for splitting work across threads, not needed yet.
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = count;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, commandBuffers) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    printf("%u command buffer(s) created.\n", count);
}

void VulkanContext::FreeCommandBuffers(uint32_t count, const VkCommandBuffer* commandBuffers)
{
    vkFreeCommandBuffers(m_device, m_commandPool, count, commandBuffers);
}

void VulkanContext::CreateQueue(VkSwapchainKHR swapchain, uint32_t numSwapchainImages)
{
    const auto& selected = m_availableDevices[m_selectedDeviceIndex];
    m_queue.Init(m_device, swapchain, selected.queueFamilyIndices.graphicsFamily.value(), 0, numSwapchainImages);
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties = m_availableDevices[m_selectedDeviceIndex].memoryProperties;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

BufferAndMemory VulkanContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    BufferAndMemory result;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &result.buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, result.buffer, &memRequirements);
    result.allocationSize = memRequirements.size;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &result.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(m_device, result.buffer, result.memory, 0);

    return result;
}

void VulkanContext::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    // One-shot command buffer just for this copy — allocated, used, freed immediately.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Use the graphics queue directly for this one-off transfer, then wait —
    // simple and correct, though a dedicated transfer queue would be faster
    // for large/frequent uploads later.
    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_availableDevices[m_selectedDeviceIndex].queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

BufferAndMemory VulkanContext::CreateVertexBuffer(const void* data, VkDeviceSize size)
{
    // Step 1: staging buffer — CPU-visible, so we can memcpy into it directly
    BufferAndMemory staging = CreateBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(m_device, staging.memory);

    // Step 2: real vertex buffer — device-local, fastest for the GPU to read,
    // but not directly writable from the CPU (hence the staging step above)
    BufferAndMemory vertexBuffer = CreateBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Step 3: GPU-side copy from staging into the real buffer
    CopyBuffer(staging.buffer, vertexBuffer.buffer, size);

    // Step 4: staging buffer's job is done, free it
    staging.Destroy(m_device);

    printf("Vertex buffer created (%llu bytes).\n", static_cast<unsigned long long>(size));

    return vertexBuffer;
}

BufferAndMemory VulkanContext::CreateUniformBuffer(VkDeviceSize size)
{
    // Host-visible/coherent since we'll be updating this every frame from the CPU
    return CreateBuffer(size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void VulkanContext::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(m_device, image, memory, 0);
}

VkImageView VulkanContext::CreateImageView(VkImage image, VkFormat format)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view");
    }
    return view;
}

VkSampler VulkanContext::CreateTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
    return sampler;
}

void VulkanContext::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    // Reuse the same one-shot command buffer pattern as CopyBuffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage, destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::invalid_argument("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_availableDevices[m_selectedDeviceIndex].queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanContext::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_availableDevices[m_selectedDeviceIndex].queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

VulkanTexture VulkanContext::CreateTexture(const char* filename)
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error(std::string("Failed to load texture image: ") + filename);
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    BufferAndMemory staging = CreateBuffer(imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, staging.memory);

    stbi_image_free(pixels);

    VulkanTexture texture;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

    CreateImage(texWidth, texHeight, format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texture.image, texture.memory);

    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging.buffer, texture.image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    staging.Destroy(m_device);

    texture.view = CreateImageView(texture.image, format);
    texture.sampler = CreateTextureSampler();

    printf("Texture created from %s (%dx%d).\n", filename, texWidth, texHeight);

    return texture;
}

BufferAndMemory VulkanContext::CreateIndexBuffer(const void* data, VkDeviceSize size)
{
    // Identical staging pattern to CreateVertexBuffer, just a different usage flag
    BufferAndMemory staging = CreateBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped;
    vkMapMemory(m_device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(m_device, staging.memory);

    BufferAndMemory indexBuffer = CreateBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    CopyBuffer(staging.buffer, indexBuffer.buffer, size);
    staging.Destroy(m_device);

    printf("Index buffer created (%llu bytes).\n", static_cast<unsigned long long>(size));

    return indexBuffer;
}

VkFormat VulkanContext::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_availableDevices[m_selectedDeviceIndex].device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format");
}

VkFormat VulkanContext::FindDepthFormat()
{
    // Try formats in order of preference — most GPUs support at least one of these
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void VulkanContext::Shutdown()
{
    // Block until the GPU has finished all submitted work — critical before
    // destroying anything the GPU might still be using (queue, semaphores).
    m_queue.WaitIdle();
    m_queue.Destroy();
}

VulkanTexture VulkanContext::CreateTexture(const char* filename, bool isColorData)
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error(std::string("Failed to load texture image: ") + filename);
    }
    VulkanTexture texture = CreateTextureFromRawRGBA(pixels, texWidth, texHeight, isColorData);
    stbi_image_free(pixels);
    return texture;
}

VulkanTexture VulkanContext::CreateTextureFromMemory(const unsigned char* data, size_t size, bool isColorData)
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to decode embedded texture from memory");
    }
    VulkanTexture texture = CreateTextureFromRawRGBA(pixels, texWidth, texHeight, isColorData);
    stbi_image_free(pixels);
    return texture;
}

VulkanTexture VulkanContext::CreateTextureFromRawRGBA(const unsigned char* pixels, uint32_t width, uint32_t height, bool isColorData)
{
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    BufferAndMemory staging = CreateBuffer(imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, staging.memory);

    VulkanTexture texture;
    // sRGB for visual colors (diffuse/albedo), UNORM for raw data (metallic, roughness, normals)
    VkFormat format = isColorData ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    CreateImage(width, height, format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texture.image, texture.memory);

    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging.buffer, texture.image, width, height);
    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    staging.Destroy(m_device);

    texture.view = CreateImageView(texture.image, format);
    texture.sampler = CreateTextureSampler();

    return texture;
}

VulkanTexture VulkanContext::CreateSolidColorTexture(float r, float g, float b, float a, bool isColorData)
{
    unsigned char pixel[4] = {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        static_cast<unsigned char>(a * 255.0f)
    };
    return CreateTextureFromRawRGBA(pixel, 1, 1, isColorData);
}

bool VulkanContext::CheckDynamicRenderingSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

static glm::vec3 CubeFaceDirection(int face, float u, float v)
{
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u)); // +X
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u)); // -X
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v)); // +Y
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v)); // -Y
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f)); // +Z
    default:return glm::normalize(glm::vec3(-u, -v, -1.0f)); // -Z
    }
}

static void SampleEquirect(const float* pixels, int width, int height, int channels, glm::vec3 dir, float* outRGBA)
{
    float phi = atan2f(dir.z, dir.x);          // -PI..PI
    float theta = acosf(glm::clamp(dir.y, -1.0f, 1.0f)); // 0..PI

    float u = (phi + 3.14159265f) / (2.0f * 3.14159265f);
    float v = theta / 3.14159265f;

    float fx = u * (width - 1);
    float fy = v * (height - 1);

    int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);
    float tx = fx - x0, ty = fy - y0;

    auto pixelAt = [&](int px, int py, int c) {
        return pixels[(py * width + px) * channels + c];
    };

    for (int c = 0; c < 4; c++) {
        float src = (c < channels) ? 1.0f : 0.0f; // alpha default 1.0 if source has no alpha
        if (c < channels) {
            float p00 = pixelAt(x0, y0, c);
            float p10 = pixelAt(x1, y0, c);
            float p01 = pixelAt(x0, y1, c);
            float p11 = pixelAt(x1, y1, c);
            float top = p00 * (1 - tx) + p10 * tx;
            float bot = p01 * (1 - tx) + p11 * tx;
            src = top * (1 - ty) + bot * ty;
        }
        outRGBA[c] = src;
    }
}

static float RadicalInverse_VdC(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

static glm::vec2 Hammersley(uint32_t i, uint32_t N)
{
    return glm::vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

static glm::vec3 ImportanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0f * 3.14159265f * Xi.x;
    float cosTheta = sqrtf((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrtf(1.0f - cosTheta * cosTheta);

    glm::vec3 H(cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta);

    glm::vec3 up = fabsf(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);

    return glm::normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

static float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

static float GeometrySmith_IBL(glm::vec3 N, glm::vec3 V, glm::vec3 L, float roughness)
{
    float NdotV = std::max(glm::dot(N, V), 0.0f);
    float NdotL = std::max(glm::dot(N, L), 0.0f);
    return GeometrySchlickGGX_IBL(NdotV, roughness) * GeometrySchlickGGX_IBL(NdotL, roughness);
}

static glm::vec2 IntegrateBRDF(float NdotV, float roughness, uint32_t sampleCount)
{
    glm::vec3 V(sqrtf(1.0f - NdotV * NdotV), 0.0f, NdotV);
    glm::vec3 N(0.0f, 0.0f, 1.0f);
    float A = 0.0f, B = 0.0f;

    for (uint32_t i = 0; i < sampleCount; i++) {
        glm::vec2 Xi = Hammersley(i, sampleCount);
        glm::vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

        float NdotL = std::max(L.z, 0.0f);
        float NdotH = std::max(H.z, 0.0f);
        float VdotH = std::max(glm::dot(V, H), 0.0f);

        if (NdotL > 0.0f) {
            float G = GeometrySmith_IBL(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV + 1e-5f);
            float Fc = powf(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return glm::vec2(A / float(sampleCount), B / float(sampleCount));
}

void VulkanContext::CreateCubemapImage(uint32_t faceSize, VkFormat format, uint32_t mipLevels, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { faceSize, faceSize, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate cubemap image memory");
    }
    vkBindImageMemory(m_device, image, memory, 0);
}

VkImageView VulkanContext::CreateCubemapImageView(VkImage image, VkFormat format, uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    VkImageView view;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap image view");
    }
    return view;
}

VkSampler VulkanContext::CreateMippedCubemapSampler(uint32_t mipLevels)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels - 1);

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create mipped cubemap sampler");
    }
    return sampler;
}

VulkanTexture VulkanContext::CreateEquirectangularCubemap(const char* filename, uint32_t faceSize)
{
    int width, height, channels;
    // stbi_loadf: loads as float, correct for HDR source images (.hdr files)
    float* pixels = stbi_loadf(filename, &width, &height, &channels, 0);
    if (!pixels) {
        throw std::runtime_error(std::string("Failed to load equirectangular image: ") + filename);
    }

    printf("Converting equirectangular image (%dx%d, %d channels) to %ux%u cubemap...\n",
        width, height, channels, faceSize, faceSize);

    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT; // HDR data needs float storage, not 8-bit

    // Build all 6 faces in CPU memory first
    size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * 4 * sizeof(float);
    std::vector<float> allFaces(6 * faceSize * faceSize * 4);

    for (int face = 0; face < 6; face++) {
        for (uint32_t y = 0; y < faceSize; y++) {
            for (uint32_t x = 0; x < faceSize; x++) {
                float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
                float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;

                glm::vec3 dir = CubeFaceDirection(face, u, v);

                float rgba[4];
                SampleEquirect(pixels, width, height, channels, dir, rgba);

                size_t idx = ((face * faceSize + y) * faceSize + x) * 4;
                allFaces[idx + 0] = rgba[0];
                allFaces[idx + 1] = rgba[1];
                allFaces[idx + 2] = rgba[2];
                allFaces[idx + 3] = rgba[3];
            }
        }
    }

    stbi_image_free(pixels);

    // Upload: staging buffer -> cubemap image (6 layers)
    VkDeviceSize totalSize = static_cast<VkDeviceSize>(allFaces.size()) * sizeof(float);

    BufferAndMemory staging = CreateBuffer(totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, totalSize, 0, &data);
    memcpy(data, allFaces.data(), static_cast<size_t>(totalSize));
    vkUnmapMemory(m_device, staging.memory);

    VulkanTexture cubemap;
    CreateCubemapImage(faceSize, format, 1, cubemap.image, cubemap.memory);

    // Transition all 6 layers to transfer-dst, copy each face, then to shader-read
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = cubemap.image;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    std::vector<VkBufferImageCopy> regions(6);
    for (int face = 0; face < 6; face++) {
        VkBufferImageCopy region{};
        region.bufferOffset = static_cast<VkDeviceSize>(face) * faceSize * faceSize * 4 * sizeof(float);
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { faceSize, faceSize, 1 };
        regions[face] = region;
    }
    vkCmdCopyBufferToImage(cmd, staging.buffer, cubemap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(regions.size()), regions.data());

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_availableDevices[m_selectedDeviceIndex].queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    staging.Destroy(m_device);

    cubemap.view = CreateCubemapImageView(cubemap.image, format, 1);
    cubemap.sampler = CreateTextureSampler();

    printf("Cubemap created from equirectangular image.\n");

    return cubemap;
}

VulkanTexture VulkanContext::UploadCubemapMips(const std::vector<std::vector<float>>& mipFaceData,
    const std::vector<uint32_t>& mipSizes, VkFormat format)
{
    uint32_t mipLevels = static_cast<uint32_t>(mipFaceData.size());

    VulkanTexture texture;
    CreateCubemapImage(mipSizes[0], format, mipLevels, texture.image, texture.memory);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = texture.image;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6 };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    std::vector<BufferAndMemory> stagingBuffers(mipLevels);

    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        VkDeviceSize mipByteSize = static_cast<VkDeviceSize>(mipFaceData[mip].size()) * sizeof(float);
        stagingBuffers[mip] = CreateBuffer(mipByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* data;
        vkMapMemory(m_device, stagingBuffers[mip].memory, 0, mipByteSize, 0, &data);
        memcpy(data, mipFaceData[mip].data(), static_cast<size_t>(mipByteSize));
        vkUnmapMemory(m_device, stagingBuffers[mip].memory);

        for (uint32_t face = 0; face < 6; face++) {
            VkBufferImageCopy region{};
            region.bufferOffset = static_cast<VkDeviceSize>(face) * mipSizes[mip] * mipSizes[mip] * 4 * sizeof(float);
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { mipSizes[mip], mipSizes[mip], 1 };
            vkCmdCopyBufferToImage(cmd, stagingBuffers[mip].buffer, texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
    }

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkQueue graphicsQueue;
    vkGetDeviceQueue(m_device, m_availableDevices[m_selectedDeviceIndex].queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);

    for (auto& sb : stagingBuffers) sb.Destroy(m_device);

    texture.view = CreateCubemapImageView(texture.image, format, mipLevels);
    texture.sampler = (mipLevels > 1) ? CreateMippedCubemapSampler(mipLevels) : CreateTextureSampler();

    return texture;
}

VulkanTexture VulkanContext::CreateIrradianceCubemap(const float* equirectPixels, int width, int height, int channels, uint32_t faceSize)
{
    printf("Generating irradiance cubemap (%ux%u)...\n", faceSize, faceSize);

    std::vector<float> faceData(static_cast<size_t>(faceSize) * faceSize * 6 * 4);
    const float PI = 3.14159265359f;
    const float sampleDelta = 0.025f; // coarse step — irradiance is very low-frequency, this is plenty

    for (int face = 0; face < 6; face++) {
        for (uint32_t y = 0; y < faceSize; y++) {
            for (uint32_t x = 0; x < faceSize; x++) {
                float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
                float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;
                glm::vec3 N = CubeFaceDirection(face, u, v);

                glm::vec3 up = fabsf(N.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                glm::vec3 right = glm::normalize(glm::cross(up, N));
                up = glm::cross(N, right);

                glm::vec3 irradiance(0.0f);
                int sampleCount = 0;

                for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta) {
                    for (float theta = 0.0f; theta < 0.5f * PI; theta += sampleDelta) {
                        glm::vec3 tangentSample(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), cosf(theta));
                        glm::vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

                        float rgba[4];
                        SampleEquirect(equirectPixels, width, height, channels, glm::normalize(sampleVec), rgba);

                        irradiance += glm::vec3(rgba[0], rgba[1], rgba[2]) * cosf(theta) * sinf(theta);
                        sampleCount++;
                    }
                }
                irradiance = PI * irradiance * (1.0f / float(sampleCount));

                size_t idx = ((static_cast<size_t>(face) * faceSize + y) * faceSize + x) * 4;
                faceData[idx + 0] = irradiance.x;
                faceData[idx + 1] = irradiance.y;
                faceData[idx + 2] = irradiance.z;
                faceData[idx + 3] = 1.0f;
            }
        }
    }

    std::vector<std::vector<float>> mipFaceData = { faceData };
    std::vector<uint32_t> mipSizes = { faceSize };

    VulkanTexture result = UploadCubemapMips(mipFaceData, mipSizes, VK_FORMAT_R32G32B32A32_SFLOAT);
    printf("Irradiance cubemap generated.\n");
    return result;
}

VulkanTexture VulkanContext::CreatePrefilteredSpecularCubemap(const float* equirectPixels, int width, int height, int channels,
    uint32_t baseFaceSize, uint32_t mipLevels)
{
    printf("Generating prefiltered specular cubemap (%u mips)...\n", mipLevels);

    std::vector<std::vector<float>> mipFaceData(mipLevels);
    std::vector<uint32_t> mipSizes(mipLevels);
    const uint32_t sampleCount = 256;

    for (uint32_t mip = 0; mip < mipLevels; mip++) {
        uint32_t faceSize = std::max(1u, baseFaceSize >> mip);
        mipSizes[mip] = faceSize;
        float roughness = (mipLevels > 1) ? float(mip) / float(mipLevels - 1) : 0.0f;

        std::vector<float>& faceData = mipFaceData[mip];
        faceData.resize(static_cast<size_t>(faceSize) * faceSize * 6 * 4);

        for (int face = 0; face < 6; face++) {
            for (uint32_t y = 0; y < faceSize; y++) {
                for (uint32_t x = 0; x < faceSize; x++) {
                    float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
                    float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;
                    glm::vec3 N = glm::normalize(CubeFaceDirection(face, u, v));
                    glm::vec3 V = N;

                    glm::vec3 prefilteredColor(0.0f);
                    float totalWeight = 0.0f;

                    for (uint32_t i = 0; i < sampleCount; i++) {
                        glm::vec2 Xi = Hammersley(i, sampleCount);
                        glm::vec3 H = ImportanceSampleGGX(Xi, N, roughness);
                        glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

                        float NdotL = glm::dot(N, L);
                        if (NdotL > 0.0f) {
                            float rgba[4];
                            SampleEquirect(equirectPixels, width, height, channels, glm::normalize(L), rgba);
                            prefilteredColor += glm::vec3(rgba[0], rgba[1], rgba[2]) * NdotL;
                            totalWeight += NdotL;
                        }
                    }
                    if (totalWeight > 0.0f) prefilteredColor /= totalWeight;

                    size_t idx = ((static_cast<size_t>(face) * faceSize + y) * faceSize + x) * 4;
                    faceData[idx + 0] = prefilteredColor.x;
                    faceData[idx + 1] = prefilteredColor.y;
                    faceData[idx + 2] = prefilteredColor.z;
                    faceData[idx + 3] = 1.0f;
                }
            }
        }
    }

    VulkanTexture result = UploadCubemapMips(mipFaceData, mipSizes, VK_FORMAT_R32G32B32A32_SFLOAT);
    printf("Prefiltered specular cubemap generated.\n");
    return result;
}

VulkanTexture VulkanContext::CreateBRDFLUT(uint32_t size)
{
    printf("Generating BRDF integration LUT (%ux%u)...\n", size, size);

    std::vector<float> lutData(static_cast<size_t>(size) * size * 2);

    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            float NdotV = (x + 0.5f) / float(size);
            float roughness = (y + 0.5f) / float(size);
            glm::vec2 result = IntegrateBRDF(NdotV, roughness, 256);

            size_t idx = (static_cast<size_t>(y) * size + x) * 2;
            lutData[idx + 0] = result.x;
            lutData[idx + 1] = result.y;
        }
    }

    VulkanTexture texture;
    VkFormat format = VK_FORMAT_R32G32_SFLOAT;
    VkDeviceSize dataSize = static_cast<VkDeviceSize>(lutData.size()) * sizeof(float);

    BufferAndMemory staging = CreateBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(m_device, staging.memory, 0, dataSize, 0, &data);
    memcpy(data, lutData.data(), static_cast<size_t>(dataSize));
    vkUnmapMemory(m_device, staging.memory);

    CreateImage(size, size, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.memory);

    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging.buffer, texture.image, size, size);
    TransitionImageLayout(texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    staging.Destroy(m_device);

    texture.view = CreateImageView(texture.image, format);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create BRDF LUT sampler");
    }

    printf("BRDF LUT generated.\n");
    return texture;
}

VulkanContext::IBLTextures VulkanContext::CreateIBLFromEquirect(const char* filename)
{
    int width, height, channels;
    float* pixels = stbi_loadf(filename, &width, &height, &channels, 0);
    if (!pixels) {
        throw std::runtime_error(std::string("Failed to load equirectangular image: ") + filename);
    }

    printf("Building IBL data from %s (%dx%d)...\n", filename, width, height);

    IBLTextures result;
    result.prefilteredMipLevels = 8;
    result.irradiance = CreateIrradianceCubemap(pixels, width, height, channels, 32);
    result.prefilteredSpecular = CreatePrefilteredSpecularCubemap(pixels, width, height, channels, 256, result.prefilteredMipLevels);
    result.brdfLUT = CreateBRDFLUT(128);

    stbi_image_free(pixels);

    printf("IBL generation complete.\n");
    return result;
}

BufferAndMemory VulkanContext::CreateStorageBuffer(VkDeviceSize size)
{
    return CreateBuffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}