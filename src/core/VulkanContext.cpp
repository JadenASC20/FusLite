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

    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

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

    // Graphics and present may be the same family (common on NVIDIA) —
    // dedupe with a set so we don't request the same queue twice.
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

    // Features we actually want enabled on the device.
    // Empty for now — we'll opt into specific features (like geometry shaders,
    // if needed) as the renderer grows. Requesting more than you use just
    // narrows your compatible hardware for no benefit.
    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    // Device-level validation layers are deprecated in modern Vulkan (instance
    // layers cover everything now), but setting these keeps compatibility
    // with older loaders that still expect it.
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    if (vkCreateDevice(selected.device, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }

    // volk needs to load device-level function pointers separately from
    // instance-level ones — this is what makes device-specific calls
    // (vkCreateBuffer, vkCmdDraw, etc.) actually resolve correctly.
    volkLoadDevice(m_device);

    printf("Logical device created successfully.\n");
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

void VulkanContext::Shutdown()
{
    // Block until the GPU has finished all submitted work — critical before
    // destroying anything the GPU might still be using (queue, semaphores).
    m_queue.WaitIdle();
    m_queue.Destroy();
}
