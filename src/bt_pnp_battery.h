#pragma once

#include <string>
#include <vector>

struct BtBatteryDevice {
    std::string name;      // 设备友好名，如 "iKF-V11 Pro+"
    std::string address;   // 12 位大写十六进制蓝牙地址，解析失败时为空
    int percent = -1;
};

// 读取 Windows 记录在蓝牙 PnP 设备节点上的电量。
//
// 属性键：{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2   （BYTE，0..100）
// 实测挂在 HFP 服务节点（UUID 0000111E）上，由蓝牙音频栈从耳机侧取回。
// 这条通路对经典蓝牙设备同样有效，不要求设备支持 BLE GATT。
std::vector<BtBatteryDevice> scan_bluetooth_pnp_battery();
