#version 450

#define MAX_LIGHTS 128

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    vec4 lightDirAndIntensity;
    vec4 sunColor;
    vec4 colorTint;
    float clearcoatFactor;
    float clearcoatRoughness;
    float flakeStrength;
    float flakeScale;
    vec4 clusterGridAndScreen;
    vec2 screenSize;
    float nearZ;
    float farZ;
} pc;

layout(binding = 1) uniform sampler2D diffuseSampler;
layout(binding = 2) uniform sampler2D metallicRoughnessSampler; // G=roughness, B=metallic (glTF convention)
layout(binding = 3) uniform samplerCube irradianceMap;
layout(binding = 4) uniform samplerCube prefilteredMap;
layout(binding = 5) uniform sampler2D brdfLUT;

struct LightData {
    vec4 posAndRadius;
    vec4 colorAndIntensity;
};

layout(std430, binding = 6) readonly buffer LightBuffer {
    LightData lights[];
} lightBuffer;

struct ClusterLightInfo {
    uint offset;
    uint count;
};

layout(std430, binding = 7) readonly buffer ClusterLightInfoBuffer {
    ClusterLightInfo info[];
} clusterLightInfo;

layout(std430, binding = 8) readonly buffer LightIndexBuffer {
    uint indices[];
} lightIndexBuffer;

layout(binding = 9) uniform sampler2D shadowMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec3 fragPosWorld;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Schlick-GGX geometry term
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluateDirectLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

// Clearcoat layer: a second, much smoother specular lobe on top of the base material.
// Real clear lacquer has an IOR around 1.5, giving F0 ~0.04 — same as any dielectric.
const float CLEARCOAT_F0 = 0.04;

float DistributionGGX_Clearcoat(vec3 N, vec3 H, float roughness)
{
    // Identical math to the base DistributionGGX — factored separately so the
    // clearcoat lobe can use its own (much lower) roughness independently.
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Simple hash-based pseudo-random noise, used to generate flake positions
float Hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Perturbs a normal slightly based on high-frequency noise, simulating
// small reflective metal/mica flakes embedded in the base coat.
vec3 ApplyFlakeNormal(vec3 N, vec3 tangent, vec3 bitangent, vec2 uv, float flakeScale, float flakeStrength)
{
    vec2 flakeUV = uv * flakeScale;
    vec2 cell = floor(flakeUV);

    // Each "cell" gets its own random flake tilt direction and intensity
    float randX = Hash(cell) * 2.0 - 1.0;
    float randY = Hash(cell + vec2(37.0, 17.0)) * 2.0 - 1.0;
    float randStrength = Hash(cell + vec2(91.0, 53.0));

    // Only some cells actually contain a visible flake — sparse, not everywhere
    float flakeMask = step(0.85, randStrength);

    vec3 perturb = (tangent * randX + bitangent * randY) * flakeStrength * flakeMask;
    return normalize(N + perturb);
}

float ComputeShadow(vec3 fragPosWorld, vec3 N, vec3 L)
{
    vec4 lightSpacePos = ubo.lightViewProj * vec4(fragPosWorld, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Vulkan NDC: xy in [-1,1] -> [0,1] for texture sampling; z already in [0,1]
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Outside the shadow map's coverage — treat as fully lit
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;

    // Slope-scaled bias to reduce shadow acne on surfaces at grazing angles to the light
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.0005);

    return (currentDepth - bias > closestDepth) ? 0.0 : 1.0;
}

void main() {
    vec3 albedo = texture(diffuseSampler, fragTexCoord).rgb;
    albedo *= pc.colorTint.rgb;
    vec2 mr = texture(metallicRoughnessSampler, fragTexCoord).gb;
    float roughness = clamp(mr.x, 0.05, 1.0);
    float metallic = mr.y;

    float clearcoatFactor = pc.clearcoatFactor;
    float clearcoatRoughness = pc.clearcoatRoughness;

    vec3 N = normalize(fragNormalWorld);

    vec3 dPosX = dFdx(fragPosWorld);
    vec3 dPosY = dFdy(fragPosWorld);
    vec3 approxTangent = normalize(dPosX - N * dot(dPosX, N));
    vec3 approxBitangent = normalize(cross(N, approxTangent));
    N = ApplyFlakeNormal(N, approxTangent, approxBitangent, fragTexCoord, pc.flakeScale, pc.flakeStrength);

    vec3 V = normalize(ubo.cameraPos.xyz - fragPosWorld);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 totalLight = vec3(0.0);
    vec3 totalClearcoat = vec3(0.0);

    #define ACCUMULATE_LIGHT(L, radiance) \
    { \
        totalLight += EvaluateDirectLight(N, V, L, radiance, albedo, roughness, metallic); \
        vec3 H_ = normalize(V + (L)); \
        float NdotL_ = max(dot(N, (L)), 0.0); \
        float NDFc_ = DistributionGGX_Clearcoat(N, H_, clearcoatRoughness); \
        float Gc_ = GeometrySmith(N, V, (L), clearcoatRoughness); \
        float Fc_ = CLEARCOAT_F0 + (1.0 - CLEARCOAT_F0) * pow(clamp(1.0 - max(dot(H_, V), 0.0), 0.0, 1.0), 5.0); \
        Fc_ *= clearcoatFactor; \
        float denom_ = 4.0 * NdotV * NdotL_ + 0.0001; \
        totalClearcoat += vec3((NDFc_ * Gc_ * Fc_) / denom_) * (radiance); \
    }

    // Sun
    vec3 sunDir = normalize(pc.lightDirAndIntensity.xyz);
    vec3 sunColor = pc.sunColor.rgb * pc.lightDirAndIntensity.w;

 
    float surfaceShadow = ComputeShadow(fragPosWorld, N, sunDir);
    sunColor *= surfaceShadow; // apply shadow to the sun only, for now

    ACCUMULATE_LIGHT(sunDir, sunColor)

    // Determine which cluster this fragment belongs to
    ivec3 gridDims = ivec3(pc.clusterGridAndScreen.xyz);
    vec2 tileSize = pc.screenSize / vec2(gridDims.xy);

    ivec2 tileXY = ivec2(gl_FragCoord.xy / tileSize);
    tileXY = clamp(tileXY, ivec2(0), gridDims.xy - 1);

    // View-space depth of this fragment (positive distance from camera)
    vec4 fragPosView = ubo.view * vec4(fragPosWorld, 1.0);
    float viewDepth = -fragPosView.z;

    float logRatio = pc.farZ / pc.nearZ;
    int zSlice = int(log(viewDepth / pc.nearZ) / log(logRatio) * float(gridDims.z));
    zSlice = clamp(zSlice, 0, gridDims.z - 1);

    uint clusterIndex = uint(tileXY.x) + uint(tileXY.y) * uint(gridDims.x) +
                         uint(zSlice) * uint(gridDims.x) * uint(gridDims.y);

    uint lightOffset = clusterLightInfo.info[clusterIndex].offset;
    uint lightCount = clusterLightInfo.info[clusterIndex].count;

    for (uint li = 0; li < lightCount; li++) {
        uint lightIdx = lightIndexBuffer.indices[lightOffset + li];

        vec3 lightPos = lightBuffer.lights[lightIdx].posAndRadius.xyz;
        float lightRadius = lightBuffer.lights[lightIdx].posAndRadius.w;
        vec3 lightColor = lightBuffer.lights[lightIdx].colorAndIntensity.rgb;
        float lightIntensity = lightBuffer.lights[lightIdx].colorAndIntensity.a;

        vec3 toLight = lightPos - fragPosWorld;
        float dist = length(toLight);
        vec3 pointL = toLight / max(dist, 0.0001);

        float attenuation = clamp(1.0 - (dist / lightRadius), 0.0, 1.0);
        attenuation *= attenuation;
        vec3 pointRadiance = lightColor * lightIntensity * attenuation;

        ACCUMULATE_LIGHT(pointL, pointRadiance)
    }

    vec3 baseLayer = totalLight;

    float avgFc = clamp(clearcoatFactor * 0.5, 0.0, 1.0);
    vec3 outgoing = baseLayer * (1.0 - avgFc) + totalClearcoat;

    // --- IBL ambient ---
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;

    vec3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);

    vec3 irradianceSample = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = irradianceSample * albedo;

    vec3 prefilteredColor = textureLod(prefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);

    vec3 ambient = kD_ibl * diffuseIBL + specularIBL;
    vec3 finalColor = ambient + outgoing;

    outColor = vec4(finalColor, 1.0);
}