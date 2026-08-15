#version 450

layout(binding = 0) uniform sampler2D hdrTex;   // lit scene
layout(binding = 1) uniform sampler2D ssrTex;      // rgb = SSR hit color, a = hit mask
layout(binding = 2) uniform sampler2D normalTex;   // world normal, [0,1]
layout(binding = 3) uniform sampler2D depthTex;    // scene depth [0,1]
layout(binding = 4) uniform samplerCube prefilteredMap;  // IBL prefiltered specular

layout(push_constant) uniform CompPush {
    mat4 invProj;
    mat4 invView;
    vec4 cameraPos;     // xyz = camera world pos
    float reflectivity;
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 ReconstructWorldPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProj * ndc;
    viewPos /= viewPos.w;
    vec4 world = pc.invView * viewPos;
    return world.xyz;
}

void main()
{
    vec3 hdr = texture(hdrTex, inUV).rgb;
    vec4 ssr = texture(ssrTex, inUV);   // rgb, a=hitMask

    // Reconstruct world-space reflection vector to sample the environment on a miss.
    float depth = texture(depthTex, inUV).r;
    vec3 iblReflection = vec3(0.0);
    if (depth < 1.0) {  // not sky
        vec3 worldPos = ReconstructWorldPos(inUV, depth);
        vec3 N = normalize(texture(normalTex, inUV).xyz * 2.0 - 1.0);
        vec3 V = normalize(pc.cameraPos.xyz - worldPos);
        vec3 R = reflect(-V, N);
        iblReflection = textureLod(prefilteredMap, R, 0.0).rgb;  // mip 0 = sharpest (mirror)
    }

    // SSR where it hit (a=1), IBL environment where it missed (a=0).
    vec3 reflection = mix(iblReflection, ssr.rgb, ssr.a);

    // Blend reflection into the lit scene.
    outColor = vec4(mix(hdr, reflection, pc.reflectivity), 1.0);
}