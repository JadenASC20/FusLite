#include "AppState.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void ExposureSettings::Update(float deltaTime)
{
    if (!autoExposure) {
        // Keep the smoothed state in sync so toggling back doesn't jump.
        exposure = exp2f(manualEV);
        smoothedExposure = exposure;
        return;
    }

    const float avgLum = std::max(measuredLuminance, 1e-4f);
    float target = std::clamp(keyValue / avgLum, minExposure, maxExposure);

    // Asymmetric adaptation: fast when the scene brightens (target drops below
    // current), slow when it darkens. Matches how eyes actually behave.
    const float rate = (target < smoothedExposure) ? adaptSpeedBright : adaptSpeedDark;
    const float t = 1.0f - expf(-rate * deltaTime);

    smoothedExposure += (target - smoothedExposure) * t;
    exposure = smoothedExposure;
}

void Scene::UpdateTransforms(const TurntableSettings& turntable, float deltaTime)
{
    // Auto-rotate advances only the selected object.
    if (turntable.autoRotate && HasSelectedObject()) {
        SceneObject& sel = objects[selectedIndex];
        sel.rotationY = fmodf(sel.rotationY + turntable.speedDegPerSec * deltaTime, 360.0f);
    }

    for (SceneObject& obj : objects) {
        obj.transform = obj.baseTransform *
            glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationY), glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

void TemporalState::Sync(size_t objectCount)
{
    if (prevModel.size() != objectCount) {
        prevModel.assign(objectCount, glm::mat4(1.0f));
    }
}

void TemporalState::Capture(const Scene& scene, const glm::mat4& viewProj)
{
    Sync(scene.objects.size());
    for (size_t i = 0; i < scene.objects.size(); i++) {
        prevModel[i] = scene.objects[i].transform;
    }
    prevViewProj = viewProj;
}