#pragma once
#include <cstdint>

#include "AppState.h"

class Camera;
class ImGuiManager;

// Per-frame scalars the UI displays but doesn't own
struct EditorFrameInfo
{
    float    deltaTime = 0.0f;
    int      haltonIndex = 0;
    int      jitterPhases = 8;
    uint32_t viewportWidth = 1920;
    uint32_t viewportHeight = 1080;
};

// Draws the gizmo and all four editor windows
void DrawEditorUI(Scene& scene, RenderSettings& settings,
    const Camera& camera, const EditorFrameInfo& info);