#pragma once

#include "core/check.hpp"

#include <vulkan/vulkan.h>

#define NITID_VK(x)                                                                         \
    do {                                                                                    \
        VkResult r_ = (x);                                                                  \
        if (r_ != VK_SUCCESS) ::nitid::failf("Vulkan: {} failed ({})", #x, int(r_));        \
    } while (0)
