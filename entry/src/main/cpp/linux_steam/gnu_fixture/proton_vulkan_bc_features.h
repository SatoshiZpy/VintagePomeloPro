#pragma once
#include <vulkan/vulkan.h>

// textureCompressionBC promises the whole BC family. Some host stacks report
// it even when BC4-7 have no sampled-image support. Report only capabilities
// backed by format queries, so DXVK can select its existing decode path.
static inline VkFormat proton_x64_validate_bc_features(
    VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceFormatProperties query,
    VkPhysicalDeviceFeatures* features)
{
    if (!features->textureCompressionBC) return VK_FORMAT_UNDEFINED;
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    for (int value = VK_FORMAT_BC1_RGB_UNORM_BLOCK; value <= VK_FORMAT_BC7_SRGB_BLOCK; ++value) {
        VkFormatProperties properties{};
        if (query) query(physical, static_cast<VkFormat>(value), &properties);
        if ((properties.optimalTilingFeatures & required) != required) {
            features->textureCompressionBC = VK_FALSE;
            return static_cast<VkFormat>(value);
        }
    }
    return VK_FORMAT_UNDEFINED;
}
