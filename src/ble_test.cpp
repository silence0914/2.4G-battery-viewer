// ---------------------------------------------------------------------------
// bletest -- 诊断蓝牙通路
//
//  1. 列出所有 BLE 设备，标出哪些带 Battery Service
//  2. 走一遍正式的电量扫描路径
// ---------------------------------------------------------------------------

#include <cstdio>

#include "ble_battery.h"

int main() {
    std::printf("=== all BLE devices ===\n");
    auto all = scan_ble_devices();
    if (all.empty()) {
        std::printf("  (none)\n");
    } else {
        for (const auto& d : all) {
            std::printf("  %-32s %-18s battery=%s",
                        d.name.c_str(), d.address.c_str(),
                        d.has_battery ? "yes" : "no");
            if (d.has_battery && d.percent >= 0) std::printf("  %d%%", d.percent);
            std::printf("\n");
        }
    }

    std::printf("\n=== battery scan (0x180F / 0x2A19) ===\n");
    auto list = scan_ble_batteries(true);
    std::printf("  found %zu device(s)\n", list.size());
    for (const auto& b : list) {
        std::printf("  %-32s %-18s %d%%\n", b.name.c_str(), b.address.c_str(), b.percent);
    }

    std::printf("\n=== what Windows itself reports (System.Devices.BatteryLife) ===\n");
    auto win = scan_windows_battery();
    if (win.empty()) {
        std::printf("  (no paired bluetooth device readable)\n");
    } else {
        for (const auto& w : win) {
            char buf[32];
            if (w.percent >= 0) std::snprintf(buf, sizeof(buf), "%d%%", w.percent);
            else std::snprintf(buf, sizeof(buf), "n/a");
            std::printf("  %-32s battery=%s\n", w.name.c_str(), buf);
        }
    }
    return 0;
}
