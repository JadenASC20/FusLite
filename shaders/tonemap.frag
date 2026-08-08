#version 450

layout(binding = 0) uniform sampler2D hdrColor;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    int tonemapMode;
    float exposure;
} pc;

// Techniques implemented from github.com/dmnsgn/glsl-tone-map/tree/main
// GT7 Implemented from SIGGRAPH blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp


// Reinhard

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 reinhard(vec3 c) {
    float l = luminance(c);
    float ln = l / (1.0 + l);
    return c * (ln / max(l, 1e-5));
}

vec3 reinhardExtended(vec3 c) {
    const float whitePoint = 4.0; // tune; values >= this map to 1.0
    float l = luminance(c);
    float ln = l * (1.0 + l / (whitePoint * whitePoint)) / (1.0 + l);
    return c * (ln / max(l, 1e-5));
}

//ACES

const mat3 ACESInput = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);

const mat3 ACESOutput = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 aces(vec3 c) {
    c = ACESInput * c;
    c = RRTAndODTFit(c);
    return clamp(ACESOutput * c, 0.0, 1.0);
}

// AgX

const mat3 LINEAR_REC2020_TO_LINEAR_SRGB = mat3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1329, -0.1006,
    -0.0728, -0.0083,  1.1187);

const mat3 LINEAR_SRGB_TO_LINEAR_REC2020 = mat3(
    0.6274, 0.0691, 0.0164,
    0.3293, 0.9195, 0.0880,
    0.0433, 0.0113, 0.8956);

const mat3 AgXInsetMatrix = mat3(
    0.856627153315983,  0.137318972929847,  0.11189821299995,
    0.0951212405381588, 0.761241990602591,  0.0767994186031903,
    0.0482516061458583, 0.101439036467562,  0.811302368396859);

const mat3 AgXOutsetMatrix = mat3(
     1.1271005818144368,  -0.1413297634984383,  -0.14132976349843826,
    -0.11060664309660323,  1.157823702216272,   -0.11060664309660294,
    -0.016493938717834573,-0.016493938717834257, 1.2519364065950405);

const float AgxMinEv = -12.47393;
const float AgxMaxEv =  4.026069;

// AgX (slope/offset/power/saturation)

vec3 agxCdl(vec3 color, vec3 slope, vec3 offset, vec3 power, float saturation) {
    color = LINEAR_SRGB_TO_LINEAR_REC2020 * color;
    color = AgXInsetMatrix * color;
    color = max(color, 1e-10);
    color = clamp(log2(color), AgxMinEv, AgxMaxEv);
    color = (color - AgxMinEv) / (AgxMaxEv - AgxMinEv);
    color = clamp(color, 0.0, 1.0);

    vec3 x2 = color * color;
    vec3 x4 = x2 * x2;
    color = + 15.5   * x4 * x2
            - 40.14  * x4 * color
            + 31.96  * x4
            - 6.868  * x2 * color
            + 0.4298 * x2
            + 0.1191 * color
            - 0.00232;

    color = pow(max(color * slope + offset, 0.0), power);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = luma + saturation * (color - luma);

    color = AgXOutsetMatrix * color;
    color = pow(max(vec3(0.0), color), vec3(2.2));
    color = LINEAR_REC2020_TO_LINEAR_SRGB * color;
    return clamp(color, 0.0, 1.0);
}

vec3 agx(vec3 c) {
    return agxCdl(c, vec3(1.0), vec3(0.0), vec3(1.0), 1.0);
}
vec3 agxPunchy(vec3 c) {
    return agxCdl(c, vec3(1.0), vec3(0.0), vec3(1.35), 1.4);
}

// GT7

const mat3 REC709_TO_REC2020 = mat3(
    0.627402, 0.069095, 0.016394,
    0.329292, 0.919544, 0.088028,
    0.043306, 0.011360, 0.895578);

const mat3 REC2020_TO_REC709 = mat3(
     1.660491, -0.124550, -0.018151,
    -0.587641,  1.132900, -0.100579,
    -0.072850, -0.008349,  1.118730);

// ST-2084 (PQ) EOTF / inverse, in framebuffer scale (1.0 = 100 cd/m^2)
const float GT7_REF_LUM = 100.0;   // cd/m^2
const float GT7_PQ_C    = 10000.0;

float gt7_eotf2084(float n) {
    n = clamp(n, 0.0, 1.0);
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float np = pow(n, 1.0 / m2);
    float l  = max(np - c1, 0.0);
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0 / m1);
    return (l * GT7_PQ_C) / GT7_REF_LUM;
}

float gt7_inverseEotf2084(float v) {
    float physical = v * GT7_REF_LUM;
    float y = physical / GT7_PQ_C;
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float ym = pow(max(y, 0.0), m1);
    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0 + c3 * ym)));
}

vec3 gt7_rgbToICtCp(vec3 rgb) {
    float l = (rgb.r * 1688.0 + rgb.g * 2146.0 + rgb.b * 262.0) / 4096.0;
    float m = (rgb.r * 683.0  + rgb.g * 2951.0 + rgb.b * 462.0) / 4096.0;
    float s = (rgb.r * 99.0   + rgb.g * 309.0  + rgb.b * 3688.0) / 4096.0;
    float lPQ = gt7_inverseEotf2084(l);
    float mPQ = gt7_inverseEotf2084(m);
    float sPQ = gt7_inverseEotf2084(s);
    return vec3(
        (2048.0  * lPQ + 2048.0  * mPQ) / 4096.0,
        (6610.0  * lPQ - 13613.0 * mPQ + 7003.0 * sPQ) / 4096.0,
        (17933.0 * lPQ - 17390.0 * mPQ - 543.0  * sPQ) / 4096.0);
}

vec3 gt7_iCtCpToRgb(vec3 ictcp) {
    float l = ictcp.x + 0.00860904 * ictcp.y + 0.11103  * ictcp.z;
    float m = ictcp.x - 0.00860904 * ictcp.y - 0.11103  * ictcp.z;
    float s = ictcp.x + 0.560031   * ictcp.y - 0.320627 * ictcp.z;
    float lLin = gt7_eotf2084(l);
    float mLin = gt7_eotf2084(m);
    float sLin = gt7_eotf2084(s);
    return vec3(
        max( 3.43661   * lLin - 2.50645  * mLin + 0.0698454 * sLin, 0.0),
        max(-0.79133   * lLin + 1.9836   * mLin - 0.192271  * sLin, 0.0),
        max(-0.0259499 * lLin - 0.0989137* mLin + 1.12486   * sLin, 0.0));
}

float gt7_smoothStep(float x, float edge0, float edge1) {
    if (x < edge0) return 0.0;
    if (x > edge1) return 1.0;
    float t = (x - edge0) / (edge1 - edge0);
    return t * t * (3.0 - 2.0 * t);
}

float gt7_evaluateCurve(float x) {
    const float peakIntensity = 2.5;
    const float alpha         = 0.25;
    const float midPoint      = 0.538;
    const float linearSection = 0.444;
    const float toeStrength   = 1.280;

    float k  = (linearSection - 1.0) / (alpha - 1.0);
    float kA = peakIntensity * linearSection + peakIntensity * k;
    float kB = -peakIntensity * k * exp(linearSection / k);
    float kC = -1.0 / (k * peakIntensity);

    if (x < 0.0) return 0.0;

    float weightLinear = gt7_smoothStep(x, 0.0, midPoint);
    float weightToe    = 1.0 - weightLinear;
    float shoulder     = kA + kB * exp(x * kC);

    if (x < linearSection * peakIntensity) {
        float toeMapped = midPoint * pow(x / midPoint, toeStrength);
        return weightToe * toeMapped + weightLinear * x;
    } else {
        return shoulder;
    }
}

float gt7_chromaCurve(float x, float a, float b) {
    return 1.0 - gt7_smoothStep(x, a, b);
}

vec3 gt7(vec3 c) {
    const float gt7Exposure = 1.25; // can change
    c *= gt7Exposure;

    // Curve params (SDR): peak = 250/100 = 2.5
    const float peakIntensity = 2.5;
    const float sdrCorrection = 0.4;   // 1.0 / 2.5
    const float blendRatio    = 0.6;
    const float fadeStart     = 0.98;
    const float fadeEnd       = 1.16;

    vec3 rgb2020 = REC709_TO_REC2020 * c;
    rgb2020 = max(rgb2020, 0.0);

    // UCS of the original color
    vec3 ucs = gt7_rgbToICtCp(rgb2020);

    // Per-channel curved ("skewed") color, and its UCS
    vec3 skewed = vec3(
        gt7_evaluateCurve(rgb2020.r),
        gt7_evaluateCurve(rgb2020.g),
        gt7_evaluateCurve(rgb2020.b));
    vec3 skewedUcs = gt7_rgbToICtCp(skewed);

    // Luminance target in UCS
    float lumTargetUcs = gt7_rgbToICtCp(vec3(peakIntensity)).x;

    // Chroma scale from the fade curve
    float chromaScale = gt7_chromaCurve(ucs.x / lumTargetUcs, fadeStart, fadeEnd);

    // Luminance from the skewed color
    vec3 scaledUcs = vec3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale);
    vec3 scaledRgb = gt7_iCtCpToRgb(scaledUcs);

    // Blend per-channel result with UCS-scaled result
    vec3 blended = (1.0 - blendRatio) * skewed + blendRatio * scaledRgb;
    blended = min(blended, vec3(peakIntensity));

    // apply SDR correction
    vec3 out709 = REC2020_TO_REC709 * (sdrCorrection * blended);
    return clamp(out709, 0.0, 1.0);
}

vec3 applyToneMap(vec3 hdr, int mode) {
    if      (mode == 0) return reinhard(hdr);
    else if (mode == 1) return reinhardExtended(hdr);
    else if (mode == 2) return aces(hdr);
    else if (mode == 3) return agx(hdr);
    else if (mode == 4) return agxPunchy(hdr);
    else                return gt7(hdr);
}

void main() {
    vec3 hdr = texture(hdrColor, inUV).rgb;
    hdr *= pc.exposure;
    vec3 mapped = applyToneMap(hdr, pc.tonemapMode);
    outColor = vec4(mapped, 1.0);
}