/* GNU GE-Proton x64 diagnostic. Reuse the production cube workload, but load
 * AMD64/ARM64X DXVK from a private directory instead of the managed PE32 directory.
 * The legacy bounded dxvk11-x86 runner slot can launch this PE64 executable;
 * acceptance must use the PE headers, pointer size and cube result, not the slot name.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3d9.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN64
#error This diagnostic must be compiled as an AMD64 PE executable
#endif

typedef HRESULT (WINAPI *CreateDeviceFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
    UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *,
    IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

/* Observe Vulkan results without changing requested features or return values. */
typedef FARPROC (WINAPI *GetVkProcFn)(void *, const char *);
typedef LONG (WINAPI *CreateVkDeviceFn)(void *, const void *, const void *, void **);
static GetVkProcFn real_get_vk_proc;
static CreateVkDeviceFn real_create_vk_device;
typedef void (WINAPI *GetVkFeaturesFn)(void *, DWORD *);
static GetVkFeaturesFn real_get_vk_features;
typedef void (WINAPI *GetVkFeatures2Fn)(void *, void *);
static GetVkFeatures2Fn real_get_vk_features2;
typedef struct TraceFeatureNode {DWORD type; struct TraceFeatureNode *next; DWORD values[64];} TraceFeatureNode;

typedef struct {
    DWORD type; const void *next; DWORD flags, queueCount; const void *queues;
    DWORD layerCount; const char *const *layers; DWORD extensionCount;
    const char *const *extensions; const DWORD *features;
} TraceDeviceInfo;

static LONG WINAPI trace_create_vk_device(void *physical, const void *info,
    const void *allocator, void **device)
{
    const TraceDeviceInfo *request = info;
    if (real_get_vk_features && request->features) {
        DWORD available[55] = {0};real_get_vk_features(physical, available);
        for (unsigned i = 0; i < 55; ++i)
            if (request->features[i] && !available[i])
                printf("GNU_X64_VULKAN unsupported_core_feature index=%u requested=%lu available=%lu\n",
                    i, request->features[i], available[i]);
    }
    if (real_get_vk_features2) {
        for (const TraceFeatureNode *node = request->next; node; node = node->next) {
            TraceFeatureNode query = {0}, root = {0};
            query.type = node->type;root.type = 1000059000;root.next = &query;
            real_get_vk_features2(physical, &root);
            unsigned count = node->type == 1000028000 ? 2 : 1;
            for (unsigned i = 0; i < count; ++i)
                printf("GNU_X64_VULKAN chain_feature type=%lu index=%u requested=%lu available=%lu\n",
                    node->type, i, node->values[i], query.values[i]);
        }
    }
    LONG result = real_create_vk_device(physical, info, allocator, device);
    printf("GNU_X64_VULKAN vkCreateDevice result=%ld\n", result);
    fflush(stdout);
    return result;
}
static FARPROC WINAPI trace_get_vk_proc(void *instance, const char *name)
{
    FARPROC result = real_get_vk_proc(instance, name);
    if (result && !strcmp(name, "vkCreateDevice")) {
        union { FARPROC raw; CreateVkDeviceFn call; } entry;
        entry.raw = result;
        real_create_vk_device = entry.call;
        union { FARPROC raw; GetVkFeaturesFn call; } features;
        features.raw = real_get_vk_proc(instance, "vkGetPhysicalDeviceFeatures");
        real_get_vk_features = features.call;
        union { FARPROC raw; GetVkFeatures2Fn call; } features2;
        features2.raw = real_get_vk_proc(instance, "vkGetPhysicalDeviceFeatures2");
        real_get_vk_features2 = features2.call;
        entry.call = trace_create_vk_device;
        return entry.raw;
    }
    return result;
}
static void trace_vulkan_import(HMODULE module)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)module + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return;
    IMAGE_IMPORT_DESCRIPTOR *import = (IMAGE_IMPORT_DESCRIPTOR *)((char *)module + rva);
    for (; import->Name; ++import) {
        const char *dll = (char *)module + import->Name;
        if (_stricmp(dll, "vulkan-1.dll") || !import->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)((char *)module + import->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)((char *)module + import->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)((char *)module + names->u1.AddressOfData);
            if (strcmp((char *)name->Name, "vkGetInstanceProcAddr")) continue;
            DWORD protection;
            if (!VirtualProtect(&slots->u1.Function, sizeof(ULONGLONG), PAGE_READWRITE, &protection)) continue;
            union { ULONGLONG address; GetVkProcFn call; } entry;
            entry.address = slots->u1.Function;
            if (!real_get_vk_proc) real_get_vk_proc = entry.call;
            entry.call = trace_get_vk_proc;
            slots->u1.Function = entry.address;
            VirtualProtect(&slots->u1.Function, sizeof(ULONGLONG), protection, &protection);
            printf("GNU_X64_VULKAN observer=installed\n");fflush(stdout);
        }
    }
}

static HMODULE load_dxvk64(const wchar_t *path)
{
    HMODULE module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        printf("GNU_X64_DXVK_LOAD_FAILED path=%ls error=%lu\n", path, GetLastError());
        fflush(stdout);
        return NULL;
    }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)module + dos->e_lfanew);
    printf("GNU_X64_DXVK_MODULE path=%ls machine=0x%04x pointer_bits=%u\n",
        path, nt->FileHeader.Machine, (unsigned)(8 * sizeof(void *)));
    fflush(stdout);
    DWORD config = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
    DWORD config_size = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size;
    int arm64x = 0;
    if (nt->FileHeader.Machine == IMAGE_FILE_MACHINE_ARM64 && config && config_size >= 208) {
        ULONGLONG chpe = 0;
        memcpy(&chpe, (char *)module + config + 200, sizeof(chpe));
        arm64x = chpe != 0;
    }
    printf("GNU_X64_DXVK_ABI amd64=%u arm64x=%u\n",
        nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64, arm64x);
    fflush(stdout);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 && !arm64x) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return NULL;
    }
    trace_vulkan_import(module);
    return module;
}

static HRESULT WINAPI gnu_x64_create_device(IDXGIAdapter *adapter,
    D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk,
    const DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swap,
    ID3D11Device **device, D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context)
{
    static CreateDeviceFn create;
    if (type != D3D_DRIVER_TYPE_HARDWARE) return DXGI_ERROR_UNSUPPORTED;
    if (!create) {
        /* This cube uses no stream-output or geometry streams. The project's
         * opt-in compatibility mode requests only supported optional features;
         * success here does not certify full D3D11 feature coverage for games. */
        char compatibility[8] = {0};
        if (!GetEnvironmentVariableA("WINEHUA_DXVK_RELAXED_FEATURES", compatibility, sizeof(compatibility)) ||
            strcmp(compatibility, "1")) {
            printf("GNU_X64_GRAPHICS profile_environment_missing=1\n");
            return HRESULT_FROM_WIN32(ERROR_BAD_ENVIRONMENT);
        }
        printf("GNU_X64_DXVK limited_features=1 workload=vertex_pixel_depth_cube\n");
        const char *modules[] = {"winevulkan.dll", "d3dcompiler_47.dll"};
        const char *symbols[] = {"vkGetInstanceProcAddr", "D3DCompile"};
        for (unsigned i = 0; i < 2; ++i) {
            HMODULE module = LoadLibraryA(modules[i]);
            DWORD error = module ? ERROR_SUCCESS : GetLastError();
            FARPROC symbol = module ? GetProcAddress(module, symbols[i]) : NULL;
            char path[MAX_PATH] = {0};
            if (module) GetModuleFileNameA(module, path, sizeof(path));
            printf("GNU_X64_DEPENDENCY module=%s path=%s loaded=%u error=%lu symbol=%s resolved=%u\n",
                modules[i], path, module != NULL, error, symbols[i], symbol != NULL);
            fflush(stdout);
            if (!symbol) return HRESULT_FROM_WIN32(error ? error : ERROR_PROC_NOT_FOUND);
        }
        if (!load_dxvk64(L"C:\\dxvk64\\dxgi.dll")) return HRESULT_FROM_WIN32(GetLastError());
        HMODULE d3d11 = load_dxvk64(L"C:\\dxvk64\\d3d11.dll");
        if (!d3d11) return HRESULT_FROM_WIN32(GetLastError());
        union { FARPROC raw; CreateDeviceFn call; } entry;
        entry.raw = GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
        create = entry.call;
        if (!create) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    return create(adapter, type, software, flags, levels, count, sdk, desc,
        swap, device, level, context);
}

static IDirect3D9 *WINAPI unused_d3d9(UINT sdk)
{
    (void)sdk;
    return NULL;
}

#define Direct3DCreate9 unused_d3d9
#define D3D11CreateDeviceAndSwapChain gnu_x64_create_device
#include "winehua_d3d_switch_cube.c"
