// Compile the Wine WSI with an explicitly scoped, paired x64 ICD.
// The Native FEX WSI and the legacy i386 Wine ICD keep their current paths.
#include <dlfcn.h>
#include <cstring>
#include "proton_transport_profile.h"
static void* x64GraphicsOpen(const char* path,int flags) {
    if(path&&!strcmp(path,"/data/storage/el2/base/files/steamohos-native/proton-probe/graphics/libvulkan_virtio.so")) {
        // Configure before dlopen: Venus caches these options at initialization.
        static const bool configured=[] {
            protontransport::configure(protontransport::readMode("/data/storage/el2/base/files/steamohos-native/proton-probe/vulkan-transport-profile"));
            return true;
        }();
        (void)configured;
        return dlopen("/data/storage/el2/base/files/steamohos-native/proton-probe/graphics-x64/libvulkan_virtio.so",flags);
    }
    return dlopen(path,flags);
}
#define dlopen x64GraphicsOpen
#define vkGetInstanceProcAddr x64BaseGetInstanceProcAddr
#define vkGetDeviceProcAddr x64BaseGetDeviceProcAddr
#include "proton_vulkan_wsi.cpp"
#undef vkGetInstanceProcAddr
#undef vkGetDeviceProcAddr
#include "proton_vulkan_bc_features.h"
#include "proton_vulkan_profile.h"
#include "proton_vulkan_bgra_usage.h"

static void x64ValidateBc(VkPhysicalDevice physical, VkPhysicalDeviceFeatures* features) {
    auto query=instanceProc<PFN_vkGetPhysicalDeviceFormatProperties>(physicalInstance(physical),"vkGetPhysicalDeviceFormatProperties");
    const auto missing=proton_x64_validate_bc_features(physical,query,features);
    static std::atomic<unsigned> reports{0};
    if(missing!=VK_FORMAT_UNDEFINED&&reports.fetch_add(1)<8)
        fprintf(stderr,"GNU_X64_BC incomplete_family=1 missing_format=%u textureCompressionBC=0 fallback=dxvk_decode\n",unsigned(missing));
}
static void VKAPI_CALL x64GetFeatures(VkPhysicalDevice physical,VkPhysicalDeviceFeatures* features) {
    instanceProc<PFN_vkGetPhysicalDeviceFeatures>(physicalInstance(physical),"vkGetPhysicalDeviceFeatures")(physical,features);
    x64ValidateBc(physical,features);
}
static void VKAPI_CALL x64GetFeatures2(VkPhysicalDevice physical,VkPhysicalDeviceFeatures2* features) {
    instanceProc<PFN_vkGetPhysicalDeviceFeatures2>(physicalInstance(physical),"vkGetPhysicalDeviceFeatures2")(physical,features);
    // Read-only capability diagnostics, enabled with the existing private profiler.
    // Query the paired ICD directly to distinguish a Wine pNext omission from
    // an actual Venus capability gap; never advertise unsupported features.
    static std::atomic<unsigned> capabilityReports{0};
    if (x64profile::enabled() && capabilityReports.fetch_add(1) < 2) {
        VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT divisor{};
        divisor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 direct{};
        direct.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        direct.pNext = &divisor;
        instanceProc<PFN_vkGetPhysicalDeviceFeatures2>(physicalInstance(physical), "vkGetPhysicalDeviceFeatures2")(physical, &direct);
        auto enumerate = instanceProc<PFN_vkEnumerateDeviceExtensionProperties>(physicalInstance(physical), "vkEnumerateDeviceExtensionProperties");
        uint32_t count = 0, version = 0;
        VkResult result = enumerate(physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> extensions;
        if (result == VK_SUCCESS && count <= 4096) {
            extensions.resize(count);
            result = enumerate(physical, nullptr, &count, extensions.data());
            if (result == VK_SUCCESS) for (uint32_t i = 0; i < count; ++i)
                if (!strcmp(extensions[i].extensionName, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME))
                    version = extensions[i].specVersion;
        }
        bool requested = false;
        for (auto* next = static_cast<VkBaseOutStructure*>(features->pNext); next; next = next->pNext)
            if (next->sType == divisor.sType) requested = true;
        fprintf(stderr, "GNU_X64_CAPS source=paired_icd divisor_extension_version=%u divisor=%u zero_divisor=%u requested_chain=%u enumerate_result=%d\n",
                version, divisor.vertexAttributeInstanceRateDivisor, divisor.vertexAttributeInstanceRateZeroDivisor, unsigned(requested), int(result));
    }
    x64ValidateBc(physical,&features->features);
}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*);
static PFN_vkVoidFunction x64FeatureProc(const char* name) {
    if(!name)return nullptr;
    if(auto profile=x64profile::proc(name))return profile;
    if(auto bgra=x64bgra::proc(name))return bgra;
    if(!strcmp(name,"vkGetInstanceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if(!strcmp(name,"vkGetDeviceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if(!strcmp(name,"vkGetPhysicalDeviceFeatures"))return reinterpret_cast<PFN_vkVoidFunction>(x64GetFeatures);
    if(!strcmp(name,"vkGetPhysicalDeviceFeatures2")||!strcmp(name,"vkGetPhysicalDeviceFeatures2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(x64GetFeatures2);
    return nullptr;
}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char* name) {
    if(auto proc=x64FeatureProc(name))return proc;
    return x64BaseGetInstanceProcAddr(instance,name);
}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char* name) {
    if(auto proc=x64FeatureProc(name))return proc;
    return x64BaseGetDeviceProcAddr(device,name);
}
