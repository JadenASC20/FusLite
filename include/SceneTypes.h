#pragma once
#include <glm/glm.hpp>
#include <string>

constexpr int RAMP_RESOLUTION = 64;
constexpr int MAX_RAMP_OBJECTS = 8;
constexpr int MAX_RAMP_STOPS = 6;

struct ColorStop
{
    glm::vec3 color;
    float position;
};

struct SceneObject
{
    std::string name;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec3 colorTint = glm::vec3(1.0f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    float clearcoatFactor = 0.8f;
    float clearcoatRoughness = 0.03f;
    float flakeStrength = 0.12f;
    float flakeScale = 400.0f;

    std::vector<ColorStop> penumbraStops = {
        { { 1.00f, 0.50f, 0.20f }, 0.0f },
        { { 0.20f, 0.35f, 0.80f }, 1.0f }
    };
    float penumbraWeight = 0.0f;
    float penumbraBands = 0.0f;          // 0 = smooth
    int   penumbraPattern = 0;           // 0 none, 1 noise, 2 hatch, 3 halftone
    float penumbraPatternScale = 40.0f;
    float penumbraPatternStrength = 0.0f;
};

struct SceneLight
{
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float radius = 5.0f;
};