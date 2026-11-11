// ---------------------------------------------------------------------------
// hidpp_probe -- enumerate the HID++ 2.0 feature table of a Logitech receiver
//                and query every battery-related feature it finds.
//
// Protocol notes
//   long report  : 0x11 | dev | featIdx | (fn<<4)|swid | params[16]
//   response     : 0x11 | dev | featIdx | (fn<<4)|swid | ret[16]
//   error        : 0x11 | dev | 0xFF    | 0x8F        | err  | ...
//
//   root feature     0x0000, fn0 getFeature(featureId) -> idx,type,ver
//   FEATURE_SET      0x0001, fn0 getCount(), fn1 getFeatureId(idx)
//   BATTERY_VOLTAGE  0x1000, fn0 getBatteryVoltage()
//   UNIFIED_BATTERY  0x1004, fn0 getBatteryLevelStatus()
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "hidapi.h"

static void out(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

struct Link {
    hid_device* h = nullptr;
    unsigned char dev = 1;

    bool call(unsigned char feat, unsigned char fn,
              const unsigned char* params, int nparams,
              unsigned char* ret, int& nret) {
        unsigned char req[20] = {0};
        req[0] = 0x11;
        req[1] = dev;
        req[2] = feat;
        req[3] = static_cast<unsigned char>(fn << 4);  // swid 0
        for (int i = 0; i < nparams && i < 16; ++i) req[4 + i] = params[i];

        if (hid_write(h, req, sizeof(req)) < 0) {
            out("    write failed: %ls\n", hid_error(h));
            return false;
        }

        unsigned char buf[64];
        ULONGLONG t0 = GetTickCount64();
        while (GetTickCount64() - t0 < 800) {
            int r = hid_read_timeout(h, buf, sizeof(buf), 40);
            if (r <= 0) continue;
            if (r < 7) continue;
            if (buf[1] != dev) continue;                   // other device slot
            if (buf[2] == 0xFF && buf[3] == 0x8F) {        // HID++ error
                out("    <- error 0x%02X (feature 0x%02X fn %u)\n", buf[4], feat, fn);
                return false;
            }
            if (buf[2] != feat) continue;
            if ((buf[3] >> 4) != fn) continue;
            nret = r - 4;
            if (nret > 16) nret = 16;
            memcpy(ret, buf + 4, static_cast<size_t>(nret));
            return true;
        }
        return false;
    }
};

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    unsigned short vid = 0x046D;
    if (argc > 1) vid = static_cast<unsigned short>(strtol(argv[1], nullptr, 16));
    int want_dev = (argc > 2) ? static_cast<int>(strtol(argv[2], nullptr, 0)) : 0;

    if (hid_init() != 0) { out("hid_init failed\n"); return 1; }

    hid_device_info* devs = hid_enumerate(vid, 0x0);
    hid_device_info* target = nullptr;
    for (hid_device_info* d = devs; d; d = d->next) {
        if (d->usage_page == 0xFF00 && d->usage == 0x0002) { target = d; break; }
    }
    if (!target) {
        out("no HID++ long channel (usage page 0xFF00 / usage 0x0002) found for VID %04X\n", vid);
        hid_free_enumeration(devs);
        hid_exit();
        return 1;
    }
    out("long channel: %s\n\n", target->path ? target->path : "?");

    hid_device* h = hid_open_path(target->path);
    if (!h) { out("open failed: %ls\n", hid_error(nullptr)); return 1; }

    Link link;
    link.h = h;

    std::vector<unsigned char> devids;
    if (want_dev) devids.push_back(static_cast<unsigned char>(want_dev));
    else { devids.push_back(1); devids.push_back(2); devids.push_back(0xFF); }

    for (unsigned char dev : devids) {
        link.dev = dev;
        unsigned char ret[16] = {0};
        int n = 0;

        out("============================================================\n");
        out("device index 0x%02X\n", dev);
        out("============================================================\n");

        // root.getFeature(FEATURE_SET 0x0001) -> index
        unsigned char p1[2] = {0x00, 0x01};
        if (!link.call(0x00, 0x00, p1, 2, ret, n)) {
            out("  no answer - slot empty\n\n");
            continue;
        }
        unsigned char fsIdx = ret[0];
        out("  FEATURE_SET(0x0001) at index 0x%02X (type %u ver %u)\n", fsIdx, ret[1], ret[2]);
        if (fsIdx == 0) { out("  feature set missing\n\n"); continue; }

        // FEATURE_SET.fn0 -> count
        if (!link.call(fsIdx, 0x00, nullptr, 0, ret, n)) { out("  getCount failed\n\n"); continue; }
        int count = ret[0];
        out("  feature table entries: %d\n\n", count);

        struct Feat { int idx; unsigned short id; };
        std::vector<Feat> table;
        for (int i = 1; i <= count; ++i) {
            unsigned char pi[1] = {static_cast<unsigned char>(i)};
            unsigned char r2[16] = {0};
            int n2 = 0;
            if (!link.call(fsIdx, 0x01, pi, 1, r2, n2)) continue;
            unsigned short fid = static_cast<unsigned short>((r2[0] << 8) | r2[1]);
            table.push_back({i, fid});
            out("  [%2d] 0x%04X\n", i, fid);
        }

        // query the battery-ish features we know about
        struct Known { unsigned short id; const char* name; };
        static const Known known[] = {
            {0x1000, "BATTERY_VOLTAGE"},
            {0x1001, "BATTERY_UNIT"},
            {0x1004, "UNIFIED_BATTERY"},
            {0x1006, "BATTERY_STATUS?"},
        };
        out("\n  ---- battery candidates ----\n");
        for (const auto& k : known) {
            auto it = std::find_if(table.begin(), table.end(),
                                   [&](const Feat& f) { return f.id == k.id; });
            if (it == table.end()) {
                out("  0x%04X %-16s : not present\n", k.id, k.name);
                continue;
            }
            out("  0x%04X %-16s : index 0x%02X, fn0 -> ", k.id, k.name, it->idx);
            unsigned char r3[16] = {0};
            int n3 = 0;
            if (link.call(static_cast<unsigned char>(it->idx), 0x00, nullptr, 0, r3, n3)) {
                for (int i = 0; i < n3; ++i) out("%02X ", r3[i]);
                if (k.id == 0x1000 && n3 >= 2)
                    out("  (voltage %u mV, flags 0x%02X)", r3[0] | (r3[1] << 8), n3 > 2 ? r3[2] : 0);
                out("\n");
            } else {
                out("no reply\n");
            }
        }
        out("\n");
        break;  // first slot that answers wins
    }

    hid_close(h);
    hid_free_enumeration(devs);
    hid_exit();
    return 0;
}
