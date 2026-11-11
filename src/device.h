#pragma once

#include <string>

enum class DeviceKind {
    Mouse,
    Keyboard,
    Headset,
    Unknown,
};

enum class BatteryStatus {
    Ok,           // 读到电量
    Unsupported,  // 设备在，但电量通道未知
    Error,        // 设备在，查询失败
    Absent,       // 没找到设备
};

struct DeviceInfo {
    DeviceKind kind = DeviceKind::Unknown;
    std::string kind_label;
    std::string model;
    std::string transport;
    BatteryStatus status = BatteryStatus::Absent;
    int percent = -1;        // 0..100, -1 = 未知
    int millivolts = -1;     // 原始电压，-1 = 未知
    bool charging = false;
    std::string note;
};

// 单节锂电电压 -> 百分比（估算值，非厂商曲线）
int voltage_to_percent(int millivolts);
