// ---------------------------------------------------------------------------
// listen_all -- 同时监听某个 VID 下所有 HID 集合的输入报文
//
// 用法: listen_all <VID十六进制> <秒数>
// 例:   listen_all 05AC 300
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hidapi.h"

struct Slot {
    hid_device* h = nullptr;
    unsigned short up = 0;
    unsigned short usage = 0;
    int iface = 0;
    std::string path;
};

int main(int argc, char** argv) {
    unsigned short vid = 0x05AC;
    if (argc > 1) vid = static_cast<unsigned short>(strtoul(argv[1], nullptr, 16));
    int seconds = 120;
    if (argc > 2) seconds = atoi(argv[2]);

    SetConsoleOutputCP(CP_UTF8);
    hid_init();

    hid_device_info* devs = hid_enumerate(vid, 0x0);
    std::vector<Slot> slots;
    for (hid_device_info* d = devs; d; d = d->next) {
        Slot s;
        s.h = hid_open_path(d->path);
        s.up = d->usage_page;
        s.usage = d->usage;
        s.iface = d->interface_number;
        s.path = d->path ? d->path : "";
        slots.push_back(s);
    }

    std::printf("VID %04X: %zu collections\n", vid, slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        std::printf("  [%zu] if=%d up=0x%04X u=0x%04X %s  %s\n", i, slots[i].iface,
                    slots[i].up, slots[i].usage, slots[i].h ? "OPEN" : "fail",
                    slots[i].path.c_str());
    }
    std::printf("\nlistening %d s ...\n", seconds);
    std::fflush(stdout);

    unsigned char buf[512];
    ULONGLONG start = GetTickCount64();
    unsigned long long total = 0;

    while (GetTickCount64() - start < static_cast<ULONGLONG>(seconds) * 1000ULL) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].h) continue;
            int r = hid_read_timeout(slots[i].h, buf, sizeof(buf), 4);
            if (r <= 0) continue;
            ++total;
            std::printf("[%zu up=0x%04X u=0x%04X] ", i, slots[i].up, slots[i].usage);
            for (int k = 0; k < r; ++k) std::printf("%02X ", buf[k]);
            std::printf("\n");
            std::fflush(stdout);
        }
    }

    std::printf("\ndone, %llu reports\n", total);

    for (auto& s : slots) if (s.h) hid_close(s.h);
    hid_free_enumeration(devs);
    hid_exit();
    return 0;
}
