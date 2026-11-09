#pragma once
// Legacy DXVK 5dff1d7 includes R32 UAV views for typeless BGRA images, but
// only sets EXTENDED_USAGE for sRGB. Preserve the real driver's capabilities
// and errors; permit storage usage through an actual compatible R32 view.
namespace x64bgra {
static bool eligible(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                     VkImageCreateFlags flags) {
    return format == VK_FORMAT_B8G8R8A8_UNORM && tiling == VK_IMAGE_TILING_OPTIMAL
        && (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        && (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
        && !(flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT);
}
static bool extend(VkPhysicalDevice physical, VkFormat format, VkImageTiling tiling,
                   VkImageUsageFlags usage, VkImageCreateFlags flags, const void* next) {
    if (!physical || !eligible(format, tiling, usage, flags)) return false;
    const VkImageFormatListCreateInfo* list = nullptr;
    for (auto p = static_cast<const VkBaseInStructure*>(next); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            list = reinterpret_cast<const VkImageFormatListCreateInfo*>(p);
    auto query = instanceProc<PFN_vkGetPhysicalDeviceFormatProperties>(
        physicalInstance(physical), "vkGetPhysicalDeviceFormatProperties");
    if (!query) return false;
    VkFormatProperties properties{};
    query(physical, format, &properties);
    if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) return false;
    for (auto view : {VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_SFLOAT}) {
        // A non-empty list constrains the legal views; an empty/absent list
        // allows all compatible formats, per Vulkan's format-list rules.
        if (list && list->viewFormatCount) {
            bool allowed = false;
            for (uint32_t i = 0; i < list->viewFormatCount; ++i)
                allowed |= list->pViewFormats[i] == view;
            if (!allowed) continue;
        }
        query(physical, view, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) return true;
    }
    return false;
}
static void report(const char* stage, VkImageUsageFlags usage, VkImageCreateFlags flags,
                   VkResult before, VkResult after) {
    static std::atomic<unsigned> count{0};
    if (count.fetch_add(1) < 24)
        fprintf(stderr, "GNU_X64_BGRA stage=%s usage=0x%x flags=0x%x extended_usage=1 before=%d result=%d\n",
                stage, unsigned(usage), unsigned(flags), int(before), int(after));
}
static VkResult VKAPI_CALL query(VkPhysicalDevice physical, VkFormat format, VkImageType type,
        VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
        VkImageFormatProperties* properties) {
    auto real = instanceProc<PFN_vkGetPhysicalDeviceImageFormatProperties>(
        physicalInstance(physical), "vkGetPhysicalDeviceImageFormatProperties");
    VkResult result = real(physical, format, type, tiling, usage, flags, properties);
    if (result != VK_ERROR_FORMAT_NOT_SUPPORTED || !extend(physical, format, tiling, usage, flags, nullptr))
        return result;
    VkResult retry = real(physical, format, type, tiling, usage,
        flags | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT, properties);
    report("query", usage, flags, result, retry);
    return retry;
}
static VkResult VKAPI_CALL query2(VkPhysicalDevice physical,
        const VkPhysicalDeviceImageFormatInfo2* info, VkImageFormatProperties2* properties) {
    auto real = instanceProc<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
        physicalInstance(physical), "vkGetPhysicalDeviceImageFormatProperties2");
    VkResult result = real(physical, info, properties);
    if (result != VK_ERROR_FORMAT_NOT_SUPPORTED || !extend(physical, info->format,
            info->tiling, info->usage, info->flags, info->pNext)) return result;
    auto retryInfo = *info;
    retryInfo.flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    VkResult retry = real(physical, &retryInfo, properties);
    report("query2", info->usage, info->flags, result, retry);
    return retry;
}
static VkResult VKAPI_CALL create(VkDevice device, const VkImageCreateInfo* info,
        const VkAllocationCallbacks* allocator, VkImage* image) {
    auto real = deviceProc<PFN_vkCreateImage>(device, "vkCreateImage");
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    if (eligible(info->format, info->tiling, info->usage, info->flags)) {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        auto it = devices.find(device);
        if (it != devices.end()) physical = it->second.physical;
    }
    if (!extend(physical, info->format, info->tiling, info->usage, info->flags, info->pNext))
        return real(device, info, allocator, image);
    auto fixed = *info;
    fixed.flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    VkResult result = real(device, &fixed, allocator, image);
    // Creation is called once; "before" is not a failed creation attempt.
    report("create", info->usage, info->flags, VK_SUCCESS, result);
    return result;
}
static PFN_vkVoidFunction proc(const char* name) {
    if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties")) return reinterpret_cast<PFN_vkVoidFunction>(query);
    if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2") || !strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2KHR"))
        return reinterpret_cast<PFN_vkVoidFunction>(query2);
    if (!strcmp(name, "vkCreateImage")) return reinterpret_cast<PFN_vkVoidFunction>(create);
    return nullptr;
}
} // namespace x64bgra
