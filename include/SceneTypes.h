#pragma once
#include <glm/glm.hpp>
#include <string>

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
};

struct SceneLight
{
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float radius = 5.0f;
};