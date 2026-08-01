#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 projNoJitter;
    mat4 lightViewProj;
    mat4 prevModel;
    mat4 prevViewProj;
    vec4 cameraPos;
    vec4 penumbraParams;
    vec4 penumbraPattern;

} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) out vec4 fragClipPos;
layout(location = 5) out vec4 fragPrevClipPos;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormalWorld;
layout(location = 3) out vec3 fragPosWorld;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragColor = inColor;
    fragTexCoord = inTexCoord;

    mat3 normalMatrix = mat3(transpose(inverse(ubo.model)));
    fragNormalWorld = normalize(normalMatrix * inNormal);
    fragPosWorld = worldPos.xyz;

    // Unjittered current position, and the same vertex under last frame's
    // transforms. Their screen-space difference is the motion vector.
    fragClipPos = ubo.projNoJitter * ubo.view * worldPos;
    fragPrevClipPos = ubo.prevViewProj * ubo.prevModel * vec4(inPosition, 1.0);
}