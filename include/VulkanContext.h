#pragma once
#include <volk.h>
#include <vector>
#include <optional>
#include <string>
#include <VulkanQueue.h>
#include <Buffer.h>
#include <VulkanTexture.h>

struct GLFWwindow;

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool IsComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }

};

struct PhysicalDeviceInfo
{
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};

    std::vector<VkQueueFamilyProperties> queueFamilyProperties;
    QueueFamilyIndices queueFamilyIndices;

    // Swapchain support — queried now since we already have the device handle,
    // needed again shortly for actual swapchain creation.
    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    std::vector<VkPresentModeKHR> presentModes;

    bool IsSuitable() const
    {
        return queueFamilyIndices.IsComplete() && !surfaceFormats.empty() && !presentModes.empty();
    }
};

class VulkanContext
{
public:
    VulkanContext();
    ~VulkanContext();

    void Init(const char* pAppName, GLFWwindow* window);
    void CreateQueue(VkSwapchainKHR swapchain, uint32_t numSwapchainImages);
    void Shutdown(); // waits for GPU idle and tears down the queue safely

    VkInstance GetInstance() const { return m_instance; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    const PhysicalDeviceInfo& GetSelectedDevice() const { return m_availableDevices[m_selectedDeviceIndex]; }
    VkDevice GetDevice() const { return m_device; }
    VkCommandPool GetCommandPool() const { return m_commandPool; }
    VulkanQueue* GetQueue() { return &m_queue; }

    void CreateCommandBuffers(uint32_t count, VkCommandBuffer* commandBuffers);
    void FreeCommandBuffers(uint32_t count, const VkCommandBuffer* commandBuffers);
    
    BufferAndMemory CreateVertexBuffer(const void* data, VkDeviceSize size);
    BufferAndMemory CreateUniformBuffer(VkDeviceSize size);
    BufferAndMemory CreateIndexBuffer(const void* data, VkDeviceSize size);

    VulkanTexture CreateTexture(const char* filename);
    VkFormat FindDepthFormat();

    VulkanTexture CreateTexture(const char* filename, bool isColorData);
    VulkanTexture CreateTextureFromMemory(const unsigned char* data, size_t size, bool isColorData);
    VulkanTexture CreateTextureFromRawRGBA(const unsigned char* pixels, uint32_t width, uint32_t height, bool isColorData);
    VulkanTexture CreateSolidColorTexture(float r, float g, float b, float a, bool isColorData);

private:
    void CreateInstance(const char* pAppName);
    void SetupDebugMessenger();
    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();
    void CreateSurface(GLFWwindow* window);

    // Physical device: gather-then-select
    void EnumeratePhysicalDevices();
    PhysicalDeviceInfo QueryPhysicalDevice(VkPhysicalDevice device);
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, const std::vector<VkQueueFamilyProperties>& queueFamilies);
    void SelectPhysicalDevice();
    int RateDeviceSuitability(const PhysicalDeviceInfo& info);
    
    void CreateLogicalDevice();
    void CreateCommandPool();
    
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VulkanQueue m_queue;

    std::vector<PhysicalDeviceInfo> m_availableDevices;
    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    int m_selectedDeviceIndex = -1;

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    BufferAndMemory CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format);
    VkSampler CreateTextureSampler();
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    bool CheckDynamicRenderingSupport(VkPhysicalDevice device);
};