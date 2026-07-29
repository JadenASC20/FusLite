#pragma once
#include <glm/glm.hpp>

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightViewProj;
    glm::vec4 cameraPos;
    glm::vec4 penumbraParams;   // x weight, y rampIndex, z bands, w patternStrength
    glm::vec4 penumbraPattern;  // x mode, y scale, zw unused
};