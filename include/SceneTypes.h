#pragma once
#include <glm/glm.hpp>
#include <string>

struct SceneObject
{
    std::string name;
    glm::mat4 transform = glm::mat4(1.0f);

    // Material tuning — same fields as existing RenderParams, now per-object
    float clearcoatFactor = 0.8f;
    float clearcoatRoughness = 0.03f;
    float flakeStrength = 0.12f;
    float flakeScale = 400.0f;

    // Later: pointer/index to which Model/mesh this object actually draws
};

struct SceneLight
{
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float radius = 5.0f;
};