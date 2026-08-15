#version 450

// Linear mirror SSR. Reconstruct view-space position from depth, reflect the
// view ray about the view-space normal, march the reflection ray in view space,
// project each step to screen UV, compare against sampled depth. On hit, sample HDR.

layout(binding = 0) uniform sampler2D hdrTex;     // lit scene (what we reflect)
layout(binding = 1) uniform sampler2D depthTex;   // scene depth (nonlinear [0,1])
layout(binding = 2) uniform sampler2D normalTex;  // world-space geometric normal, [0,1]-encoded
layout(binding = 3) uniform sampler2D hizTex;     // Hi-Z pyramid: R=min linear depth, G=max linear depth

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
    int   hizMipCount;

} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outSSR;   // rgb = reflected color, a = hit mask (1=hit, 0=miss)

float LinearizeDepth(float d) {
    return (2.0 * pc.nearZ * pc.farZ) / (pc.farZ + pc.nearZ - d * (pc.farZ - pc.nearZ));
}

vec3 ReconstructViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 v = pc.invProj * ndc;
    return v.xyz / v.w;
}

// Project a view-space point to screen UV. Returns uv, sets ok = false if behind camera.
vec2 ProjectToUV(vec3 viewPos, out bool ok) {
    vec4 clip = pc.proj * vec4(viewPos, 1.0);
    ok = clip.w > 0.0;
    vec3 ndc = clip.xyz / max(clip.w, 1e-6);
    return ndc.xy * 0.5 + 0.5;
}

void main()
{
    float depth = texture(depthTex, inUV).r;
    if (depth >= 1.0) { outSSR = vec4(0.0); return; }   // sky

    vec3 viewPos = ReconstructViewPos(inUV, depth);
    vec3 worldN  = texture(normalTex, inUV).xyz * 2.0 - 1.0;
    vec3 viewN   = normalize(mat3(pc.view) * worldN);
    vec3 viewDir = normalize(viewPos);
    vec3 reflDir = reflect(viewDir, viewN);

    // March the reflection ray. We advance in view space but use the Hi-Z pyramid
    // to skip empty space: at each step, read the min/max linear depth of the current
    // screen cell at mip level; if the ray segment can't intersect the surface in
    // that cell (its linear depth is nearer than the cell's min), skip ahead and climb
    // a mip. If it might intersect (overlaps the cell's depth range), descend for detail.
    
    vec3 rayPos = viewPos + reflDir * 0.05;   // small bias off the surface
    int level = 0;                            // start fine, climb as we skip empty space
    
    int maxLevel = min(pc.hizMipCount - 1, 6);

    vec4 hitColor = vec4(0.0);

    for (int i = 0; i < pc.maxSteps; i++) {
        bool ok;
        vec2 uv = ProjectToUV(rayPos, ok);
        if (!ok || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        // Ray's linear depth at this point (view z is negative, linear eye distance = -z).
        float rayLinZ = -rayPos.z;

        // Read the Hi-Z cell at this mip: R=min, G=max linear depth in the cell's footprint
        vec2 cell = textureLod(hizTex, uv, float(level)).rg;
        float cellMin = cell.x;
        float cellMax = cell.y;

        // Does the ray's depth fall within (or past) the cell's occupied depth range?
        // If the ray is still NEARER than everything in the cell (rayLinZ < cellMin - thickness),
        // the cell is empty in front of the ray: skip ahead, climb a mip to skip faster.
        if (rayLinZ < cellMin - pc.thickness) {
            rayPos += reflDir * pc.stepSize *  (2.0 + float(level) * float(level) * 0.5);  // big skips at coarse mips
            level = min(level + 1, maxLevel);
            continue;
        }

        // The ray has reached the cell's depth range. If we're at the finest mip, test for a hit.
        if (level == 0) {
            
            // Compare against the actual surface depth at this pixel.
            float sceneDepth = texture(depthTex, uv).r;
            if (sceneDepth < 1.0) {
                float sceneLinZ = LinearizeDepth(sceneDepth);
                float delta = rayLinZ - sceneLinZ;
                if (delta > 0.0 && delta < pc.thickness) {
                    hitColor = vec4(texture(hdrTex, uv).rgb, 1.0);
                    break;
                }
            }

            // No hit at mip0: nudge forward a small step and keep going.
            rayPos += reflDir * pc.stepSize;

        } else {

            // Potential intersection at a coarse mip: descend for precision, don't advance.
            level = level - 1;
        }
    }

    outSSR = hitColor;
}