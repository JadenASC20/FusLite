#pragma once
#include <glm/glm.hpp>

struct RenderParams
{
    glm::vec4 lightDirAndIntensity = { -0.5f, 1.0f, -0.3f, 3.0f };
    glm::vec4 sunColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    glm::vec4 colorTint = { 1.0f, 1.0f, 1.0f, 0.0f };
    float clearcoatFactor = 0.8f;
    float clearcoatRoughness = 0.03f;
    float flakeStrength = 0.12f;
    float flakeScale = 400.0f;
    glm::vec4 clusterGridAndScreen = { 16.0f, 9.0f, 24.0f, 0.0f };
    glm::vec2 screenSize = { 1920.0f, 1080.0f };
    float nearZ = 0.1f;
    float farZ = 1000.0f;
};

constexpr int MAX_LIGHTS = 128;

struct GPULight
{
    glm::vec4 posAndRadius;
    glm::vec4 colorAndIntensity;
};