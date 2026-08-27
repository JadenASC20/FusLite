#version 450
layout(binding = 0) uniform sampler2D hdrTex;      // lit scene
layout(binding = 1) uniform sampler2D ssrTex;      // rgb = SSR hit color, a = hit mask
layout(binding = 2) uniform sampler2D normalTex;   // world normal, [0,1]
layout(binding = 3) uniform sampler2D depthTex;    // scene depth [0,1]
layout(binding = 4) uniform samplerCube prefilteredMap;  // IBL prefiltered specular
layout(binding = 5) uniform sampler2D materialTex; // R=roughness, G=metallic
layout(binding = 6) uniform sampler2D ssaoTex;  

layout(push_constant) uniform CompPush {
    mat4 invProj;
    mat4 invView;
    vec4 cameraPos;
    float reflectivity;
    float aoStrength;
    float _p1, _p2;
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

// Depth-aware blur of the AO buffer, comparing LINEAR view-space depth so the
// reject threshold means the same thing near and far. Centred 3x3 kernel.
float LinearizeDepth(float d) {
    // Reconstruct view-space Z magnitude from non-linear depth via invProj.
    vec4 ndc = vec4(0.0, 0.0, d, 1.0);
    vec4 v = pc.invProj * ndc;
    return abs(v.z / v.w);
}

float BlurAO(vec2 uv, float centerRawDepth) {
    vec2 texel = 1.0 / vec2(textureSize(ssaoTex, 0));
    float centerZ = LinearizeDepth(centerRawDepth);
    float sum = 0.0;
    float weight = 0.0;
    // Centred 3x3 (x,y in -1..1). Use -1..2 if you want a wider 4-wide kernel,
    // but keep it symmetric — do not use -2..1 (off-centre).
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 o = vec2(float(x), float(y)) * texel;
            float sd = texture(depthTex, uv + o).r;
            float sz = LinearizeDepth(sd);
            // Reject across depth edges, now in linear (world-unit) space.
            if (abs(sz - centerZ) < 0.1) {   // ~0.1 view-space units; tune
                sum += texture(ssaoTex, uv + o).r;
                weight += 1.0;
            }
        }
    }
    return (weight > 0.0) ? (sum / weight) : texture(ssaoTex, uv).r;
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

    // Roughness attenuates reflection MAGNITUDE (not just blur), so rough surfaces
    // (tires, interior) reflect weakly while smooth surfaces (paint, chrome) reflect strongly.
    // The global slider is now a master over per-material-correct strengths.
    float reflStrength = pow(1.0 - roughness, 2.0);           // smooth=1, rough=0
    
    float centerDepth = texture(depthTex, inUV).r;   // or reuse your existing depth sample
    
    float ao = BlurAO(inUV, centerDepth);
    ao = pow(ao, 2.0);    
    ao = mix(1.0, ao, pc.aoStrength);
    outColor = vec4((hdr + reflection * F * reflStrength) * ao, 1.0);

    return;

}