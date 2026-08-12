#version 450

// Linear mirror SSR. Reconstruct view-space position from depth, reflect the
// view ray about the view-space normal, march the reflection ray in view space,
// project each step to screen UV, compare against sampled depth. On hit, sample HDR.

layout(binding = 0) uniform sampler2D hdrTex;     // lit scene (what we reflect)
layout(binding = 1) uniform sampler2D depthTex;   // scene depth (nonlinear [0,1])
layout(binding = 2) uniform sampler2D normalTex;  // world-space geometric normal, [0,1]-encoded

layout(push_constant) uniform SSRPush {
    mat4 invProj;      // inverse of NO-JITTER projection
    mat4 view;         // world -> view (to bring world normal into view space)
    mat4 proj;         // NO-JITTER projection (view -> clip, for projecting march steps)
    vec2 screenSize;
    float nearZ;
    float farZ;
    int   maxSteps;    // linear march step count
    float stepSize;    // view-space distance per step
    float thickness;   // depth-compare tolerance (view-space units)
    float _pad;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outSSR;   // rgb = reflected color, a = hit mask (1=hit, 0=miss)

// Reconstruct VIEW-space position from a screen UV + its sampled nonlinear depth.
vec3 ReconstructViewPos(vec2 uv, float depth)
{
    // UV [0,1] -> NDC xy [-1,1]. Vulkan depth is already [0,1] = NDC z.
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProj * ndc;
    return viewPos.xyz / viewPos.w;   // perspective divide -> view space
}

void main()
{
    float depth = texture(depthTex, inUV).r;

    // Sky / far plane: nothing to reflect from. depth==1.0 is the cleared far plane.
    if (depth >= 1.0) {
        outSSR = vec4(0.0);
        return;
    }

    // View-space position and normal of THIS pixel.
    vec3 viewPos = ReconstructViewPos(inUV, depth);

    // World normal -> view normal. (Stored normal is world-space geometric.)
    vec3 worldN = texture(normalTex, inUV).xyz * 2.0 - 1.0;
    vec3 viewN  = normalize(mat3(pc.view) * worldN);

    // View-space reflection ray. Camera is at origin in view space, so the
    // direction FROM camera TO the surface is just normalize(viewPos).
    vec3 viewDir = normalize(viewPos);
    vec3 reflDir = reflect(viewDir, viewN);

    // March the reflection ray in view space, projecting each step to screen UV.
    vec3 marchPos = viewPos;
    vec4 hitColor = vec4(0.0);

    for (int i = 0; i < pc.maxSteps; i++) {
        marchPos += reflDir * pc.stepSize;

        // View -> clip -> NDC -> UV
        vec4 clip = pc.proj * vec4(marchPos, 1.0);
        if (clip.w <= 0.0) break;              // behind camera
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;

        // Off-screen: CP1 just stops (no fallback — that's CP3).
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        // Sample the scene depth at the ray's screen position, reconstruct its view pos.
        float sceneDepth = texture(depthTex, uv).r;
        if (sceneDepth >= 1.0) continue;       // ray over sky, keep going
        vec3 sceneViewPos = ReconstructViewPos(uv, sceneDepth);

        // Depth compare in view space. View z is negative (looking down -z), so the
        // ray is "behind" the scene surface when marchPos.z < sceneViewPos.z.
        float delta = sceneViewPos.z - marchPos.z;
        if (delta > 0.0 && delta < pc.thickness) {
            // On Hit: sample the lit scene color there.
            hitColor = vec4(texture(hdrTex, uv).rgb, 1.0);
            break;
        }
    }

    outSSR = hitColor;
}