#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// Tanner Helland's blackbody approximation — standard, widely-used technique
// for converting a Kelvin temperature into an RGB color for lighting purposes.
inline glm::vec3 KelvinToRGB(float kelvin)
{
    float temp = kelvin / 100.0f;
    float r, g, b;

    if (temp <= 66.0f) {
        r = 255.0f;
    }
    else {
        r = temp - 60.0f;
        r = 329.698727446f * powf(r, -0.1332047592f);
    }

    if (temp <= 66.0f) {
        g = temp;
        g = 99.4708025861f * logf(g) - 161.1195681661f;
    }
    else {
        g = temp - 60.0f;
        g = 288.1221695283f * powf(g, -0.0755148492f);
    }

    if (temp >= 66.0f) {
        b = 255.0f;
    }
    else if (temp <= 19.0f) {
        b = 0.0f;
    }
    else {
        b = temp - 10.0f;
        b = 138.5177312231f * logf(b) - 305.0447927307f;
    }

    return glm::vec3(
        std::clamp(r, 0.0f, 255.0f) / 255.0f,
        std::clamp(g, 0.0f, 255.0f) / 255.0f,
        std::clamp(b, 0.0f, 255.0f) / 255.0f
    );
}