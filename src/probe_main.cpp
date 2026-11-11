// ---------------------------------------------------------------------------
// probe -- HID enumeration / report dump utility
//
// Purpose: find out which HID collection of each peripheral carries the
// battery level, before writing any UI.
//
//   probe          -> enumerate everything, dump caps + report descriptors
//   probe <secs>   -> additionally read input reports passively for <secs>
//
// Output goes to stdout and to ./probe_output.log (UTF-8).
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "hidapi.h"

// ---------------------------------------------------------------------------
// logging
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;

static void out(const char* fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    if (g_log) fputs(buf, g_log);
}

static std::string narrow(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static void hexdump(const unsigned char* p, int n, const char* indent) {
    char line[256];
    int pos = 0;
    line[0] = '\0';
    for (int i = 0; i < n; ++i) {
        pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), "%02X ", p[i]);
        if ((i + 1) % 24 == 0 || i == n - 1) {
            out("%s%s\n", indent, line);
            pos = 0;
            line[0] = '\0';
        }
    }
}

// ---------------------------------------------------------------------------
// HID capabilities (report byte lengths) via a plain CreateFile handle
// ---------------------------------------------------------------------------
struct Caps {
    USHORT input_bytes = 0;
    USHORT output_bytes = 0;
    USHORT feature_bytes = 0;
    USHORT link_nodes = 0;
    bool ok = false;
};

static Caps query_caps(const char* path) {
    Caps c;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return c;

    PHIDP_PREPARSED_DATA pp = nullptr;
    if (HidD_GetPreparsedData(h, &pp)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
            c.input_bytes = caps.InputReportByteLength;
            c.output_bytes = caps.OutputReportByteLength;
            c.feature_bytes = caps.FeatureReportByteLength;
            c.link_nodes = caps.NumberLinkCollectionNodes;
            c.ok = true;
        }
        HidD_FreePreparsedData(pp);
    }
    CloseHandle(h);
    return c;
}

// ---------------------------------------------------------------------------
// the three devices we care about
// ---------------------------------------------------------------------------
struct Target {
    unsigned short vid;
    const char* name;
};

static const Target kTargets[] = {
    {0x046D, "Logitech G903 (LIGHTSPEED receiver)"},
    {0x05AC, "Valkyrie VK75-Loki (2.4G receiver, spoofs Apple VID)"},
    {0x1D57, "unknown 2.4G combo (stale entry)"},
    {0x38B1, "iKF V11 Pro+ headset (Jieli)"},
};

static const char* target_name(unsigned short vid) {
    for (const auto& t : kTargets)
        if (t.vid == vid) return t.name;
    return nullptr;
}

// ---------------------------------------------------------------------------
// passive read of input reports
// ---------------------------------------------------------------------------
static void passive_read(hid_device* h, int seconds) {
    std::vector<std::pair<std::string, int>> seen;  // payload -> count, in first-seen order
    unsigned char buf[1024];

    ULONGLONG start = GetTickCount64();
    int total = 0;
    while (GetTickCount64() - start < static_cast<ULONGLONG>(seconds) * 1000ULL) {
        int r = hid_read_timeout(h, buf, sizeof(buf), 200);
        if (r < 0) {
            out("    read error: %s\n", narrow(hid_error(h)).c_str());
            return;
        }
        if (r == 0) continue;
        total++;
        std::string key(reinterpret_cast<const char*>(buf), static_cast<size_t>(r));
        bool found = false;
        for (auto& kv : seen) {
            if (kv.first == key) { kv.second++; found = true; break; }
        }
        if (!found) {
            seen.emplace_back(key, 1);
            out("    IN  %3d bytes : ", r);
            hexdump(buf, r, "");
        }
    }

    if (total == 0) {
        out("    (no input reports in %d s)\n", seconds);
        return;
    }
    out("    total %d reports, %zu distinct\n", total, seen.size());
    for (auto& kv : seen) {
        if (kv.second > 1) {
            out("      x%-4d : ", kv.second);
            hexdump(reinterpret_cast<const unsigned char*>(kv.first.data()),
                    static_cast<int>(kv.first.size()), "");
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    int read_seconds = 0;
    if (argc > 1) read_seconds = atoi(argv[1]);
    if (read_seconds < 0) read_seconds = 0;

    g_log = fopen("probe_output.log", "wb");
    out("probe: hidapi %s\n", hid_version_str());
    out("read_seconds = %d\n\n", read_seconds);

    if (hid_init() != 0) {
        out("hid_init failed\n");
        return 1;
    }

    hid_device_info* devs = hid_enumerate(0x0, 0x0);

    int total = 0;
    for (hid_device_info* d = devs; d; d = d->next) total++;
    out("========================================================================\n");
    out("ALL HID COLLECTIONS: %d\n", total);
    out("========================================================================\n");
    for (hid_device_info* d = devs; d; d = d->next) {
        const char* tn = target_name(d->vendor_id);
        out("%s[%04X:%04X] up=0x%04X u=0x%04X if=%d  mfg='%s'\n    product='%s'\n    path=%s\n",
            tn ? ">> " : "   ",
            d->vendor_id, d->product_id, d->usage_page, d->usage, d->interface_number,
            narrow(d->manufacturer_string).c_str(),
            narrow(d->product_string).c_str(),
            d->path ? d->path : "(null)");
    }

    // ---- detailed pass over the target devices -------------------------------
    for (hid_device_info* d = devs; d; d = d->next) {
        const char* tn = target_name(d->vendor_id);
        if (!tn) continue;

        out("\n========================================================================\n");
        out("PROBE  %s  [%04X:%04X]  up=0x%04X u=0x%04X  if=%d\n",
            tn, d->vendor_id, d->product_id, d->usage_page, d->usage, d->interface_number);
        out("========================================================================\n");

        Caps c = query_caps(d->path);
        if (c.ok)
            out("  caps: input=%u out=%u feature=%u link_nodes=%u\n",
                c.input_bytes, c.output_bytes, c.feature_bytes, c.link_nodes);
        else
            out("  caps: unavailable\n");

        hid_device* h = hid_open_path(d->path);
        if (!h) {
            out("  OPEN FAILED: %s\n", narrow(hid_error(nullptr)).c_str());
            continue;
        }

        unsigned char desc[8192];
        int dl = hid_get_report_descriptor(h, desc, sizeof(desc));
        out("  report descriptor: %d bytes\n", dl);
        if (dl > 0) {
            int show = dl < 320 ? dl : 320;
            hexdump(desc, show, "    ");
            if (dl > show) out("    ... (%d more bytes)\n", dl - show);
        }

        if (read_seconds > 0)
            passive_read(h, read_seconds);

        hid_close(h);
    }

    hid_free_enumeration(devs);
    hid_exit();
    out("\ndone.\n");
    if (g_log) fclose(g_log);
    return 0;
}
