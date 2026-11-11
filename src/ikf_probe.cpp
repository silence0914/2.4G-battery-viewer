// ---------------------------------------------------------------------------
// ikf_probe -- 在 iKF (38B1:3412) 的所有 HID 集合上同时写和听
//
// 目的：找出「电量查询」的应答到底从哪个集合、用哪种 report id 出来。
//
// 报文格式来自官方网页工具 hub.ikeepfun.com 的逆向：
//   frame = [0x55, seq|0x80, 0x00, lenLo, lenHi, ...payload, 0x00 x N]  (63 字节)
//   get_battery  payload = [0xFA, 0x0C]
//   get_version  payload = [0x0D]
//   get_pdid     payload = [0xEF, 0x01]
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hidapi.h"

static std::string narrow(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static void dump(const unsigned char* p, int n) {
    for (int i = 0; i < n; ++i) std::printf("%02X ", p[i]);
    std::printf("\n");
}

// 组一帧 63 字节的 iKF 报文
static std::vector<unsigned char> pack(const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> p(63, 0x00);
    p[0] = 0x55;
    p[1] = 0x80;  // seq=0, LAST_PACKET
    p[2] = 0x00;
    p[3] = static_cast<unsigned char>(payload.size() & 0xFF);
    p[4] = static_cast<unsigned char>((payload.size() >> 8) & 0xFF);
    std::memcpy(&p[5], payload.data(), payload.size());
    return p;
}

struct Frame {
    const char* label;
    std::vector<unsigned char> payload;
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    hid_init();

    hid_device_info* devs = hid_enumerate(0x38B1, 0x3412);

    struct Slot {
        hid_device* h = nullptr;
        std::string path;
        unsigned short up = 0, usage = 0;
        int iface = 0;
    };
    std::vector<Slot> slots;

    for (hid_device_info* d = devs; d; d = d->next) {
        Slot s;
        s.path = d->path ? d->path : "";
        s.up = d->usage_page;
        s.usage = d->usage;
        s.iface = d->interface_number;
        s.h = hid_open_path(d->path);
        slots.push_back(s);
    }

    std::printf("opened %zu collections\n", slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        std::printf("  [%zu] if=%d up=0x%04X u=0x%04X %s %s\n", i, slots[i].iface,
                    slots[i].up, slots[i].usage,
                    slots[i].h ? "OPEN" : "closed",
                    slots[i].path.c_str());
    }

    const Frame frames[] = {
        {"get_battery [FA 0C]", {0xFA, 0x0C}},
        {"get_version [0D]", {0x0D}},
        {"get_pdid [EF 01]", {0xEF, 0x01}},
    };

    unsigned char buf[512];

    // 两种写入口径：带 report id 0x02、不带
    const int reportIds[] = {-1, 0x02};

    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].h) continue;

        for (int rid : reportIds) {
            std::printf("\n== write to [%zu] if=%d up=0x%04X u=0x%04X  reportId=%d ==\n",
                        i, slots[i].iface, slots[i].up, slots[i].usage, rid);

            for (const Frame& f : frames) {
                std::vector<unsigned char> body = pack(f.payload);
                std::vector<unsigned char> out;
                if (rid >= 0) out.push_back(static_cast<unsigned char>(rid));
                out.insert(out.end(), body.begin(), body.end());

                int w = hid_write(slots[i].h, out.data(), out.size());
                std::printf("  %-24s write=%d (%zu bytes)\n", f.label, w, out.size());
                Sleep(40);
            }

            // 在刚写完的这个集合上听 700ms
            ULONGLONG t0 = GetTickCount64();
            int got = 0;
            while (GetTickCount64() - t0 < 700) {
                int r = hid_read_timeout(slots[i].h, buf, sizeof(buf), 60);
                if (r > 0) {
                    std::printf("  >> input on [%zu] (%d): ", i, r);
                    dump(buf, r);
                    ++got;
                }
            }
            if (!got) std::printf("  (no input)\n");
        }
    }

    // 最后再对所有集合扫一遍，看有没有谁在推送
    std::printf("\n== global listen 1500 ms ==\n");
    ULONGLONG t0 = GetTickCount64();
    int total = 0;
    while (GetTickCount64() - t0 < 1500) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].h) continue;
            int r = hid_read_timeout(slots[i].h, buf, sizeof(buf), 15);
            if (r > 0) {
                std::printf("  >> [%zu] up=0x%04X (%d): ", i, slots[i].up, r);
                dump(buf, r);
                ++total;
            }
        }
    }
    if (!total) std::printf("  (nothing)\n");

    for (auto& s : slots) if (s.h) hid_close(s.h);
    hid_free_enumeration(devs);
    hid_exit();
    return 0;
}
