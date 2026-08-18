#pragma once
#include <volk.h>
#include <array>

struct Vertex
{
    float pos[3];
    float normal[3];
    float color[3];
    float texCoord[2];
    float tangent[4];

    static VkVertexInputBindingDescription GetBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 5> GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 5> attributes{};

        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, normal);

        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, color);

        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[3].offset = offsetof(Vertex, texCoord);

        attributes[4].binding = 0; 
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[4].offset = offsetof(Vertex, tangent);

        return attributes;
    }
};