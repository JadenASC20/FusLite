#pragma once
#include <volk.h>
#include <array>

struct Vertex
{
    float pos[2];
    float color[3];

    static VkVertexInputBindingDescription GetBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};

        attributes[0].binding = 0;
        attributes[0].location = 0; // matches `layout(location = 0)` in the vertex shader
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT; // vec2
        attributes[0].offset = offsetof(Vertex, pos);

        attributes[1].binding = 0;
        attributes[1].location = 1; // matches `layout(location = 1)`
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
        attributes[1].offset = offsetof(Vertex, color);

        return attributes;
    }
};