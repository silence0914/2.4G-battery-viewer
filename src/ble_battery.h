#pragma once

#include <string>
#include <vector>

struct BleDeviceBattery {
    std::string name;
    std::string address;
    int percent = -1;
};

// 扫描带标准 Battery Service（0x180F / 0x2A19）的 BLE 设备并读电量。
// 阻塞调用；内部有 30 秒缓存，force=true 时强制重新扫描。
std::vector<BleDeviceBattery> scan_ble_batteries(bool force = false);
