#pragma once
#include <glm/glm.hpp>

constexpr int MAX_LIGHTS = 4;

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;

    glm::vec4 lightPosAndRadius[MAX_LIGHTS];   // xyz = world position, w = radius/falloff distance
    glm::vec4 lightColorAndIntensity[MAX_LIGHTS]; // rgb = color, a = intensity
    glm::vec4 numLightsPacked; // x = active light count (as float, avoids std140 int-array padding headaches)
};