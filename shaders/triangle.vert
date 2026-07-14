#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormalWorld;
layout(location = 3) out vec3 fragPosWorld;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // Normal matrix: inverse-transpose of the model matrix's upper 3x3,
    // correct even under non-uniform scale. mat3(model) alone would be
    // wrong if the model is scaled unevenly.
    mat3 normalMatrix = mat3(transpose(inverse(ubo.model)));
    fragNormalWorld = normalize(normalMatrix * inNormal);
    fragPosWorld = worldPos.xyz;
}