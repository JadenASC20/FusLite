#include "ScenePresets.h"
#include "Model.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

    // The studio stage the hero cars sit on. Set to false to load the car alone --
    // useful when the stage GLB is the thing that won't parse.
    constexpr bool kIncludeStage = true;
    constexpr const char* kStagePath = "assets/FusLiteStageCylinder.glb";

    // Set to false once the material names are settled. The per-material dump is
    // noisy, but it's the only way to see why a role didn't match.
    constexpr bool kLogMaterialRoles = true;

    SceneObject MakeStageObject()
    {
        SceneObject stage;
        stage.name = "Turntable Stage";
        stage.baseTransform = glm::mat4(1.0f);
        stage.transform = glm::mat4(1.0f);
        stage.colorTint = glm::vec3(0.1f);
        stage.roughness = 0.1f;
        stage.metallic = 0.0f;
        return stage;
    }

    SceneObject MakeCarObject(const char* name)
    {
        SceneObject car;
        car.name = name;
        car.baseTransform = glm::mat4(1.0f);
        car.transform = glm::mat4(1.0f);
        car.colorTint = glm::vec3(0.8f, 0.05f, 0.05f);
        car.roughness = 1.0f;
        car.metallic = 0.0f;
        car.clearcoatFactor = 0.0f;
        car.clearcoatRoughness = 0.0f;
        car.flakeStrength = 0.0f;
        car.flakeScale = 0.0f;
        return car;
    }

    SceneDescription BuildHeroCarScene(const char* presetName,
        const char* modelPath,
        const char* objectName)
    {
        SceneDescription desc;
        desc.name = presetName;
        desc.skyboxHdri = "assets/McLarenShowcaseDemo/McLarenAutoshop.hdr";   // CHANGE THIS TO CHANGE SKYBOX

        desc.modelPaths.push_back(modelPath);
        desc.objects.push_back(MakeCarObject(objectName));

        if (kIncludeStage) {
            desc.modelPaths.push_back(kStagePath);
            desc.objects.push_back(MakeStageObject());
        }
        return desc;
    }

    SceneDescription BuildShaderBallScene()
    {
        constexpr float kSpacing = 2.5f;
        const std::string ballPath = "assets/ShaderBallShowcase/ShaderBall.obj";
        const std::string floorPath = "assets/ShaderBallShowcase/floor.obj";

        SceneDescription desc;
        desc.name = "Shader Balls";
        desc.skyboxHdri = "assets/ShaderBallShowcase/Skybox.hdr";

        for (int i = 0; i < 5; i++) {
            desc.modelPaths.push_back(ballPath);

            SceneObject ball;
            ball.name = "pSphere" + std::string(1, static_cast<char>('A' + i));
            ball.baseTransform = glm::translate(glm::mat4(1.0f),
                glm::vec3((i - 2) * kSpacing, 0.0f, 0.0f));
            ball.transform = ball.baseTransform;
            ball.colorTint = glm::vec3(0.7f, 0.1f, 0.1f);
            ball.clearcoatFactor = 0.2f + i * 0.15f;
            ball.flakeStrength = 0.0f;
            desc.objects.push_back(ball);
        }

        desc.modelPaths.push_back(floorPath);

        SceneObject ground;
        ground.name = "pGroundPlane";
        ground.baseTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f))
            * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f));
        ground.transform = ground.baseTransform;
        ground.colorTint = glm::vec3(0.55f);
        ground.clearcoatFactor = 0.0f;
        ground.flakeStrength = 0.0f;
        desc.objects.push_back(ground);

        return desc;
    }

    // ---------------------------------------------------------------------------
    // Name matching.
    //
    // Case-insensitive, because nothing guarantees an exporter preserves the
    // artist's capitalisation. Substring, not exact, because names arrive looking
    // like "Chevy_Windshield_Glass.001".
    // ---------------------------------------------------------------------------
    bool NameContains(const std::string& haystack, const char* needle)
    {
        const size_t hlen = haystack.size();
        const size_t nlen = strlen(needle);
        if (nlen == 0 || nlen > hlen) return false;

        for (size_t i = 0; i + nlen <= hlen; i++) {
            size_t j = 0;
            while (j < nlen &&
                std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                j++;
            }
            if (j == nlen) return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------
    // Transparency tagging.
    //
    // The old predicate checked only "Glass" and "Window", which misses
    // "Windshield" and "Windscreen" -- neither string contains "Window". A missed
    // tag sends the material down the OPAQUE path with blending disabled, which
    // renders as flat unblended white over the cabin.
    // ---------------------------------------------------------------------------
    bool IsGlassMaterial(const std::string& n)
    {
        // Explicit opt-out wins: assets often ship an "Opaque_Glass" trim piece.
        if (NameContains(n, "Opaque")) return false;
        // The structure around the glass, not the glass itself. The Colorado
        // ships "Glass_WindowSurroundFront" for the black frame.
        if (NameContains(n, "Surround") || NameContains(n, "Frame")) return false;

        return NameContains(n, "Glass")
            || NameContains(n, "Window")
            || NameContains(n, "Windshield")
            || NameContains(n, "Windscreen")
            || NameContains(n, "Screen")
            || NameContains(n, "Transparent")
            || NameContains(n, "Translucent");
    }

} // namespace

SceneDescription BuildScene(ScenePreset preset)
{
    SceneDescription desc;

    switch (preset) {
    case ScenePreset::McLaren:
        desc = BuildHeroCarScene("McLaren 600LT",
            "assets/McLarenShowcaseDemo/McLaren.glb",
            "McLaren 600LT");
        break;
    case ScenePreset::Colorado:
        desc = BuildHeroCarScene("Chevrolet Colorado",
            "assets/ChevColoradoShowcaseDemo/ChevColorado.glb",
            "Chevrolet Colorado");
        break;
    case ScenePreset::ShaderBalls:
        desc = BuildShaderBallScene();
        break;
    }

    if (desc.modelPaths.size() != desc.objects.size()) {
        throw std::runtime_error("SceneDescription: modelPaths and objects must be 1:1");
    }
    return desc;
}

void ApplyMaterialRoles(std::vector<MaterialParams>& materials)
{
    for (MaterialParams& mp : materials) {
        const std::string& n = mp.name;

        // Transparency. This is now the single source of truth -- the glTF
        // loader no longer sets isGlass.
        mp.isGlass = IsGlassMaterial(n);

        // Defaults: no clearcoat, no flake. metallic / roughness / colorTint
        // already came from the glTF material factors.
        mp.clearcoatFactor = 0.0f;
        mp.clearcoatRoughness = 1.0f;
        mp.flakeStrength = 0.0f;
        mp.flakeScale = 0.0f;

        // --- Clearcoat roles (no glTF-core source) ---
        if (NameContains(n, "CarPaint") && !NameContains(n, "Trim")) {
            mp.clearcoatFactor = 0.0f; mp.clearcoatRoughness = 0.00f;
            mp.flakeStrength = 0.0f; mp.flakeScale = 0.0f;
        }
        else if (NameContains(n, "Carbon")) {
            mp.clearcoatFactor = 0.8f; mp.clearcoatRoughness = 0.08f;
        }
        else if (NameContains(n, "PianoBlack")) {
            mp.clearcoatFactor = 1.0f; mp.clearcoatRoughness = 0.04f;
        }
        else if (NameContains(n, "Calliper") || NameContains(n, "Caliper")) {
            mp.clearcoatFactor = 0.6f; mp.clearcoatRoughness = 0.10f;
        }
        // Headlight / taillight lenses. Guarded so it can't swallow a glass
        // material that also happens to have "light" in its name.
        else if (NameContains(n, "Light") && !mp.isGlass) {
            mp.clearcoatFactor = 0.5f;
        }
        // --- Surface corrections where the asset's factors are wrong ---
        else if (NameContains(n, "Tire") || NameContains(n, "Tyre") ||
            NameContains(n, "Rubber")) {
            mp.roughness = 0.9f;  mp.metallic = 0.0f;   // matte, non-metal
            mp.clearcoatFactor = 0.0f;
        }
        else if (NameContains(n, "Rim") || NameContains(n, "Wheel")) {
            mp.roughness = 0.35f; mp.metallic = 1.0f;   // metal, but not a mirror
        }
        else if (NameContains(n, "Chrome")) {
            mp.roughness = 0.1f;  mp.metallic = 1.0f;   // mirror is correct here
        }
        else if (NameContains(n, "PlasticRough")) {
            mp.roughness = 0.7f;  mp.metallic = 0.0f;   // rough plastic trim
        }
        else if (NameContains(n, "Seat") || NameContains(n, "Leather") ||
            NameContains(n, "Fabric") || NameContains(n, "Cloth") ||
            NameContains(n, "Interior") || NameContains(n, "Dash") ||
            NameContains(n, "Carpet")) {
            mp.roughness = 1.0f;  mp.metallic = 0.0f;   // matte, non-metal
            mp.clearcoatFactor = 0.0f;
        }

        // --- Glass override -------------------------------------------------
        // Runs AFTER the chain so it wins regardless of which branch matched.
        // The asset gives glass metallic 0.5-0.7 and no diffuse texture, so it
        // falls back to flat white: a white Lambertian surface at 25% opacity
        // over the cabin, which saturates. Real glass transmits and has
        // essentially no diffuse response.
        if (mp.isGlass) {
            mp.metallic = 0.0f;                            // dielectric, always
            mp.roughness = std::max(mp.roughness, 0.06f);   // 0.0 is a delta lobe
            mp.colorTint = glm::vec3(0.02f);                // near-zero albedo, not black
            mp.clearcoatFactor = 0.0f;
        }


        if (kLogMaterialRoles) {
            printf("  role: %-38s -> %-6s  rough %.2f  metal %.2f  cc %.2f\n",
                n.c_str(), mp.isGlass ? "GLASS" : "opaque",
                mp.roughness, mp.metallic, mp.clearcoatFactor);
        }
    }
}

void BindModelMaterialsToScene(Scene& scene, const std::vector<Model>& models)
{
    const size_t count = std::min(scene.objects.size(), models.size());

    for (size_t i = 0; i < count; i++) {
        SceneObject& obj = scene.objects[i];
        obj.materials = models[i].GetDefaultMaterials();

        if (obj.materials.empty()) {
            // Fallback so DrawFilteredImpl's mats.front() is never UB.
            MaterialParams mp;
            mp.name = "default";
            mp.colorTint = obj.colorTint;
            mp.roughness = obj.roughness;
            mp.metallic = obj.metallic;
            mp.clearcoatFactor = obj.clearcoatFactor;
            mp.clearcoatRoughness = obj.clearcoatRoughness;
            mp.flakeStrength = obj.flakeStrength;
            mp.flakeScale = obj.flakeScale;
            obj.materials.push_back(mp);
        }

        if (kLogMaterialRoles) {
            printf("Material roles for '%s' (%zu materials):\n",
                obj.name.c_str(), obj.materials.size());
        }
        ApplyMaterialRoles(obj.materials);
    }
}

std::vector<SceneLight> DefaultSceneLights()
{
    // Studio three-point, positioned outside a ~5m vehicle bounding box.
    return {
        { "KeyLight",  {  4.0f, 3.0f,  4.0f }, { 1.0f, 0.95f, 0.9f }, 6.0f, 15.0f },
        { "FillLight", { -5.0f, 2.5f,  2.0f }, { 0.9f, 0.95f, 1.0f }, 3.0f, 15.0f },
        { "RimLight",  {  0.0f, 3.5f, -5.0f }, { 1.0f, 1.0f,  1.0f }, 4.0f, 15.0f },
    };
}