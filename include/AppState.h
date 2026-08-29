#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "SceneTypes.h"
#include "UniformBufferObject.h"   // TM_ACES and friends

// Every tunable that used to be a static global in main.cpp. Grouped so a
// single reference can be threaded through the UI and the frame recorder
// instead of thirty loose parameters.
enum class SelectionType { None, Object, Light };

struct TAASettings
{
    bool  jitterEnabled = true;
    bool  resolveEnabled = true;
    float blendAlpha = 0.1f;   // 1.0 = passthrough, 0.1 = normal accumulation
};

struct ExposureSettings
{
    int   tonemapMode = TM_ACES;
    bool  autoExposure = true;
    float manualEV = 0.0f;
    float exposure = 1.0f;   // what the tonemap pass consumes
    float smoothedExposure = 1.0f;   // internal adaptation state
    float measuredLuminance = 0.0f;   // last frame's 1x1 readback
    float keyValue = 0.18f;  // middle-grey target
    float minExposure = 0.25f;
    float maxExposure = 4.0f;
    float adaptSpeedBright = 3.0f;   // fast: adapting to a brighter scene
    float adaptSpeedDark = 1.0f;   // slow: adapting to a darker scene

    // Framerate-independent exponential approach toward the target exposure.
    void Update(float deltaTime);
};

struct SSRSettings
{
    bool  enabled = true;
    float reflectivity = 0.6f;
    int   maxSteps = 64;
    float stepSize = 0.25f;
    float thickness = 0.001f;
};

struct SSAOSettings
{
    bool  enabled = true;
    float radius = 0.5f;
    float bias = 0.025f;
    float power = 1.5f;
};

struct TurntableSettings
{
    bool  autoRotate = false;
    float speedDegPerSec = 20.0f;
};

struct RenderSettings
{
    TAASettings       taa;
    ExposureSettings  exposure;
    SSRSettings       ssr;
    SSAOSettings      ssao;
    TurntableSettings turntable;

    int  debugView = 0;              // index into kDebugViewNames
    bool customShadowPenumbra = false;
    bool showGui = true;
};

struct SunLight
{
    glm::vec3 direction = { -0.5f, 1.0f, -0.3f };
    float     kelvin = 5500.0f;
    float     intensity = 3.0f;
    float     size = 0.05f;
};

struct Scene
{
    std::vector<SceneObject> objects;
    std::vector<SceneLight>  lights;
    SunLight                 sun;

    SelectionType selectionType = SelectionType::None;
    int           selectedIndex = -1;

    bool HasSelectedObject() const {
        return selectionType == SelectionType::Object &&
            selectedIndex >= 0 && selectedIndex < static_cast<int>(objects.size());
    }
    bool HasSelectedLight() const {
        return selectionType == SelectionType::Light &&
            selectedIndex >= 0 && selectedIndex < static_cast<int>(lights.size());
    }

    // Advances the selected object's turntable angle, then rebuilds every
    // object's transform as base (gizmo translate/scale) * turntable rotation.
    // Must run BEFORE the per-object UBO update so model/prevModel capture the
    // correct one-frame delta for TAA motion vectors.
    void UpdateTransforms(const TurntableSettings& turntable, float deltaTime);
};

// Frame-to-frame bookkeeping for TAA: history ping-pong parity, jitter phase,
// and the previous-frame matrices that motion vectors are derived from.
struct TemporalState
{
    std::vector<glm::mat4> prevModel;
    glm::mat4              prevViewProj = glm::mat4(1.0f);
    int                    frameIndex = 0;

    // Read these BEFORE calling Advance().
    int  HistoryRead()  const { return frameIndex & 1; }
    int  HistoryWrite() const { return HistoryRead() ^ 1; }
    bool IsFirstFrame() const { return frameIndex == 0; }
    int  JitterPhase(int phaseCount) const { return (frameIndex % phaseCount) + 1; }

    void Advance() { frameIndex++; }

    // Grows prevModel if objects were added; safe to call every frame.
    void Sync(size_t objectCount);

    // Snapshot at end of frame: this frame's transforms become next frame's
    // "previous". Call AFTER recording, not before.
    void Capture(const Scene& scene, const glm::mat4& viewProj);
};

inline constexpr const char* kTonemapNames[] = {
    "Reinhard", "Reinhard Extended", "ACES", "AgX", "AgX Punchy", "GT7"
};

inline constexpr const char* kDebugViewNames[] = {
    "Off (normal render)", "HDR", "Motion", "Normal", "Depth", "SSR", "SSAO"
};