#version 450

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion; 
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec2 outMaterial;

layout(binding = 1) uniform samplerCube cubeSampler;

void main() {
    outColor = texture(cubeSampler, inDir);
    outMotion = vec2(0.0);
    outNormal = vec4(0.5, 0.5, 1.0, 1.0);
    outMaterial = vec2(1.0, 0.0);
}