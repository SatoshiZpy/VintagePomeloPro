#include "../entry/src/main/cpp/linux_steam/proton_graphics/pe_image.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <iostream>
int main() {
    char directory[]="/tmp/proton-pe-XXXXXX";assert(mkdtemp(directory));
    std::string root(directory),image=root+"/game.exe";
    std::vector<unsigned char> bytes(512,0);bytes[0]='M';bytes[1]='Z';bytes[60]=128;
    bytes[128]='P';bytes[129]='E';bytes[132]=0x64;bytes[133]=0x86;bytes[152]=0x0b;bytes[153]=2;
    auto write=[&](){std::ofstream f(image,std::ios::binary);f.write(reinterpret_cast<char*>(bytes.data()),bytes.size());};
    using steamohos::ProtonPeMachine;
    write();assert(ProtonPeMachine(image)==0x8664);
    bytes[132]=0x4c;bytes[133]=1;bytes[153]=1;write();assert(ProtonPeMachine(image)==0x14c);
    bytes[153]=2;write();assert(ProtonPeMachine(image)==0); // ABI and optional header disagree
    bytes[132]=0x64;bytes[133]=0xaa;write();assert(ProtonPeMachine(image)==0); // native ARM is not AMD64
    bytes[132]=0x64;bytes[133]=0x86;write();
    assert(!symlink(image.c_str(),(root+"/linked.exe").c_str()));assert(ProtonPeMachine(root+"/linked.exe")==0);
    bytes[60]=0xff;bytes[61]=0xff;bytes[62]=0xff;bytes[63]=0xff;write();assert(ProtonPeMachine(image)==0);
    bytes.resize(63);write();assert(ProtonPeMachine(image)==0);
    assert(ProtonPeMachine(root)==0);assert(ProtonPeMachine(root+"/missing")==0);
    unlink((root+"/linked.exe").c_str());unlink(image.c_str());rmdir(root.c_str());
    std::cout<<"PASS: actual PE architecture, wrong magic/ABI, ARM image, symlink, overflow, truncation, directory and missing executable\n";
}
