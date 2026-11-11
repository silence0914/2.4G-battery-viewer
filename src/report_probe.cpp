// ---------------------------------------------------------------------------
// report_probe -- 对每个 HID 集合尝试 HidD_GetInputReport / GetFeatureReport
//                 看看哪些 report id 能同步读出数据
//
// 用法: report_probe <VID十六进制>
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hidapi.h"

static void hexdump(const unsigned char* p, int n) {
    for (int i = 0; i < n; ++i) std::printf("%02X ", p[i]);
    std::printf("\n");
}

int main(int argc, char** argv) {
    unsigned short vid = 0x05AC;
    if (argc > 1) vid = static_cast<unsigned short>(strtoul(argv[1], nullptr, 16));

    SetConsoleOutputCP(CP_UTF8);
    hid_init();

    hid_device_info* devs = hid_enumerate(vid, 0x0);
    int idx = 0;
    for (hid_device_info* d = devs; d; d = d->next, ++idx) {
        hid_device* h = hid_open_path(d->path);
        std::printf("\n[%d] if=%d up=0x%04X u=0x%04X  %s\n", idx, d->interface_number,
                    d->usage_page, d->usage, h ? "open" : "FAIL");
        if (!h) continue;

        unsigned char buf[256];

        for (int id = 0; id <= 0x10; ++id) {
            std::memset(buf, 0, sizeof(buf));
            buf[0] = static_cast<unsigned char>(id);
            int r = hid_get_input_report(h, buf, sizeof(buf));
            if (r > 0) {
                std::printf("   IN_REPORT id=0x%02X (%d): ", id, r);
                hexdump(buf, r);
            }
        }

        for (int id = 0; id <= 0x10; ++id) {
            std::memset(buf, 0, sizeof(buf));
            buf[0] = static_cast<unsigned char>(id);
            int r = hid_get_feature_report(h, buf, sizeof(buf));
            if (r > 0) {
                std::printf("   FEATURE   id=0x%02X (%d): ", id, r);
                hexdump(buf, r);
            }
        }

        hid_close(h);
    }

    hid_free_enumeration(devs);
    hid_exit();
    return 0;
}
