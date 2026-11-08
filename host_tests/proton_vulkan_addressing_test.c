#define _GNU_SOURCE 1
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_addressing.h"
int main(void) {
    unsetenv("STEAMOHOS_PROTON_NATIVE_VULKAN");
    unsetenv("STEAMOHOS_PROTON_X64_GRAPHICS");
    unsetenv("STEAMOHOS_PROTON_VULKAN_MAP32");
    proton_configure_vulkan_addressing();
    assert(!getenv("STEAMOHOS_PROTON_VULKAN_MAP32"));
    setenv("STEAMOHOS_PROTON_NATIVE_VULKAN", "1", 1);
    proton_configure_vulkan_addressing();
    assert(!strcmp(getenv("STEAMOHOS_PROTON_VULKAN_MAP32"), "1"));
    setenv("STEAMOHOS_PROTON_X64_GRAPHICS", "1", 1);
    proton_configure_vulkan_addressing();
    assert(!getenv("STEAMOHOS_PROTON_VULKAN_MAP32"));
    /* Child constructor must also remove a stale inherited value. */
    setenv("STEAMOHOS_PROTON_VULKAN_MAP32", "1", 1);
    proton_configure_vulkan_addressing();
    assert(!getenv("STEAMOHOS_PROTON_VULKAN_MAP32"));
    unsetenv("STEAMOHOS_PROTON_X64_GRAPHICS");
    proton_configure_vulkan_addressing();
    assert(!strcmp(getenv("STEAMOHOS_PROTON_VULKAN_MAP32"), "1"));
    puts("PASS: i386 low-address mappings preserved; x64 and child exec use full pointers");
}
