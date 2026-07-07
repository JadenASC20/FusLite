#define VOLK_IMPLEMENTATION
#include <volk.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

int main() {
    // 1. Initialize volk (loads the Vulkan loader itself)
    if (volkInitialize() != VK_SUCCESS) {
        fprintf(stderr, "Failed to initialize volk\n");
        return EXIT_FAILURE;
    }

    // 2. Init GLFW (windowing) — no OpenGL context, we're using Vulkan
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "FusLite", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // 3. Create a Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // GLFW tells us which extensions it needs for surface creation
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    VkInstance instance;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance (VkResult: %d)\n", result);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // 4. Load instance-level function pointers now that we have an instance
    volkLoadInstance(instance);

    printf("Vulkan instance created successfully.\n");

    // 5. Quick sanity check: enumerate physical devices (GPUs)
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    printf("Found %u Vulkan-capable device(s).\n", deviceCount);

    if (deviceCount > 0) {
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        for (const auto& device : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            printf("  - %s\n", props.deviceName);
        }
    }

    // 6. Basic window loop (just keeps the window open so you can see it)
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // 7. Cleanup
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
} 