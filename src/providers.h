#pragma once

#include <vector>

#include "device.h"

// 枚举当前接入的外设并尽量读取电量。阻塞调用，放在工作线程里跑。
std::vector<DeviceInfo> poll_devices();
