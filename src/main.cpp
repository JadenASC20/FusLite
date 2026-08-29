// FusLite -- Vulkan 1.3 clustered Forward+ PBR renderer
//
// main.cpp owns the window, the camera, the scene, and the frame loop.
// Everything with a GPU handle in it lives in Renderer.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <Camera.h>
#include <ShadowMap.h>
#include <ColorTemperature.h>
#include <ClusterConfig.h>
#include <Halton.h>
#include <ImGuiManager.h>
#include <RenderParams.h>

#include <AppState.h>
#include <EditorUI.h>
#include <FrameRecorder.h>
#include <Renderer.h>
#include <ScenePresets.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

// 
// >>> CHANGE THIS TO SWITCH SCENES <<<
// ScenePreset::McLaren | ScenePreset::Colorado | ScenePreset::ShaderBalls
constexpr ScenePreset kActiveScene = ScenePreset::McLaren;

constexpr uint32_t kWindowWidth = 1920;
constexpr uint32_t kWindowHeight = 1080;
constexpr int      kTaaJitterPhases = 8;
constexpr float    kNearZ = 0.1f;
constexpr float    kFarZ = 1000.0f;

// GLFW callbacks are C function pointers with nowhere to hang state, so these
// two are the only globals in the program.
static Camera* g_camera = nullptr;
static RenderSettings* g_settings = nullptr;

static void KeyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && g_settings) {
        g_settings->showGui = !g_settings->showGui;
    }
    if (g_camera) g_camera->OnKey(key, action);
}

static void MouseMoveCallback(GLFWwindow*, double x, double y)
{
    if (g_camera) g_camera->OnMouseMove(x, y);
}

static void MouseButtonCallback(GLFWwindow*, int button, int action, int)
{
    if (!ImGuiManager::IsMouseControlledByImGui() && g_camera) {
        g_camera->OnMouseButton(button, action);
    }
}

static GLFWwindow* CreateWindow()
{
    if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "FusLite", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseMoveCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    return window;
}

// Applies this frame's Halton sample as a sub-pixel projection offset.
static void ApplyTaaJitter(Camera& camera, const TemporalState& temporal, bool enabled)
{
    const int phase = temporal.JitterPhase(kTaaJitterPhases);

    // Halton is [0,1); recentre to [-0.5,0.5), then convert pixels to NDC.
    const float hx = Halton(phase, 2) - 0.5f;
    const float hy = Halton(phase, 3) - 0.5f;

    camera.SetJitter(
        enabled ? hx * 2.0f / float(kWindowWidth) : 0.0f,
        enabled ? hy * 2.0f / float(kWindowHeight) : 0.0f);
}

static void DrawFrameUI(Scene& scene, RenderSettings& settings, const Camera& camera,
    ImGuiManager& imgui, float deltaTime, int jitterPhase)
{
    imgui.BeginFrame();

    EditorFrameInfo info{};
    info.deltaTime = deltaTime;
    info.haltonIndex = jitterPhase;
    info.jitterPhases = kTaaJitterPhases;
    info.viewportWidth = kWindowWidth;
    info.viewportHeight = kWindowHeight;

    DrawEditorUI(scene, settings, camera, info);
    imgui.EndFrame();
}

int main()
{
    GLFWwindow* window = nullptr;

    try {
        window = CreateWindow();

        Renderer renderer;
        renderer.Init(window, kActiveScene, kWindowWidth, kWindowHeight);

        Camera camera(
            glm::vec3(0.0f, 1.0f, 5.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            45.0f, renderer.AspectRatio(), kNearZ, kFarZ);
        g_camera = &camera;

        RenderSettings settings;
        g_settings = &settings;

        Scene scene;
        scene.objects = renderer.Description().objects;
        scene.lights = DefaultSceneLights();
        BindModelMaterialsToScene(scene, renderer.Models());

        RenderParams renderParams{};
        renderParams.clusterGridAndScreen =
            glm::vec4(CLUSTER_GRID_X, CLUSTER_GRID_Y, CLUSTER_GRID_Z, 0.0f);
        renderParams.screenSize = glm::vec2(kWindowWidth, kWindowHeight);
        renderParams.nearZ = kNearZ;
        renderParams.farZ = kFarZ;

        TemporalState temporal;
        temporal.Sync(scene.objects.size());

        auto  startTime = std::chrono::high_resolution_clock::now();
        float lastTime = 0.0f;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            const float now = std::chrono::duration<float>(
                std::chrono::high_resolution_clock::now() - startTime).count();
            const float deltaTime = now - lastTime;
            lastTime = now;

            camera.Update(deltaTime);
            ApplyTaaJitter(camera, temporal, settings.taa.jitterEnabled);

            // Snapshot temporal indices before advancing the counter.
            FrameParams frame{};
            frame.historyRead = temporal.HistoryRead();
            frame.historyWrite = temporal.HistoryWrite();
            frame.firstFrame = temporal.IsFirstFrame();
            const int jitterPhase = temporal.JitterPhase(kTaaJitterPhases);
            temporal.Advance();

            renderer.CullAndUploadLights(scene, camera.GetViewMatrix());
            renderer.UploadPenumbraRamps(scene);

            frame.lightViewProj =
                ShadowMap::ComputeLightViewProj(scene.sun.direction, 15.0f, kNearZ, 100.0f);
            frame.imageIndex = renderer.AcquireNextImage();
            frame.frameInFlight = renderer.CurrentFrameInFlight();

            settings.exposure.measuredLuminance = renderer.MeasureLuminance(frame.frameInFlight);
            settings.exposure.Update(deltaTime);

            // Transforms first, then UBOs: model and prevModel must describe the
            // same frame's delta or motion vectors go wrong.
            scene.UpdateTransforms(settings.turntable, deltaTime);
            temporal.Sync(scene.objects.size());
            renderer.UpdatePerObjectUniforms(scene, camera, frame.imageIndex,
                frame.lightViewProj, temporal);
            renderer.UpdateSkybox(frame.imageIndex, camera);

            renderParams.lightDirAndIntensity = glm::vec4(scene.sun.direction, scene.sun.intensity);
            renderParams.sunColor = glm::vec4(KelvinToRGB(scene.sun.kelvin), 0.0f);
            renderParams.lightSize = scene.sun.size;
            frame.renderParams = renderParams;

            if (settings.showGui) {
                DrawFrameUI(scene, settings, camera, renderer.ImGuiMgr(), deltaTime, jitterPhase);
            }

            RecordFrame(renderer.CommandBuffer(frame.imageIndex), renderer.Passes(), frame,
                renderer.Models(), scene, camera, settings);

            temporal.Capture(scene,
                camera.GetProjectionMatrixNoJitter() * camera.GetViewMatrix());

            renderer.SubmitAndPresent(frame.imageIndex);
        }

        g_camera = nullptr;
        g_settings = nullptr;
        renderer.Shutdown();
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Fatal error: %s\n", e.what());
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}