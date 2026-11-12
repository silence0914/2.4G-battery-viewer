#pragma once

#include <string>
#include <vector>

// 经典蓝牙（BR/EDR）链路状态。
//
// 为什么需要它：耳机断电后 Windows **不会**立刻删掉它的 BTHENUM 服务节点，
// SetupAPI 里依然能看到这个节点、以及上面缓存的电量属性。只看 DIGCF_PRESENT
// 会把已经关机的耳机误判成"已连接"，显示上一次的旧电量。
// BLUETOOTH_DEVICE_INFO::fConnected 才是链路层的真实状态。
//
// 地址格式：12 位大写十六进制，不带冒号，例如 "5514CD864055"。
//
// 返回值：false 表示查询本身失败（蓝牙服务不可用 / 没有无线电）。
// 这种情况下调用方**不要**拿空列表去否决设备，否则会把真连着的设备也藏掉。
bool query_connected_bluetooth(std::vector<std::string>* out);
