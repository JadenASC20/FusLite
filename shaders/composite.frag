#version 450

layout(binding = 0) uniform sampler2D hdrTex;   // lit scene
layout(binding = 1) uniform sampler2D ssrTex;   // raw reflection (rgb) + hit mask (a)

layout(push_constant) uniform CompPush {
    float reflectivity;
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 hdr = texture(hdrTex, inUV).rgb;
    vec4 ssr = texture(ssrTex, inUV);
    vec3 result = mix(hdr, ssr.rgb, ssr.a * pc.reflectivity);
    outColor = vec4(result, 1.0);
}