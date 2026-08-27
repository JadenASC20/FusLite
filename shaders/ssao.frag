#version 450
#define KERNEL_SIZE 32

layout(binding = 0) uniform sampler2D depthTex;
layout(binding = 1) uniform sampler2D normalTex;
layout(binding = 2) uniform sampler2D noiseTex;
layout(binding = 3) uniform Kernel { vec4 samples[KERNEL_SIZE]; } kern;

layout(push_constant) uniform SSAOPush {
    mat4 proj;
    mat4 invProj;
    mat4 view;
    vec2 screenSize;
    float radius;
    float bias;
    float power;
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outAO;

vec3 ReconstructViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 v = pc.invProj * ndc;
    return v.xyz / v.w;
}

void main() {
    float depth = texture(depthTex, inUV).r;
    if (depth >= 1.0) { outAO = 1.0; return; }

    vec3 fragPos = ReconstructViewPos(inUV, depth);
    vec3 worldN = texture(normalTex, inUV).xyz * 2.0 - 1.0;
    vec3 N = normalize(mat3(pc.view) * worldN);

    vec2 noiseScale = pc.screenSize / 4.0;
    vec3 randomVec = normalize(vec3(texture(noiseTex, inUV * noiseScale).xy * 2.0 - 1.0, 0.0));

    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    float occlusion = 0.0;
    int valid = 0;
    for (int i = 0; i < KERNEL_SIZE; i++) {
        vec3 samplePos = fragPos + (TBN * kern.samples[i].xyz) * pc.radius;
        vec4 offset = pc.proj * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        vec2 sampleUV = offset.xy * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        valid++;   // <-- THIS is the missing line: count samples that stayed on-screen

        float sd = texture(depthTex, sampleUV).r;
        float sampleZ = ReconstructViewPos(sampleUV, sd).z;
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleZ));
        if (sampleZ >= samplePos.z + pc.bias) occlusion += rangeCheck;
    }
    occlusion = (valid > 0) ? 1.0 - (occlusion / float(valid)) : 1.0;
    outAO = pow(clamp(occlusion, 0.0, 1.0), pc.power);
}