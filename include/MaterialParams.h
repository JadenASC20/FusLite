#pragma once
#include <glm/glm.hpp>
#include <string>

// Editable per-material params, pushed per-submesh. Index matches the aiMesh material
// index. Texture-backed fields (base color / metallic / roughness) flow through the
// descriptor set; these are the push-constant overrides + non-textured params.

struct MaterialParams
{
    std::string name;
    glm::vec3 colorTint = glm::vec3(1.0f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    float clearcoatFactor = 0.8f;
    float clearcoatRoughness = 0.03f;
    float flakeStrength = 0.12f;
    float flakeScale = 400.0f;
    bool hasDiffuseTexture = false;
    bool hasMRTexture = false;
    bool hasNormalMap = false;
    bool isGlass = false;
    glm::vec2 normalUVScale = glm::vec2(1.0f);
    glm::vec2 normalUVOffset = glm::vec2(0.0f);
};