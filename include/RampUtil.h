#pragma once
#include "SceneTypes.h"
#include <algorithm>

inline glm::vec3 EvaluateRamp(const std::vector<ColorStop>& stops, float t)
{
    if (stops.empty()) return glm::vec3(1.0f);
    if (stops.size() == 1) return stops[0].color;

    std::vector<ColorStop> sorted = stops;
    std::sort(sorted.begin(), sorted.end(),
        [](const ColorStop& a, const ColorStop& b) { return a.position < b.position; });

    if (t <= sorted.front().position) return sorted.front().color;
    if (t >= sorted.back().position)  return sorted.back().color;

    for (size_t i = 0; i + 1 < sorted.size(); i++) {
        if (t >= sorted[i].position && t <= sorted[i + 1].position) {
            float span = sorted[i + 1].position - sorted[i].position;
            float f = (span > 1e-5f) ? (t - sorted[i].position) / span : 0.0f;
            return glm::mix(sorted[i].color, sorted[i + 1].color, f);
        }
    }
    return sorted.back().color;
}