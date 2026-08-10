#version 450

layout(binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform DebugPush {
    int   mode;    // 1=HDR, 2=Motion, 3=Normal, 4=Depth
    float nearZ;
    float farZ;
    float _pad;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    if (pc.mode == 1) {                         // HDR: Reinhard so it's viewable
        vec3 c = texture(srcTex, inUV).rgb;
        outColor = vec4(c / (c + vec3(1.0)), 1.0);
    }
    else if (pc.mode == 2) {                    // Motion: signed 2D vector -> color, amplified
        vec2 m = texture(srcTex, inUV).rg;
        outColor = vec4(m * 20.0 + 0.5, 0.0, 1.0);
    }
    else if (pc.mode == 3) {                    // Normal: already [0,1]-encoded
        outColor = vec4(texture(srcTex, inUV).rgb, 1.0);
    }
    else if (pc.mode == 4) {                    // Depth: sample .r, linearize (Vulkan [0,1] depth)
        float d = texture(srcTex, inUV).r;
        float lin = (2.0 * pc.nearZ * pc.farZ) /
                    (pc.farZ + pc.nearZ - d * (pc.farZ - pc.nearZ));
        outColor = vec4(vec3(lin / pc.farZ), 1.0);
    }
    else {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}