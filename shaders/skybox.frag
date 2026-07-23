#version 450

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outColor;
layout(binding = 1) uniform samplerCube cubeSampler;

void main() {
    outColor = texture(cubeSampler, inDir);
}