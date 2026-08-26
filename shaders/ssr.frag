#version 450

// Inspired from Uludag / Sakib Saikia SSR Setup.

// Transform the reflection ray's start & end into screen space ONCE, then march
// the straight screen-space line by Hi-Z cell DDA
layout(binding = 0) uniform sampler2D colorBuffer;    // lit scene (hdrTex)  -> "color"
layout(binding = 1) uniform sampler2D depthBuffer;    // scene depth [0,1]   -> "depth"
layout(binding = 2) uniform sampler2D normalBuffer;   // world normal [0,1]  -> "normal"
layout(binding = 3) uniform sampler2D hiZBuffer;      // Hi-Z pyramid (CP-B) -> "HiZ"
layout(binding = 4) uniform sampler2D materialBuffer; // R=rough, G=metal

layout(push_constant) uniform SSRPush {
    mat4 invProj;
    mat4 view;
    mat4 proj;
    vec2 screenSize;
    float nearZ;
    float farZ;
    int   maxSteps;
    float stepSize;
    float thickness;
    int   hizMipCount;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outSSR;

// View-space position from a UV + nonlinear depth.
vec3 ViewPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 p = pc.invProj * ndc;
    return p.xyz / p.w;
}

float EdgeFade(vec2 uv) {
    vec2 f = smoothstep(vec2(0.0), vec2(0.1), uv) * (1.0 - smoothstep(vec2(0.9), vec2(1.0), uv));
    return f.x * f.y;
}

void main()
{
    float depth = texture(depthBuffer, inUV).r;
    if (depth >= 1.0) { outSSR = vec4(0.0); return; }   // sky

    float roughness = texture(materialBuffer, inUV).r;
    float roughFade = 1.0 - smoothstep(0.25, 0.45, roughness);
    if (roughFade <= 0.0) { outSSR = vec4(0.0); return; }

    // Build the reflection ray in view space
    vec3 rayOriginVS = ViewPosFromDepth(inUV, depth);           // "rayOrigin"
    vec3 normalWS    = texture(normalBuffer, inUV).xyz * 2.0 - 1.0;
    vec3 normalVS    = normalize(mat3(pc.view) * normalWS);
    vec3 toCameraVS  = normalize(rayOriginVS);                  // origin->cam dir is -normalize(pos), use normalize(pos) as incident
    vec3 rayDirVS    = reflect(toCameraVS, normalVS);           // "rayDir"

    // Ray endpoints in VIEW space
    float maxDistance = 30.0;                                   // "maxDistance"
    vec3 rayStartVS = rayOriginVS + rayDirVS * 0.05;            // small start bias
    vec3 rayEndVS   = rayOriginVS + rayDirVS * maxDistance;

    // Project endpoints to CLIP, then to SCREEN space
    vec4 clipStart = pc.proj * vec4(rayStartVS, 1.0);
    vec4 clipEnd   = pc.proj * vec4(rayEndVS,   1.0);

    // Clip the end to in front of camera if it went behind (w <= 0).
    if (clipEnd.w <= 0.0) {
        float wNear = 1e-4;
        float tClip = (wNear - clipStart.w) / (clipEnd.w - clipStart.w);
        rayEndVS = mix(rayStartVS, rayEndVS, clamp(tClip, 0.0, 1.0));
        clipEnd  = pc.proj * vec4(rayEndVS, 1.0);
    }

    // Perspective divide -> NDC -> screen. .z carries nonlinear depth [0,1].
    vec3 rayStartSS = vec3((clipStart.xy / clipStart.w) * 0.5 + 0.5, clipStart.z / clipStart.w);
    vec3 rayEndSS   = vec3((clipEnd.xy   / clipEnd.w)   * 0.5 + 0.5, clipEnd.z   / clipEnd.w);

    vec4 result = vec4(0.0);

    vec2 rayDirSS = rayEndSS.xy - rayStartSS.xy;   // 2D screen-space direction (UV units)
    float rayLenSS = length(rayDirSS);
    if (rayLenSS < 1e-6) { outSSR = vec4(0.0); return; }

    int level = 0;
    int maxLevel = min(pc.hizMipCount - 1, 6);
    float t = 0.0;                                  // parameter along the ray, 0..1
    float tEps = (2.0 / max(pc.screenSize.x, pc.screenSize.y)) / rayLenSS;  // cross-cell nudge

    for (int i = 0; i < pc.maxSteps; i++) {
        vec3 sampleSS = mix(rayStartSS, rayEndSS, t);
        vec2 sampleUV = sampleSS.xy;
        if (t > 1.0 || sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
                       sampleUV.y < 0.0 || sampleUV.y > 1.0)
            break;

        float rayZ = sampleSS.z;                    // nonlinear depth (same space as pyramid now)

        // Hi-Z cell at this mip: R = min, G = max nonlinear depth in the cell footprint.
        vec2 cell = textureLod(hiZBuffer, sampleUV, float(level)).rg;
        float cellMin = cell.x;
        float cellMax = cell.y;

        // Exact t to the next spatial cell boundary at this mip (UV is linear in t).
        vec2 mipSize  = pc.screenSize / exp2(float(level));
        vec2 cellIdx  = floor(sampleUV * mipSize);
        vec2 nextEdge = (cellIdx + step(0.0, rayDirSS)) / mipSize;
        vec2 tToEdge  = (nextEdge - rayStartSS.xy) / (rayDirSS + vec2(1e-6));
        float tCellCross = min(tToEdge.x, tToEdge.y);

        if (rayZ < cellMin - pc.thickness || rayZ > cellMax + pc.thickness) {
            // Ray outside the cell's occupied depth band -> skip to boundary, climb a mip.
            t = max(tCellCross, t) + tEps;
            level = min(level + 1, maxLevel);
        } else {
            // Ray within the [min,max] band.
            if (level == 0) {
                // Finest level: exact hit test against the real surface depth.
                float sceneZ = texture(depthBuffer, sampleUV).r;
                if (rayZ > sceneZ + 1e-5 && rayZ < sceneZ + pc.thickness) {
                    result = vec4(texture(colorBuffer, sampleUV).rgb, EdgeFade(sampleUV) * roughFade);
                    break;
                }
                t = max(tCellCross, t) + tEps;   // no hit, step one cell forward
            } else {
                level = level - 1;               // descend for precision
            }
        }
    }
    outSSR = result;
}