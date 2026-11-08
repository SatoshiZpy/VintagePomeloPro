#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <cassert>
struct Device { VkPhysicalDevice physical; };
static std::recursive_mutex stateMutex;
static std::map<VkDevice, Device> devices;
static VkInstance physicalInstance(VkPhysicalDevice) { return VK_NULL_HANDLE; }
static bool bgraStorage=false, r32Storage=true;
static VkResult original=VK_ERROR_FORMAT_NOT_SUPPORTED, extended=VK_SUCCESS, creation=VK_SUCCESS;
static unsigned queries=0, creates=0;
static VkImageCreateFlags actualFlags=0;
static const void* expectedNext=nullptr;
static const VkAllocationCallbacks* expectedAllocator=nullptr;
static void VKAPI_CALL formatProperties(VkPhysicalDevice, VkFormat f, VkFormatProperties* out) {
    *out={};out->optimalTilingFeatures=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((f==VK_FORMAT_B8G8R8A8_UNORM?bgraStorage:r32Storage)) out->optimalTilingFeatures|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
}
static VkResult VKAPI_CALL imageProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling,
        VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* out) {
    ++queries;actualFlags=flags;assert(usage==(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT));
    auto result=flags&VK_IMAGE_CREATE_EXTENDED_USAGE_BIT?extended:original;
    *out={}; if(result==VK_SUCCESS)out->maxExtent={4096,4096,1};
    return result;
}
static VkResult VKAPI_CALL imageProperties2(VkPhysicalDevice p, const VkPhysicalDeviceImageFormatInfo2* in,
        VkImageFormatProperties2* out) {
    assert(in->pNext==expectedNext);
    return imageProperties(p,in->format,in->type,in->tiling,in->usage,in->flags,&out->imageFormatProperties);
}
static VkResult VKAPI_CALL createImage(VkDevice,const VkImageCreateInfo* in,const VkAllocationCallbacks* alloc,VkImage* out) {
    ++creates; actualFlags=in->flags;assert(in->pNext==expectedNext&&alloc==expectedAllocator);
    assert(in->extent.width==1&&in->extent.height==1&&in->mipLevels==1);
    if(creation==VK_SUCCESS)*out=reinterpret_cast<VkImage>(uintptr_t(5));
    return creation;
}
template<class T> static T instanceProc(VkInstance,const char* name) {
    if(!strcmp(name,"vkGetPhysicalDeviceFormatProperties"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(formatProperties));
    if(!strcmp(name,"vkGetPhysicalDeviceImageFormatProperties"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(imageProperties));
    if(!strcmp(name,"vkGetPhysicalDeviceImageFormatProperties2"))return reinterpret_cast<T>(reinterpret_cast<PFN_vkVoidFunction>(imageProperties2));
    return nullptr;
}
template<class T> static T deviceProc(VkDevice,const char*) { return reinterpret_cast<T>(createImage); }
#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_bgra_usage.h"
int main() {
    auto physical=reinterpret_cast<VkPhysicalDevice>(uintptr_t(1));
    auto device=reinterpret_cast<VkDevice>(uintptr_t(2));devices[device]={physical};
    VkPhysicalDeviceImageFormatInfo2 info{};info.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.format=VK_FORMAT_B8G8R8A8_UNORM;info.type=VK_IMAGE_TYPE_2D;info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;info.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    VkImageFormatProperties output{};
    auto run=[&](){queries=0;return x64bgra::query(physical,info.format,info.type,info.tiling,info.usage,info.flags,&output);};
    assert(run()==VK_SUCCESS&&queries==2&&output.maxExtent.width==4096);
    extended=VK_ERROR_FORMAT_NOT_SUPPORTED;assert(run()==extended&&queries==2); // no invented support
    extended=VK_SUCCESS;original=VK_ERROR_OUT_OF_HOST_MEMORY;assert(run()==original&&queries==1);
    original=VK_SUCCESS;assert(run()==VK_SUCCESS&&queries==1); // successful driver query unchanged
    original=VK_ERROR_FORMAT_NOT_SUPPORTED;bgraStorage=true;assert(run()==original&&queries==1);
    bgraStorage=false;r32Storage=false;assert(run()==original&&queries==1);
    r32Storage=true;info.flags=0;assert(run()==original&&queries==1);
    info.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;info.format=VK_FORMAT_R8G8B8A8_UNORM;assert(run()==original&&queries==1);
    info.format=VK_FORMAT_B8G8R8A8_UNORM;info.tiling=VK_IMAGE_TILING_LINEAR;assert(run()==original&&queries==1);
    info.tiling=VK_IMAGE_TILING_OPTIMAL;
    VkFormat formats[]={VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R32_UINT};
    VkImageFormatListCreateInfo list{};list.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;list.viewFormatCount=2;list.pViewFormats=formats;
    info.pNext=expectedNext=&list;
    VkImageFormatProperties2 output2{};output2.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    queries=0;assert(x64bgra::query2(physical,&info,&output2)==VK_SUCCESS&&queries==2);
    assert(info.flags==VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT&&info.pNext==&list); // caller untouched
    list.viewFormatCount=1;queries=0;assert(x64bgra::query2(physical,&info,&output2)==original&&queries==1);
    list.viewFormatCount=2;
    VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ci.pNext=&list;ci.format=info.format;ci.tiling=info.tiling;
    ci.usage=info.usage;ci.flags=info.flags;ci.extent={1,1,1};ci.mipLevels=1;
    VkAllocationCallbacks allocator{};expectedAllocator=&allocator;VkImage image=VK_NULL_HANDLE;
    assert(x64bgra::create(device,&ci,&allocator,&image)==VK_SUCCESS&&creates==1&&image);
    assert(actualFlags==(ci.flags|VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)&&ci.flags==info.flags);
    creation=VK_ERROR_OUT_OF_DEVICE_MEMORY;assert(x64bgra::create(device,&ci,&allocator,&image)==creation&&creates==2);
    list.viewFormatCount=1;assert(x64bgra::create(device,&ci,&allocator,&image)==creation&&actualFlags==ci.flags);
    assert(x64bgra::proc("vkGetPhysicalDeviceImageFormatProperties2KHR")==x64bgra::proc("vkGetPhysicalDeviceImageFormatProperties2"));
    assert(!x64bgra::proc("vkGetPhysicalDeviceFormatProperties")); // real format features never fabricated
    puts("PASS: BGRA/R32 extended usage, constrained views, original errors and limits, immutable chains, one creation call, unrelated paths unchanged");
}
