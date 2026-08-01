#pragma once

// Radical inverse in the given base. Halton(2,3) is the standard TAA choice —
// any subsequence of it is reasonably well distributed, which matters because
// a pixel can start accumulating at any index after disocclusion.
inline float Halton(int index, int base)
{
    float result = 0.0f;
    float f = 1.0f;
    int i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(i % base);
        i /= base;
    }
    return result;
}