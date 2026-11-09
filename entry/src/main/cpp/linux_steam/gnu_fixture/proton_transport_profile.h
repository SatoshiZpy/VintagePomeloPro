// Private, opt-in A/B diagnostics. The normal transport policy is unchanged.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace protontransport {
enum class Mode { Off, Direct, Event };
inline Mode readMode(const char* path) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return Mode::Off;
    struct stat st{};char text[16]{};
    bool valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size>0&&st.st_size<16;
    ssize_t n=valid?read(fd,text,sizeof(text)):-1;close(fd);
    if(!valid||n!=st.st_size)return Mode::Off;
    if(n&&text[n-1]=='\n')text[--n]=0;
    if(n==6&&!memcmp(text,"direct",6))return Mode::Direct;
    if(n==5&&!memcmp(text,"event",5))return Mode::Event;
    return Mode::Off;
}
inline void configure(Mode mode) {
    if(mode==Mode::Off)return;
    setenv("VN_WINEHUA_PERF_SUMMARY","1",1);
    setenv("VN_WINEHUA_DIRECT_FENCE_WAIT",mode==Mode::Direct?"1":"0",1);
    setenv("VN_WINEHUA_EVENT_FENCE_WAIT",mode==Mode::Event?"1":"0",1);
    fprintf(stderr,"GNU_X64_TRANSPORT diagnostic=1 fence_mode=%s perf_summary=1 no_multi_ring=unchanged\n",
            mode==Mode::Direct?"direct":"event");
}
}
