#version 450

// COMMIT 3 (passthrough) -> COMMIT 4 (blend) -> COMMIT 5 (sign fix).
// Compile to shaders/taa_resolve.frag.spv

layout(location = 0) in  vec2 uv;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D currentColor;   // this frame HDR
layout(binding = 1) uniform sampler2D motionVectors;   // RG16F, from triangle.frag
layout(binding = 2) uniform sampler2D historyColor;    // prev resolved frame

layout(push_constant) uniform Push {
    vec2  texelSize;    // 1.0 / resolution
    float blendAlpha;   // 1.0 = pure current (passthrough). 0.1 = normal TAA.
    int   firstFrame;   // 1 -> ignore history entirely
} pc;

// Clamp in YCoCg: separates luma from chroma so the neighborhood box hugs the
// real gamut instead of a loose RGB cube. Biggest single ghosting win.
vec3 RGBToYCoCg(vec3 c) {
    return vec3( 0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                 0.5  * c.r            - 0.5  * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b );
}
vec3 YCoCgToRGB(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

void main() {
    vec3 current = texture(currentColor, uv).rgb;

    // Passthrough paths: frame 0, or blendAlpha driven to 1.0 from the GUI.
    // COMMIT 3 leaves blendAlpha at 1.0 to prove the plumbing + copy path.
    if (pc.firstFrame == 1 || pc.blendAlpha >= 0.999) {
        outColor = vec4(current, 1.0);
        return;
    }

    // --- Reproject ---
    // triangle.frag writes outMotion = (prevNDC - currentNDC) * 0.5, so in this
    // engine's UV convention history lives at uv + motion. If the SIGN TEST
    // (pan vertical vs horizontal) shows one axis ghosting, flip that axis here.
    vec2 motion  = texture(motionVectors, uv).rg;
    vec2 histUV  = uv + motion;                 // <-- SIGN LEVER (commit 5)

    // Off-screen history is invalid -> fall back to current, no blend.
    if (any(lessThan(histUV, vec2(0.0))) || any(greaterThan(histUV, vec2(1.0)))) {
        outColor = vec4(current, 1.0);
        return;
    }

    vec3 history = texture(historyColor, histUV).rgb;

    // --- Neighborhood color box (variance clip, Salvi/Karis) ---
    vec3 cMin = vec3( 1e9);
    vec3 cMax = vec3(-1e9);
    vec3 m1   = vec3(0.0);
    vec3 m2   = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 s = RGBToYCoCg(texture(currentColor, uv + vec2(x, y) * pc.texelSize).rgb);
            cMin = min(cMin, s);
            cMax = max(cMax, s);
            m1  += s;
            m2  += s * s;
        }
    }
    vec3 mean  = m1 / 9.0;
    vec3 sigma = sqrt(max(vec3(0.0), m2 / 9.0 - mean * mean));
    const float gamma = 1.0;                    // lower = tighter clamp, less ghost, more flicker
    vec3 boxMin = max(cMin, mean - gamma * sigma);
    vec3 boxMax = min(cMax, mean + gamma * sigma);

    vec3 histYCoCg = clamp(RGBToYCoCg(history), boxMin, boxMax);
    history = YCoCgToRGB(histYCoCg);

    outColor = vec4(mix(history, current, pc.blendAlpha), 1.0);
}
