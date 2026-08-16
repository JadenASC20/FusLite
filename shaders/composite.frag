#version 450
layout(binding = 0) uniform sampler2D hdrTex;      // lit scene
layout(binding = 1) uniform sampler2D ssrTex;      // rgb = SSR hit color, a = hit mask
layout(binding = 2) uniform sampler2D normalTex;   // world normal, [0,1]
layout(binding = 3) uniform sampler2D depthTex;    // scene depth [0,1]
layout(binding = 4) uniform samplerCube prefilteredMap;  // IBL prefiltered specular
layout(binding = 5) uniform sampler2D materialTex; // R=roughness, G=metallic

layout(push_constant) uniform CompPush {
    mat4 invProj;
    mat4 invView;
    vec4 cameraPos;     // xyz = camera world pos
    float reflectivity;
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const float MAX_REFLECTION_LOD = 4.0;

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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
    float depth = texture(depthTex, inUV).r;

    if (depth >= 1.0) { // sky: passthrough, no reflection
        outColor = vec4(hdr, 1.0);
        return;
    }

    vec2 mat = texture(materialTex, inUV).rg;
    float roughness = mat.r;
    float metallic  = mat.g;

    vec3 worldPos = ReconstructWorldPos(inUV, depth);
    vec3 N = normalize(texture(normalTex, inUV).xyz * 2.0 - 1.0);
    vec3 V = normalize(pc.cameraPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 0.0);
    vec3 R = reflect(-V, N);

    // Roughness-dependent mip: rough surfaces sample a blurrier environment.
    vec3 iblReflection = textureLod(prefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;

    vec3 F0 = mix(vec3(0.04), vec3(1.0), metallic); // untinted (metal tint = next step)
    vec3 F  = FresnelSchlickRoughness(NdotV, F0, roughness);

    vec4 ssr = texture(ssrTex, inUV); // rgb = hit color, a = hit mask
    vec3 reflection = mix(iblReflection, ssr.rgb, ssr.a); // SSR where hit, IBL where miss

    outColor = vec4(hdr + reflection * F * pc.reflectivity, 1.0);
}