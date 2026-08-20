#version 450
layout(binding = 0) uniform sampler2D aoTex;      // raw AO (R8)
layout(binding = 1) uniform sampler2D depthTex;   // for edge-aware rejection

layout(push_constant) uniform BlurPush {
    vec2 texelSize;      // 1/width, 1/height
    float depthThreshold; // reject samples beyond this linear-depth diff
    float _pad;
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outAO;

void main() {
    float centerDepth = texture(depthTex, inUV).r;
    float sum = 0.0;
    float weight = 0.0;
    // 4x4 neighborhood (the classic SSAO blur kernel)
    for (int x = -2; x < 2; x++) {
        for (int y = -2; y < 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * pc.texelSize;
            vec2 uv = inUV + offset;
            float sampleDepth = texture(depthTex, uv).r;
            // Reject sa mples across a depth edge (keeps AO from bleeding across silhouettes)
            if (abs(sampleDepth - centerDepth) < pc.depthThreshold) {
                sum += texture(aoTex, uv).r;
                weight += 1.0;
            }
        }
    }
    outAO = (weight > 0.0) ? (sum / weight) : texture(aoTex, inUV).r;
}