#version 450

// Linear mirror SSR. Reconstruct view-space position from depth, reflect the
// view ray about the view-space normal, march the reflection ray in view space,
// project each step to screen UV, compare against sampled depth. On hit, sample HDR.

layout(binding = 0) uniform sampler2D hdrTex;     // lit scene (what we reflect)
layout(binding = 1) uniform sampler2D depthTex;   // scene depth (nonlinear [0,1])
layout(binding = 2) uniform sampler2D normalTex;  // world-space geometric normal, [0,1]-encoded
layout(binding = 3) uniform sampler2D hizTex;     // Hi-Z pyramid: R=min linear depth, G=max linear depth
layout(binding = 4) uniform sampler2D materialTex; // R=roughness, G=metallic


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

float EdgeFade(vec2 uv) {
    // 0 at the very edge, 1 well inside; fade band ~10% of screen
    vec2 f = smoothstep(vec2(0.0), vec2(0.1), uv) * (1.0 - smoothstep(vec2(0.9), vec2(1.0), uv));
    return f.x * f.y;
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

    float roughness = texture(materialTex, inUV).r;
    float roughFade = 1.0 - smoothstep(0.25, 0.45, roughness);   // 1 below 0.25, ramps to 0 by 0.45
    if (roughFade <= 0.0) { outSSR = vec4(0.0); return; }         // fully rough: skip the march entirely

    // March the reflection ray. We advance in view space but use the Hi-Z pyramid
    // to skip empty space: at each step, read the min/max linear depth of the current
    // screen cell at mip level; if the ray segment can't intersect the surface in
    // that cell (its linear depth is nearer than the cell's min), skip ahead and climb
    // a mip. If it might intersect (overlaps the cell's depth range), descend for detail.
    
    vec3 rayPos = viewPos + reflDir * (0.05 + viewPos.z * -0.01);  // bias grows with depth    
    int level = 0;                            // start fine, climb as we skip empty space
    
    int maxLevel = min(pc.hizMipCount - 1, 6);

    vec4 hitColor = vec4(0.0);

    for (int i = 0; i < pc.maxSteps; i++) {
        bool ok;
        vec2 uv = ProjectToUV(rayPos, ok);
        if (!ok || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;

        // Ray's linear depth at this point (view z is negative, linear eye distance = -z).
        float rayLinZ = -rayPos.z;
        float thick = pc.thickness * (1.0 + rayLinZ * 0.05);   // grows with distance; tune the 0.05


        // Read the Hi-Z cell at this mip: R=min, G=max linear depth in the cell's footprint
        vec2 cell = textureLod(hizTex, uv, float(level)).rg;
        float cellMin = cell.x;
        float cellMax = cell.y;

        // Does the ray's depth fall within (or past) the cell's occupied depth range?
        // If the ray is still NEARER than everything in the cell (rayLinZ < cellMin - thickness),
        // the cell is empty in front of the ray: skip ahead, climb a mip to skip faster.
        if (rayLinZ < cellMin - thick || rayLinZ > cellMax + thick) {
            // Cell count at this mip
            vec2 mipSize = pc.screenSize / exp2(float(level));

            // Current UV and a UV a tiny bit further along the ray (to get UV-space direction)
            bool ok0, ok1;
            vec2 uv0 = ProjectToUV(rayPos, ok0);
            vec2 uv1 = ProjectToUV(rayPos + reflDir * 0.01, ok1);
            vec2 uvDir = uv1 - uv0;                        // ray direction in UV space (unnormalized)

            // Which cell are we in, and where are its far edges (in the direction of travel)?
            vec2 cellUV = uv0 * mipSize;                   // position in cell units
            vec2 cellEdge = floor(cellUV) + step(0.0, uvDir);   // next edge in each axis (0 or 1 side)
            vec2 edgeUV = cellEdge / mipSize;              // UV of the next cell boundary per axis

            // Parametric distance to each axis boundary: how far along uvDir to reach edgeUV
            vec2 tEdge = (edgeUV - uv0) / (uvDir + vec2(1e-6));   // avoid div0
            float tCell = min(tEdge.x, tEdge.y);           // nearest boundary
            tCell = max(tCell, 0.0);

            // Step the ray that far in view space, plus a small epsilon to cross into the next cell.
            // tCell is in units of the 0.01 view-space probe, so scale back:
            rayPos += reflDir * (tCell * 0.01 + 1e-4);
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
                if (delta > 0.0 && delta < thick) {
                    float edge = EdgeFade(uv);
                    hitColor = vec4(texture(hdrTex, uv).rgb, edge * roughFade);   // alpha = fade, not just 1.0
                    break;
                }
            }

            // No hit at mip0: nudge forward a small step and keep going.
            vec2 mipSize = pc.screenSize;
            bool okp;
            vec2 uv1 = ProjectToUV(rayPos + reflDir * 0.01, okp);
            vec2 uvDir = uv1 - uv;
            vec2 cellUV = uv * mipSize;
            vec2 cellEdge = floor(cellUV) + step(0.0, uvDir);
            vec2 edgeUV = cellEdge / mipSize;
            vec2 tEdge = (edgeUV - uv) / (uvDir + vec2(1e-6));
            float tCell = max(min(tEdge.x, tEdge.y), 0.0);
            rayPos += reflDir * (tCell * 0.01 + 1e-4);

        } else {

            // Potential intersection at a coarse mip: descend for precision, don't advance.
            level = level - 1;
        }
    }

    outSSR = hitColor;
}