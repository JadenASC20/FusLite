#pragma once
#include <volk.h>

VkShaderModule CreateShaderModuleFromBinary(VkDevice device, const char* filename);