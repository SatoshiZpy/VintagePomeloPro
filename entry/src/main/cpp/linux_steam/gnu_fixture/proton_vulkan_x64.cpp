// Compile the Wine WSI with an explicitly scoped, paired x64 ICD.
// The Native FEX WSI and the legacy i386 Wine ICD keep their current paths.
#include <dlfcn.h>
#include <cstring>
static void* x64GraphicsOpen(const char* path,int flags) {
    if(path&&!strcmp(path,"/data/storage/el2/base/files/steamohos-native/proton-probe/graphics/libvulkan_virtio.so"))
        return dlopen("/data/storage/el2/base/files/steamohos-native/proton-probe/graphics-x64/libvulkan_virtio.so",flags);
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
