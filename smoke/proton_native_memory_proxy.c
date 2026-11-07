/* Diagnostic only: opt into the existing negotiated native-memory protocol.
 * Forward the loader entry points to the byte-identical baseline Wine WSI. */
extern int setenv(const char *, const char *, int);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern long write(int, const void *, unsigned long);
static void *baseline;
__attribute__((constructor)) static void init(void) {
    setenv("VN_OHOS_NATIVE_MEMORY", "1", 1);
    baseline=dlopen("/data/storage/el2/base/files/steamohos-native/proton-probe/graphics/libproton_vulkan_baseline.so",2);
    if(baseline)write(2,"GNU_X64_NATIVE_MEMORY proxy_ready=1 protocol_requested=1\n",sizeof("GNU_X64_NATIVE_MEMORY proxy_ready=1 protocol_requested=1\n")-1);
}
void *vkGetInstanceProcAddr(void *instance,const char *name) {
    typedef void *(*Fn)(void *,const char *);
    Fn f=baseline?(Fn)dlsym(baseline,"vkGetInstanceProcAddr"):0;
    return f?f(instance,name):0;
}
void *vkGetDeviceProcAddr(void *device,const char *name) {
    typedef void *(*Fn)(void *,const char *);
    Fn f=baseline?(Fn)dlsym(baseline,"vkGetDeviceProcAddr"):0;
    return f?f(device,name):0;
}
int vkEnumerateInstanceVersion(unsigned *version) {
    typedef int (*Fn)(unsigned *);Fn f=baseline?(Fn)dlsym(baseline,"vkEnumerateInstanceVersion"):0;
    return f?f(version):-3;
}
int vkEnumerateInstanceExtensionProperties(const char *layer,unsigned *count,void *out) {
    typedef int (*Fn)(const char *,unsigned *,void *);Fn f=baseline?(Fn)dlsym(baseline,"vkEnumerateInstanceExtensionProperties"):0;
    return f?f(layer,count,out):-3;
}
int vkCreateInstance(const void *info,const void *alloc,void **instance) {
    typedef int (*Fn)(const void *,const void *,void **);Fn f=baseline?(Fn)dlsym(baseline,"vkCreateInstance"):0;
    return f?f(info,alloc,instance):-3;
}
