#version 450

#define MAX_LIGHTS 128
#define RAMP_RESOLUTION 64

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
    float roughness;
    float metallic;
    float lightSize;
    vec4 normalUVTransform; 
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

layout(std430, binding = 10) readonly buffer PenumbraRampBuffer {
    vec4 colors[];
} rampBuffer;

layout(binding = 11) uniform sampler2D normalSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec3 fragPosWorld;
layout(location = 4) in vec4 fragClipPos;
layout(location = 5) in vec4 fragPrevClipPos;
layout(location = 6) in vec4 fragTangentWorld;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMotion;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec2 outMaterial;

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

// Clearcoat layer: a second, much smoother specular lobe on top of the base material
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
// small reflective metal/mica flakes embedded in the base coat
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

const int BLOCKER_SAMPLES = 24;
const int PCF_SAMPLES = 32;

const vec2 POISSON[32] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790),
    vec2( 0.61461878,  0.52732132), vec2(-0.54130814,  0.71391660),
    vec2( 0.07953821, -0.53107584), vec2(-0.60918891, -0.12481321),
    vec2( 0.28129430,  0.62823123), vec2(-0.13138071,  0.40128181),
    vec2( 0.71981132, -0.24193218), vec2(-0.42198131, -0.63821921),
    vec2( 0.03812310,  0.13214128), vec2(-0.72183411,  0.19832141),
    vec2( 0.48132198, -0.71328913), vec2(-0.18213289, -0.78213198),
    vec2( 0.89321381,  0.41832197), vec2(-0.93218319, -0.61283197),
    vec2( 0.32189312,  0.91832137), vec2(-0.51832197,  0.28193281)
);

// how far away is whatever is blocking the light?
float FindBlockerDepth(vec2 uv, float currentDepth, float searchRadius, float rotSin, float rotCos)
{
    float blockerSum = 0.0;
    int blockerCount = 0;
    for (int i = 0; i < BLOCKER_SAMPLES; i++) {
        vec2 o = POISSON[i];
        vec2 r = vec2(o.x * rotCos - o.y * rotSin, o.x * rotSin + o.y * rotCos);
        float sampleDepth = texture(shadowMap, uv + r * searchRadius).r;
        if (sampleDepth < currentDepth) {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }
    if (blockerCount == 0) return -1.0;
    return blockerSum / float(blockerCount);
}

float ComputeShadow(vec3 fragPosWorld, vec3 N, vec3 L)
{
    vec4 lightSpacePos = ubo.lightViewProj * vec4(fragPosWorld, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 1.0;
    }

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.0005);
    float angle = Hash(gl_FragCoord.xy) * 6.28318;
    float rotSin = sin(angle);
    float rotCos = cos(angle);

    // blocker search
    float searchRadius = pc.lightSize * 0.5;
    float avgBlockerDepth = FindBlockerDepth(projCoords.xy, currentDepth - bias, searchRadius, rotSin, rotCos);
    if (avgBlockerDepth < 0.0) return 1.0;

    // penumbra width. The further the receiver is behind the blocker,
    // the wider the soft region — this ratio is the whole PCSS idea
    float penumbraRatio = (currentDepth - avgBlockerDepth) / max(avgBlockerDepth, 0.0001);
    float filterRadius = clamp(penumbraRatio * pc.lightSize, 0.0005, 0.02);

    // PCF at that computed radius
    float shadow = 0.0;
    for (int i = 0; i < PCF_SAMPLES; i++) {
        vec2 o = POISSON[i];
        vec2 r = vec2(o.x * rotCos - o.y * rotSin, o.x * rotSin + o.y * rotCos);
        float sampleDepth = texture(shadowMap, projCoords.xy + r * filterRadius).r;
        shadow += (currentDepth - bias > sampleDepth) ? 0.0 : 1.0;
    }

    return shadow / float(PCF_SAMPLES);
}

vec3 SampleRamp(float t, int rampIndex)
{
    float f = clamp(t, 0.0, 1.0) * float(RAMP_RESOLUTION - 1);
    int i0 = int(floor(f));
    int i1 = min(i0 + 1, RAMP_RESOLUTION - 1);
    int base = rampIndex * RAMP_RESOLUTION;
    return mix(rampBuffer.colors[base + i0].rgb,
               rampBuffer.colors[base + i1].rgb, f - float(i0));
}

// Procedural patterns that warp where the ramp transitions, giving the
// penumbra a dappled, hatched, or halftone edge instead of a clean gradient

float PenumbraPattern(int mode, vec2 uv, float scale)
{
    if (mode == 1) {                                  // noise
        return Hash(floor(uv * scale));
    } else if (mode == 2) {                           // hatch
        return 0.5 + 0.5 * sin((uv.x + uv.y) * scale * 3.14159);
    } else if (mode == 3) {                           // halftone
        vec2 c = fract(uv * scale) - 0.5;
        return smoothstep(0.35, 0.15, length(c));
    }
    return 0.5;
}

void main() {
    vec3 albedo = texture(diffuseSampler, fragTexCoord).rgb;
    albedo *= pc.colorTint.rgb;
    vec2 mr = texture(metallicRoughnessSampler, fragTexCoord).gb;
    float roughness = clamp(mr.x * pc.roughness, 0.05, 1.0);
    float metallic = clamp(mr.y * pc.metallic, 0.0, 1.0);

    float clearcoatFactor = pc.clearcoatFactor;
    float clearcoatRoughness = pc.clearcoatRoughness;

    vec3 N = normalize(fragNormalWorld);
    vec3 geometricN = N;   // <-- SSR G-buffer uses THIS (pre-normalmap, pre-flake). Do not change.

    // Per-vertex tangent frame (crisp on curved surfaces, no screen-space wobble).
    vec3 T = normalize(fragTangentWorld.xyz);
    // Gram-Schmidt re-orthogonalize against the interpolated normal.
    T = normalize(T - N * dot(N, T));
    vec3 approxTangent = T;
    vec3 approxBitangent = cross(N, T) * fragTangentWorld.w;   // w carries handedness

    vec2 nUV = fragTexCoord * pc.normalUVTransform.xy + pc.normalUVTransform.zw;
    vec3 nTex = texture(normalSampler, nUV).xyz * 2.0 - 1.0;
    
    // If the map looks inverted (bumps read as dents), flip green: nTex.y = -nTex.y;
    mat3 TBN = mat3(approxTangent, approxBitangent, N);
    N = normalize(TBN * nTex);

    // Flake perturbs the normal-mapped surface (shading only, never the SSR normal).
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
    vec3 sunLight = totalLight;

    // Determines which cluster this fragment belongs to
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

    // Coloured penumbra: retint the sun's contribution as it falls into shadow
    // Applied to the sun only, since surfaceShadow describes the sun's occlusion

    vec3 pointLight = totalLight - sunLight;

    float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    
    float penumbraT = 1.0 - surfaceShadow + (dither - 0.5) * (1.0 / float(PCF_SAMPLES));
    penumbraT = clamp(penumbraT, 0.0, 1.0);

    int patternMode = int(ubo.penumbraPattern.x);
    if (patternMode > 0) {
        float p = PenumbraPattern(patternMode, fragTexCoord, ubo.penumbraPattern.y);
        // Mask by the transition band: peaks at penumbraT = 0.5, zero at both ends
        // Fully lit and fully shadowed areas are left alone
        float edgeMask = 4.0 * penumbraT * (1.0 - penumbraT);
        penumbraT = clamp(penumbraT + (p - 0.5) * ubo.penumbraParams.w * edgeMask, 0.0, 1.0);
    }

    float bands = ubo.penumbraParams.z;
    if (bands > 0.5) {
        penumbraT = floor(penumbraT * bands) / bands;
    }

    vec3 rampColor = SampleRamp(penumbraT, int(ubo.penumbraParams.y));

    const vec3 LUMA = vec3(0.299, 0.587, 0.114);
    float lum = dot(sunLight, LUMA);
    float rampLum = max(dot(rampColor, LUMA), 0.001);
    vec3 tinted = rampColor * (lum / rampLum);

    sunLight = mix(sunLight, tinted, ubo.penumbraParams.x * penumbraT);
    totalLight = sunLight + pointLight;

    vec3 baseLayer = totalLight;

    float avgFc = clamp(clearcoatFactor * 0.5, 0.0, 1.0);
    vec3 outgoing = baseLayer * (1.0 - avgFc) + totalClearcoat;

    // IBL ambient
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
    float ambientOcclusion = mix(0.35, 1.0, surfaceShadow);
    vec3 finalColor = ambient * ambientOcclusion + outgoing;

    outColor = vec4(finalColor, 1.0);
    
    // DEBUGGING
    // outColor = vec4(texture(normalSampler, fragTexCoord).xyz, 1.0);
    //outColor = vec4(fract(fragTexCoord), 0.0, 1.0);

    // Screen-space motion in UV units: where this pixel was, minus where it is.
    vec2 currentNDC = fragClipPos.xy / fragClipPos.w;
    vec2 prevNDC = fragPrevClipPos.xy / fragPrevClipPos.w;
    outMotion = (prevNDC - currentNDC) * 0.5;
    outNormal = vec4(geometricN * 0.5 + 0.5, 1.0);

    outMaterial = vec2(roughness, metallic);

}