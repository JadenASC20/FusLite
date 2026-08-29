#pragma once
#include <string>
#include <vector>

#include "AppState.h"
#include "SceneTypes.h"

// Which scene the build loads. Changes kActiveScene in main.cpp.

enum class ScenePreset
{
    McLaren,     // hero car in the studio stage
    Colorado,    // Chevy Colorado in the studio stage
    ShaderBalls, // original FusLite lookdev scene: five balls + ground plane
};

struct SceneDescription
{
    std::string              skyboxHdri;
    std::vector<std::string> modelPaths;
    std::vector<SceneObject> objects;   // INVARIANT: same size as modelPaths

    const char* name = "";
};

// Builds the model list and matching scene objects for a preset.
SceneDescription BuildScene(ScenePreset preset);

// Assigns clearcoat / flake / surface-role overrides by material name.
// glTF supplies metallic, roughness, and base colour; this table only sets what
// glTF core has no channel for, plus a few asset-specific corrections.
void ApplyMaterialRoles(std::vector<MaterialParams>& materials);

// Copies each model's default materials onto its scene object, substituting a
// single fallback material when a model reports none (keeps Draw's mats.front()
// from being UB), then applies the role table.
class Model;
void BindModelMaterialsToScene(Scene& scene, const std::vector<Model>& models);

// Default point lights. Same three the old main.cpp pushed unconditionally.
std::vector<SceneLight> DefaultSceneLights();