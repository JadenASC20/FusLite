#version 450

layout(binding = 0) uniform sampler2D hdrColor;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// ACES filmic tonemap approximation (Narkowicz 2015)
vec3 ACESFilm(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(hdrColor, inUV).rgb;
    vec3 mapped = ACESFilm(hdr);

    // No manual gamma correction here — the swapchain image view is sRGB format,
    // so the hardware automatically encodes this linear output to sRGB on write.
    outColor = vec4(mapped, 1.0);
}