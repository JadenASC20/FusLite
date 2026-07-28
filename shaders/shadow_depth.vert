#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProj;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}