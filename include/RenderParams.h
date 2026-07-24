#pragma once
#include <glm/glm.hpp>


struct RenderParams
{
    glm::vec4 lightDirAndIntensity = { -0.5f, 1.0f, -0.3f, 3.0f }; // xyz = direction, w = intensity
    float clearcoatFactor = 0.8f;
    float clearcoatRoughness = 0.03f;
    float flakeStrength = 0.12f;
    float flakeScale = 400.0f;
};

constexpr int MAX_LIGHTS = 128;

struct GPULight
{
    glm::vec4 posAndRadius;
    glm::vec4 colorAndIntensity;
};