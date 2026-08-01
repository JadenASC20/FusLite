#pragma once
#include <glm/glm.hpp>

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 projNoJitter;
    glm::mat4 lightViewProj;
    glm::mat4 prevModel;
    glm::mat4 prevViewProj;
    glm::vec4 cameraPos;
    glm::vec4 penumbraParams;
    glm::vec4 penumbraPattern;
};