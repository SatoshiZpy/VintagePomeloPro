#ifndef STEAMOHOS_PROTON_VULKAN_ADDRESSING_H
#define STEAMOHOS_PROTON_VULKAN_ADDRESSING_H
#include <stdlib.h>
/* The low-address alias is for i386. AMD64/ARM64EC can directly address the
 * native ICD mapping and must not replace Wine's reserved low-memory VMAs.
 * Clear inherited MAP32 as well, since child exec preserves the environment. */
static void proton_configure_vulkan_addressing(void) {
    if (!getenv("STEAMOHOS_PROTON_NATIVE_VULKAN")) return;
    if (getenv("STEAMOHOS_PROTON_X64_GRAPHICS"))
        unsetenv("STEAMOHOS_PROTON_VULKAN_MAP32");
    else
        setenv("STEAMOHOS_PROTON_VULKAN_MAP32", "1", 1);
}
#endif
