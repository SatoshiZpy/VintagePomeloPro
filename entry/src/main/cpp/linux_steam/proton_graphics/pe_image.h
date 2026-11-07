#pragma once
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace steamohos {
// Read the executable itself; a runner/stage name does not establish its ABI.
inline uint16_t ProtonPeMachine(const std::string& path) {
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
    struct Close {int fd;~Close(){close(fd);}} closeFd{fd};
    struct stat st{};unsigned char dos[64],header[26];
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||pread(fd,dos,sizeof(dos),0)!=sizeof(dos)||dos[0]!='M'||dos[1]!='Z')return 0;
    uint32_t offset=uint32_t(dos[60])|(uint32_t(dos[61])<<8)|(uint32_t(dos[62])<<16)|(uint32_t(dos[63])<<24);
    if(offset<64||uint64_t(offset)+sizeof(header)>uint64_t(st.st_size)||pread(fd,header,sizeof(header),offset)!=sizeof(header))return 0;
    if(header[0]!='P'||header[1]!='E'||header[2]||header[3])return 0;
    uint16_t machine=uint16_t(header[4])|(uint16_t(header[5])<<8);
    uint16_t magic=uint16_t(header[24])|(uint16_t(header[25])<<8);
    if(machine==0x8664&&magic==0x20b)return machine;
    if(machine==0x14c&&magic==0x10b)return machine;
    return 0;
}
}
