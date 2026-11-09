#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_transport_profile.h"
#include <cassert>
#include <string>
int main() {
    using namespace protontransport;
    char directory[]="/tmp/proton-transport-XXXXXX";assert(mkdtemp(directory));
    std::string path=std::string(directory)+"/mode",link=std::string(directory)+"/link";
    auto write=[&](const char* s,size_t n){FILE* f=fopen(path.c_str(),"wb");assert(f);assert(fwrite(s,1,n,f)==n);fclose(f);};
    assert(readMode(path.c_str())==Mode::Off);
    setenv("VN_WINEHUA_DIRECT_FENCE_WAIT","sentinel",1);configure(Mode::Off);
    assert(!strcmp(getenv("VN_WINEHUA_DIRECT_FENCE_WAIT"),"sentinel"));
    write("direct\n",7);assert(readMode(path.c_str())==Mode::Direct);
    write("event",5);assert(readMode(path.c_str())==Mode::Event);
    write("events",6);assert(readMode(path.c_str())==Mode::Off);
    write("event\nextra",11);assert(readMode(path.c_str())==Mode::Off);
    write("event\0extra",11);assert(readMode(path.c_str())==Mode::Off);
    write("directxxxxxxxxxx",16);assert(readMode(path.c_str())==Mode::Off);
    write("",0);assert(readMode(path.c_str())==Mode::Off);
    write("event",5);assert(!symlink(path.c_str(),link.c_str()));assert(readMode(link.c_str())==Mode::Off);
    assert(readMode(directory)==Mode::Off);
    configure(Mode::Event);assert(!strcmp(getenv("VN_WINEHUA_EVENT_FENCE_WAIT"),"1"));assert(!strcmp(getenv("VN_WINEHUA_DIRECT_FENCE_WAIT"),"0"));
    configure(Mode::Direct);assert(!strcmp(getenv("VN_WINEHUA_EVENT_FENCE_WAIT"),"0"));assert(!strcmp(getenv("VN_WINEHUA_DIRECT_FENCE_WAIT"),"1"));
    assert(!strcmp(getenv("VN_WINEHUA_PERF_SUMMARY"),"1"));
    unlink(link.c_str());unlink(path.c_str());rmdir(directory);
    puts("transport mode parsing, isolation and A/B environment: PASS");
}
