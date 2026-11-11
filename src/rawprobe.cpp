// ---------------------------------------------------------------------------
// rawprobe -- send an arbitrary HID frame to a HID collection and dump replies.
//
//   rawprobe --vid 046D --up FF00 --usage 0001
//       -> just list matching collections
//
//   rawprobe --vid 046D --up FF00 --usage 0001 --send "10 01 00 00 10 00 00"
//       -> write that frame, print every reply for --read ms
//
// Options: --vid --pid --up --usage (hex, omit to match any)
//          --index N   pick the Nth match (default 0)
//          --send "HEX .."
//          --read MS   reply window (default 500)
//          --repeat N  send N times, collecting replies
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hidapi.h"

static void out(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex(const char* s, std::vector<unsigned char>& outv) {
    int hi = -1;
    for (const char* p = s; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == ',') continue;
        int v = hexval(*p);
        if (v < 0) return false;
        if (hi < 0) {
            hi = v;
        } else {
            outv.push_back(static_cast<unsigned char>((hi << 4) | v));
            hi = -1;
        }
    }
    return hi < 0;
}

static void dump(const unsigned char* p, int n) {
    for (int i = 0; i < n; ++i) out("%02X ", p[i]);
    out("\n");
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    int vid = -1, pid = -1, up = -1, usage = -1;
    int index = 0, readms = 500, repeat = 1, listenms = 0;
    int featId = -1, scanfeat = 0;
    std::vector<unsigned char> frame;
    bool have_frame = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> const char* {
            return (i + 1 < argc) ? argv[++i] : def;
        };
        if (a == "--vid")        vid = static_cast<int>(strtol(next(""), nullptr, 16));
        else if (a == "--pid")   pid = static_cast<int>(strtol(next(""), nullptr, 16));
        else if (a == "--up")    up = static_cast<int>(strtol(next(""), nullptr, 16));
        else if (a == "--usage") usage = static_cast<int>(strtol(next(""), nullptr, 16));
        else if (a == "--index") index = atoi(next("0"));
        else if (a == "--read")  readms = atoi(next("500"));
        else if (a == "--listen") listenms = atoi(next("3000"));
        else if (a == "--feat") featId = static_cast<int>(strtol(next(""), nullptr, 16));
        else if (a == "--scanfeat") scanfeat = 1;
        else if (a == "--repeat") repeat = atoi(next("1"));
        else if (a == "--send") {
            if (!parse_hex(next(""), frame)) {
                out("bad hex in --send\n");
                return 2;
            }
            have_frame = true;
        }
    }

    if (hid_init() != 0) { out("hid_init failed\n"); return 1; }

    std::vector<hid_device_info*> matches;
    hid_device_info* devs = hid_enumerate(
        vid < 0 ? 0x0 : static_cast<unsigned short>(vid),
        pid < 0 ? 0x0 : static_cast<unsigned short>(pid));
    for (hid_device_info* d = devs; d; d = d->next) {
        if (vid >= 0 && d->vendor_id != vid) continue;
        if (pid >= 0 && d->product_id != pid) continue;
        if (up >= 0 && d->usage_page != up) continue;
        if (usage >= 0 && d->usage != usage) continue;
        matches.push_back(d);
    }

    out("%zu matching collection(s)\n", matches.size());
    for (size_t i = 0; i < matches.size(); ++i) {
        out("  [%zu] %04X:%04X up=0x%04X u=0x%04X if=%d\n      %s\n",
            i, matches[i]->vendor_id, matches[i]->product_id,
            matches[i]->usage_page, matches[i]->usage, matches[i]->interface_number,
            matches[i]->path ? matches[i]->path : "(null)");
    }

    if (!have_frame && listenms <= 0 && featId < 0 && !scanfeat) {
        hid_free_enumeration(devs);
        hid_exit();
        return matches.empty() ? 1 : 0;
    }
    if (index < 0 || static_cast<size_t>(index) >= matches.size()) {
        out("no such --index\n");
        hid_free_enumeration(devs);
        hid_exit();
        return 1;
    }

    hid_device* h = hid_open_path(matches[index]->path);
    if (!h) {
        out("open failed: %ls\n", hid_error(nullptr));
        hid_free_enumeration(devs);
        hid_exit();
        return 1;
    }

    if (listenms > 0 && !have_frame) {
        out("\nLISTEN %d ms\n", listenms);
        unsigned char buf[256];
        ULONGLONG t0 = GetTickCount64();
        int got = 0;
        while (GetTickCount64() - t0 < static_cast<ULONGLONG>(listenms)) {
            int r = hid_read_timeout(h, buf, sizeof(buf), 100);
            if (r < 0) { out("  read error: %ls\n", hid_error(h)); break; }
            if (r == 0) continue;
            ++got;
            out("  IN (%d): ", r);
            dump(buf, r);
        }
        if (got == 0) out("  (nothing in %d ms)\n", listenms);
        hid_close(h);
        hid_free_enumeration(devs);
        hid_exit();
        return 0;
    }

    if (have_frame) {
    out("\nSEND (%zu bytes): ", frame.size());
    dump(frame.data(), static_cast<int>(frame.size()));

    unsigned char buf[256];
    for (int rep = 0; rep < repeat; ++rep) {
        int w = hid_write(h, frame.data(), frame.size());
        out("  write -> %d\n", w);

        ULONGLONG t0 = GetTickCount64();
        int got = 0;
        while (GetTickCount64() - t0 < static_cast<ULONGLONG>(readms)) {
            int r = hid_read_timeout(h, buf, sizeof(buf), 50);
            if (r < 0) { out("  read error: %ls\n", hid_error(h)); break; }
            if (r == 0) continue;
            ++got;
            out("  IN (%d): ", r);
            dump(buf, r);
        }
        if (got == 0) out("  (no reply in %d ms)\n", readms);
    }
    }

    // ---- feature report polling ----
    if (featId >= 0 || scanfeat) {
        auto try_feat = [&](int id) -> bool {
            unsigned char fb[256];
            std::memset(fb, 0, sizeof(fb));
            fb[0] = static_cast<unsigned char>(id);
            int r = hid_get_feature_report(h, fb, 64);
            if (r <= 0) return false;
            out("  FEAT 0x%02X (%d): ", id, r);
            dump(fb, r);
            return true;
        };

        if (scanfeat) {
            out("\nSCAN feature reports 0x00..0x10\n");
            for (int id = 0; id <= 0x10; ++id) try_feat(id);
        } else {
            out("\nPOLL feature 0x%02X for %d ms\n", featId, readms);
            ULONGLONG t0 = GetTickCount64();
            int got = 0;
            while (GetTickCount64() - t0 < static_cast<ULONGLONG>(readms)) {
                if (try_feat(featId)) ++got;
                Sleep(60);
            }
            if (got == 0) out("  (no feature data)\n");
        }
    }

    hid_close(h);
    hid_free_enumeration(devs);
    hid_exit();
    return 0;
}
