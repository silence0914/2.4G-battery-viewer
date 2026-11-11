#pragma once

#include <string>
#include <vector>

struct BleDeviceBattery {
    std::string name;
    std::string address;
    int percent = -1;
};

// 诊断用：列出所有 BLE 设备，并标出哪一个带 Battery Service
struct BleDeviceInfo {
    std::string name;
    std::string address;
    bool has_battery = false;
    int percent = -1;
};

std::vector<BleDeviceInfo> scan_ble_devices();

// Windows 自己记录的电量（System.Devices.BatteryLife）。
// 经典蓝牙设备不走 GATT，但系统有时仍能从 HFP 等渠道拿到电量。
struct WindowsBatteryInfo {
    std::string name;
    std::string id;
    int percent = -1;
};

std::vector<WindowsBatteryInfo> scan_windows_battery();

// 扫描带标准 Battery Service（0x180F / 0x2A19）的 BLE 设备并读电量。
// 阻塞调用；内部有 30 秒缓存，force=true 时强制重新扫描。
std::vector<BleDeviceBattery> scan_ble_batteries(bool force = false);
