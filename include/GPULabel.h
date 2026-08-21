#pragma once
#include <volk.h>

// RAII scoped debug label for RenderDoc / validation-layer pass naming.
struct GpuLabel {
    VkCommandBuffer cmd;
    GpuLabel(VkCommandBuffer c, const char* name, float r, float g, float b) : cmd(c) {
        if (vkCmdBeginDebugUtilsLabelEXT) {
            VkDebugUtilsLabelEXT label{};
            label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label.pLabelName = name;
            label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = 1.0f;
            vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
        }
    }
    ~GpuLabel() { 
        if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(cmd);
    }

    // a label must begin and end on the same cmd buffer, once.
    GpuLabel(const GpuLabel&) = delete;
    GpuLabel& operator=(const GpuLabel&) = delete;
};