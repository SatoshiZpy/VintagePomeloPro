#define _GNU_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "../entry/src/main/cpp/linux_steam/gnu_fixture/proton_file_mapping.h"
int main(void) {
    char temp[] = "/tmp/proton-pe-XXXXXX";
    int fd = mkstemp(temp);
    assert(fd >= 0 && !unlink(temp));
    unsigned char image[8192] = {'M', 'Z'};
    image[60] = 128;
    memcpy(image + 128, "PE\0\0", 4);
    const char *path = "/data/storage/el2/base/files/steamohos-native/proton-probe/prefix/drive_c/test.dll";
    int flags = MAP_PRIVATE | MAP_FIXED;
    for (unsigned machine = 0; machine < 4; ++machine) {
        unsigned types[] = {0xaa64, 0x8664, 0xa641, 0x014c};
        image[132] = types[machine] & 255; image[133] = types[machine] >> 8;
        assert(pwrite(fd, image, sizeof(image), 0) == sizeof(image));
        /* Identification reads the file header even after seeking to a
         * section. The mmap caller's section offset must not gate fallback. */
        assert(lseek(fd, 4096, SEEK_SET) == 4096);
        assert(proton_private_read_fallback(fd, flags, path, NULL));
        assert(proton_private_read_fallback(fd, flags,
            "/data/app/el2/100/base/com.vintage.pomelopro/files/steamohos-native/proton-probe/prefix/drive_c/test.dll", NULL));
        assert(lseek(fd, 0, SEEK_CUR) == 4096);
    }
    assert(!proton_private_read_fallback(fd, MAP_SHARED | MAP_FIXED, path, NULL));
    assert(!proton_private_read_fallback(fd, flags | MAP_ANONYMOUS, path, NULL));
    assert(!proton_private_read_fallback(fd, MAP_PRIVATE, path, NULL));
    assert(!proton_private_read_fallback(fd, flags, "/product/wine/test.dll", NULL));
    assert(!proton_private_read_fallback(fd, flags,
        "/data/app/el2/100/base/com.vintage.other/files/steamohos-native/proton-probe/test.dll", NULL));
    assert(!proton_private_read_fallback(fd, flags, "/data/storage/el2/base/files/steamohos-native/proton-probe-other/test.dll", NULL));
    const char *game = "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/NewPAL";
    const char *gamefile = "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/NewPAL/test.exe";
    assert(!proton_private_read_fallback(fd, flags, gamefile, NULL));
    assert(proton_private_read_fallback(fd, flags, gamefile, game));
    assert(!proton_private_read_fallback(fd, flags, gamefile, "/other/game"));
    const char *games[] = {"NewPAL", "Chinese Paladin 4", "SenrenBanka_trial"};
    for (unsigned i = 0; i < 3; ++i) {
        char selected[512], alias[512], sibling[1024];
        snprintf(selected, sizeof(selected), "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/%s", games[i]);
        snprintf(alias, sizeof(alias), "/storage/media/100/local/files/Docs/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/%s/game.dll", games[i]);
        assert(proton_private_read_fallback(fd, flags, alias, selected));
        snprintf(sibling, sizeof(sibling), "%s-other/game.dll", selected);
        assert(!proton_private_read_fallback(fd, flags, sibling, selected));
        assert(!proton_private_read_fallback(fd, flags, alias, "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/Other"));
    }
    /* Steam can install in either the shared Download library or its private
     * drive_c library. Resolve aliases only within the selected library. */
    const char *libraries[] = {
        "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/",
        "/storage/media/100/local/files/Docs/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/",
        "/data/storage/el2/base/files/.wine/drive_c/SteamLinuxARM64/library/steamapps/common/",
        "/data/app/el2/100/base/com.vintage.pomelopro/files/.wine/drive_c/SteamLinuxARM64/library/steamapps/common/"
    };
    for (unsigned selected_alias = 0; selected_alias < 4; ++selected_alias) {
        char selected[512];
        snprintf(selected, sizeof(selected), "%sDARK SOULS REMASTERED", libraries[selected_alias]);
        for (unsigned mapped_alias = 0; mapped_alias < 4; ++mapped_alias) {
            char mapped[1024];
            snprintf(mapped, sizeof(mapped), "%sDARK SOULS REMASTERED/DarkSoulsRemastered.exe", libraries[mapped_alias]);
            assert(proton_private_read_fallback(fd, flags, mapped, selected) ==
                   (selected_alias / 2 == mapped_alias / 2));
            assert(!proton_private_read_fallback(fd, MAP_SHARED | MAP_FIXED, mapped, selected));
            assert(!proton_private_read_fallback(fd, MAP_PRIVATE, mapped, selected));
            assert(!proton_private_read_fallback(fd, flags, mapped, NULL));
        }
        char sibling[1024], escaped[1024];
        snprintf(sibling, sizeof(sibling), "%s-other/game.exe", selected);
        snprintf(escaped, sizeof(escaped), "%s/../Other/game.exe", selected);
        assert(!proton_private_read_fallback(fd, flags, sibling, selected));
        assert(!proton_private_read_fallback(fd, flags, escaped, selected));
        assert(!proton_private_read_fallback(fd, flags,
            "/data/app/el2/100/base/com.vintage.other/files/.wine/drive_c/SteamLinuxARM64/library/steamapps/common/DARK SOULS REMASTERED/game.exe", selected));
    }
    image[132] = image[133] = 0;
    assert(pwrite(fd, image, sizeof(image), 0) == sizeof(image));
    /* Data files need the same force-exec-safe private mapping as images. */
    assert(!proton_private_read_fallback(fd, flags, path, NULL));
    assert(!ftruncate(fd, 64));
    assert(!proton_private_read_fallback(fd, flags, path, NULL));
    assert(proton_private_read_fallback(fd, flags,
        "/data/storage/el2/base/files/steamohos-native/proton-probe/package/files/share/wine/nls/locale.nls", NULL));
    assert(!proton_private_read_fallback(fd, flags,
        "/data/storage/el2/base/files/steamohos-native/proton-probe/tmp/.wine-123/server-1/tmpmap-42", NULL));
    assert(!proton_private_read_fallback(fd, flags,
        "/data/storage/el2/base/files/steamohos-native/proton-probe/libc.so.6", NULL));
    assert(proton_private_read_fallback(fd, flags,
        "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/Any New Game/data.cpk",
        "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/Any New Game"));
    assert(!proton_private_read_fallback(fd, flags, gamefile,
        "/storage/Users/currentUser/Download/com.vintage.pomelopro/SteamLinuxARM64/library/steamapps/common/NewPAL/../Other"));
    int descriptors[2];
    assert(!pipe(descriptors));
    assert(!proton_private_read_fallback(descriptors[0], flags, path, NULL));
    close(descriptors[0]); close(descriptors[1]);
    close(fd);
    puts("PASS: private image/data views, file offset, selected directory aliases, scope and shared mappings");
}
