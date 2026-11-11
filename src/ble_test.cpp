// ---------------------------------------------------------------------------
// bletest -- 验证蓝牙 GATT 扫描路径能不能正常走通
// ---------------------------------------------------------------------------

#include <cstdio>

#include "ble_battery.h"

int main() {
    std::printf("scanning BLE devices with Battery Service (0x180F)...\n");
    auto list = scan_ble_batteries(true);
    std::printf("found %zu device(s)\n", list.size());
    for (const auto& b : list) {
        std::printf("  %-40s %-18s %d%%\n", b.name.c_str(), b.address.c_str(), b.percent);
    }
    return 0;
}
