#include "EditorUI.h"
#include "Camera.h"
#include "ColorTemperature.h"
#include "RampUtil.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {


    void BeginPanel(const char* title, ImVec2 pos)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    }

    void SectionHeader(const char* label)
    {
        ImGui::Separator();
        ImGui::Text("%s", label);
    }

    void DrawGizmo(Scene& scene, const Camera& camera, const EditorFrameInfo& info)
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0,
            static_cast<float>(info.viewportWidth),
            static_cast<float>(info.viewportHeight));

        if (!scene.HasSelectedObject()) return;

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
            static_cast<float>(info.viewportWidth) / static_cast<float>(info.viewportHeight),
            0.1f, 1000.0f);

        // Manipulates baseTransform (NOT transform), 
        // the turntable rotation is reapplied on top of it every frame in Scene::UpdateTransforms.
        ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(proj),
            ImGuizmo::TRANSLATE,
            ImGuizmo::WORLD,
            glm::value_ptr(scene.objects[scene.selectedIndex].baseTransform));
    }

    // Window 1: frame timing + debug view
    void DrawDebugPanel(RenderSettings& settings, const EditorFrameInfo& info)
    {
        BeginPanel("Debug Window", ImVec2(20, 20));

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Frame Time: %.2fms/frame (%.0f FPS)", 1000.0f / io.Framerate, io.Framerate);

        static float frameTimes[90] = {};
        static int   offset = 0;
        frameTimes[offset] = info.deltaTime * 1000.0f;
        offset = (offset + 1) % IM_ARRAYSIZE(frameTimes);
        ImGui::PlotLines("Frame Time Graph", frameTimes, IM_ARRAYSIZE(frameTimes), offset,
            nullptr, 0.0f, 33.0f, ImVec2(0, 60));

        SectionHeader("Debug View");
        ImGui::Combo("##debugview", &settings.debugView, kDebugViewNames, IM_ARRAYSIZE(kDebugViewNames));

        ImGui::End();
    }

    // Window 2: sun, TAA, tonemap, exposure, SSR, SSAO
    void DrawSunControls(SunLight& sun)
    {
        ImGui::Text("Lighting (Sun)");
        ImGui::SliderFloat3("Light Dir", &sun.direction.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Light Intensity", &sun.intensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Light Size", &sun.size, 0.001f, 0.2f, "%.3f");

        glm::vec3 preview = KelvinToRGB(sun.kelvin);
        ImGui::ColorButton("##sunPreview", ImVec4(preview.r, preview.g, preview.b, 1.0f), 0, ImVec2(40, 25));
        ImGui::SameLine();
        ImGui::SliderFloat("Light Color (K)", &sun.kelvin, 1000.0f, 12000.0f, "%.0f K");
    }

    void DrawTAAControls(TAASettings& taa, const EditorFrameInfo& info)
    {
        SectionHeader("TAA");
        ImGui::Checkbox("Jitter enabled", &taa.jitterEnabled);
        ImGui::Checkbox("Resolve enabled", &taa.resolveEnabled);
        ImGui::SliderFloat("Blend alpha", &taa.blendAlpha, 0.02f, 1.0f, "%.3f");
        ImGui::Text("Jitter phase: %d/%d", info.haltonIndex, info.jitterPhases);
    }

    void DrawExposureControls(ExposureSettings& e)
    {
        SectionHeader("Tonemap");
        ImGui::Combo("Tonemap", &e.tonemapMode, kTonemapNames, IM_ARRAYSIZE(kTonemapNames));

        SectionHeader("Auto-Exposure");
        ImGui::Text("Measured avg luminance: %.4f", e.measuredLuminance);
        ImGui::Text("Applied exposure: %.3f", e.exposure);
        ImGui::Checkbox("Auto exposure", &e.autoExposure);

        if (e.autoExposure) {
            ImGui::SliderFloat("Key value", &e.keyValue, 0.05f, 0.5f, "%.3f");
            ImGui::SliderFloat("Min exposure", &e.minExposure, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Max exposure", &e.maxExposure, 1.0f, 16.0f, "%.2f");
            ImGui::SliderFloat("Adapt speed (bright)", &e.adaptSpeedBright, 0.5f, 8.0f, "%.2f");
            ImGui::SliderFloat("Adapt speed (dark)", &e.adaptSpeedDark, 0.2f, 8.0f, "%.2f");
        }
        else {
            ImGui::SliderFloat("Exposure (EV)", &e.manualEV, -4.0f, 4.0f);
        }
    }

    void DrawSSRControls(SSRSettings& ssr)
    {
        SectionHeader("SSR");
        ImGui::Checkbox("SSR enabled", &ssr.enabled);
        if (!ssr.enabled) return;

        ImGui::SliderFloat("Reflectivity", &ssr.reflectivity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderInt("Max steps", &ssr.maxSteps, 8, 256);
        ImGui::SliderFloat("Step size", &ssr.stepSize, 0.05f, 1.0f, "%.3f");
        ImGui::SliderFloat("Thickness", &ssr.thickness, 0.0001f, 0.02f, "%.4f");
    }

    void DrawSSAOControls(SSAOSettings& ssao)
    {
        SectionHeader("SSAO");
        ImGui::Checkbox("SSAO enabled", &ssao.enabled);
        if (!ssao.enabled) return;

        ImGui::SliderFloat("AO radius", &ssao.radius, 0.05f, 2.0f, "%.3f");
        ImGui::SliderFloat("AO bias", &ssao.bias, 0.0f, 0.1f, "%.3f");
        ImGui::SliderFloat("AO power", &ssao.power, 0.5f, 4.0f, "%.2f");
    }

    void DrawScenePropertyPanel(Scene& scene, RenderSettings& settings, const EditorFrameInfo& info)
    {
        BeginPanel("Scene Property Window", ImVec2(20, 100));

        DrawSunControls(scene.sun);
        DrawTAAControls(settings.taa, info);
        DrawExposureControls(settings.exposure);
        DrawSSRControls(settings.ssr);
        DrawSSAOControls(settings.ssao);

        ImGui::End();
    }

    // Window 3: outliner
    void DrawOutliner(Scene& scene)
    {
        BeginPanel("Scene Outliner", ImVec2(1580, 20));

        ImGui::Text("Scene Lights:");
        for (int i = 0; i < static_cast<int>(scene.lights.size()); i++) {
            bool selected = (scene.selectionType == SelectionType::Light && scene.selectedIndex == i);
            if (ImGui::Selectable(scene.lights[i].name.c_str(), selected)) {
                scene.selectionType = SelectionType::Light;
                scene.selectedIndex = i;
            }
        }

        ImGui::Separator();
        ImGui::Text("Scene Objects:");
        for (int i = 0; i < static_cast<int>(scene.objects.size()); i++) {
            bool selected = (scene.selectionType == SelectionType::Object && scene.selectedIndex == i);
            if (ImGui::Selectable(scene.objects[i].name.c_str(), selected)) {
                scene.selectionType = SelectionType::Object;
                scene.selectedIndex = i;
            }
        }

        ImGui::End();
    }

    // Window 4: per-selection properties
    void DrawPenumbraRampEditor(SceneObject& obj)
    {
        // Live preview strip of the evaluated ramp.
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float  width = ImGui::GetContentRegionAvail().x;
        constexpr float kHeight = 22.0f;
        constexpr int   kSwatches = 48;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int s = 0; s < kSwatches; s++) {
            glm::vec3 c = EvaluateRamp(obj.penumbraStops, float(s) / float(kSwatches - 1));
            dl->AddRectFilled(
                ImVec2(origin.x + width * s / kSwatches, origin.y),
                ImVec2(origin.x + width * (s + 1) / kSwatches, origin.y + kHeight),
                IM_COL32(int(c.r * 255), int(c.g * 255), int(c.b * 255), 255));
        }
        ImGui::Dummy(ImVec2(width, kHeight + 4));

        for (int s = 0; s < static_cast<int>(obj.penumbraStops.size()); s++) {
            ImGui::PushID(s);
            ImGui::ColorEdit3("##col", &obj.penumbraStops[s].color.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("##pos", &obj.penumbraStops[s].position, 0.0f, 1.0f, "%.2f");

            if (obj.penumbraStops.size() > 2) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    obj.penumbraStops.erase(obj.penumbraStops.begin() + s);
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }

        if (static_cast<int>(obj.penumbraStops.size()) < MAX_RAMP_STOPS && ImGui::Button("Add stop")) {
            obj.penumbraStops.push_back({ glm::vec3(1.0f), 0.5f });
        }

        ImGui::SliderFloat("Layer Weight", &obj.penumbraWeight, 0.0f, 1.0f);
        ImGui::SliderFloat("Ramp Bands", &obj.penumbraBands, 0.0f, 12.0f, "%.0f");
        ImGui::Combo("Pattern", &obj.penumbraPattern, "None\0Noise\0Hatch\0Halftone\0");
        ImGui::SliderFloat("Pattern Scale", &obj.penumbraPatternScale, 1.0f, 200.0f);
        ImGui::SliderFloat("Pattern Strength", &obj.penumbraPatternStrength, 0.0f, 0.5f);
    }

    void DrawObjectProperties(SceneObject& obj, RenderSettings& settings)
    {
        ImGui::Text("Object: %s", obj.name.c_str());

        SectionHeader("Turntable");
        ImGui::SliderFloat("Rotation", &obj.rotationY, 0.0f, 360.0f, "%.1f deg");
        ImGui::Checkbox("Auto-rotate", &settings.turntable.autoRotate);
        if (settings.turntable.autoRotate) {
            ImGui::SliderFloat("Speed", &settings.turntable.speedDegPerSec, 5.0f, 120.0f, "%.0f deg/s");
        }

        ImGui::Separator();
        ImGui::ColorEdit3("Color", &obj.colorTint.x);
        ImGui::SliderFloat("Roughness", &obj.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Metallic", &obj.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat Factor", &obj.clearcoatFactor, 0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat Roughness", &obj.clearcoatRoughness, 0.01f, 0.5f);
        ImGui::SliderFloat("Flake Strength", &obj.flakeStrength, 0.0f, 0.3f);
        ImGui::SliderFloat("Flake Scale", &obj.flakeScale, 50.0f, 1000.0f);

        SectionHeader("Shadow Penumbra");
        ImGui::Checkbox("Custom penumbra ramp", &settings.customShadowPenumbra);
        if (settings.customShadowPenumbra) {
            DrawPenumbraRampEditor(obj);
        }
    }

    void DrawLightProperties(SceneLight& light)
    {
        ImGui::Text("Light: %s", light.name.c_str());
        ImGui::Separator();
        ImGui::SliderFloat3("Position", &light.position.x, -10.0f, 10.0f);
        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 20.0f);
        ImGui::SliderFloat("Radius", &light.radius, 0.5f, 20.0f);
    }

    void DrawPropertyPanel(Scene& scene, RenderSettings& settings)
    {
        BeginPanel("Property Window", ImVec2(1580, 500));

        if (scene.HasSelectedObject()) {
            DrawObjectProperties(scene.objects[scene.selectedIndex], settings);
        }
        else if (scene.HasSelectedLight()) {
            DrawLightProperties(scene.lights[scene.selectedIndex]);
        }
        else {
            ImGui::TextDisabled("Nothing selected.");
        }

        ImGui::End();
    }

}

void DrawEditorUI(Scene& scene, RenderSettings& settings,
    const Camera& camera, const EditorFrameInfo& info)
{
    DrawGizmo(scene, camera, info);
    DrawDebugPanel(settings, info);
    DrawScenePropertyPanel(scene, settings, info);
    DrawOutliner(scene);
    DrawPropertyPanel(scene, settings);
}