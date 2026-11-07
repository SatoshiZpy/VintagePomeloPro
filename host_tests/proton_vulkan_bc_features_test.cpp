#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_bc_features.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static VkFormat missing=VK_FORMAT_UNDEFINED;
static bool noLinear=false;
static unsigned calls=0;
static void VKAPI_CALL query(VkPhysicalDevice,VkFormat f,VkFormatProperties* p) {
    ++calls;
    *p={};
    p->linearTilingFeatures=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if(f!=missing) p->optimalTilingFeatures=p->linearTilingFeatures;
    else if(noLinear) p->optimalTilingFeatures=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
}
int main() {
    VkPhysicalDeviceFeatures original{};
    original.textureCompressionBC=VK_TRUE;original.geometryShader=VK_TRUE;original.samplerAnisotropy=VK_TRUE;
    auto f=original;
    assert(proton_x64_validate_bc_features(VK_NULL_HANDLE,query,&f)==VK_FORMAT_UNDEFINED);
    assert(calls==16&&!memcmp(&f,&original,sizeof f));
    for(int value=VK_FORMAT_BC1_RGB_UNORM_BLOCK;value<=VK_FORMAT_BC7_SRGB_BLOCK;++value) {
        missing=static_cast<VkFormat>(value);
        for(bool linear:{false,true}) {
            noLinear=linear;f=original;
            assert(proton_x64_validate_bc_features(VK_NULL_HANDLE,query,&f)==missing);
            assert(!f.textureCompressionBC);f.textureCompressionBC=VK_TRUE;
            assert(!memcmp(&f,&original,sizeof f));
        }
    }
    f=original;f.textureCompressionBC=VK_FALSE;calls=0;
    assert(proton_x64_validate_bc_features(VK_NULL_HANDLE,query,&f)==VK_FORMAT_UNDEFINED&&calls==0);
    puts("PASS: complete BC family, every missing format/filter, unrelated features preserved, existing false unchanged");
}
