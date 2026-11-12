// ---------------------------------------------------------------------------
// 从蓝牙 PnP 设备节点读取 Windows 已经采集到的电量
//
// Windows 会把蓝牙耳机的电量写成一个设备属性：
//   {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2   DEVPROP_TYPE_BYTE  0..100
// 实测该属性出现在 HFP 服务节点（UUID 0000111E）上，也就是手机/耳机在
// HFP 上通过 AT+XAPL / AT+IPHONEACCEV 之类上报后，Windows 存下来的值。
//
// 这条通路不依赖 BLE，也不依赖厂商协议，只要 Windows 设置页能显示电量，
// 这里就能读到。
//
// 注意：耳机断电后节点和这个属性都还在（Windows 会保留已配对设备的节点），
// 所以必须再用经典蓝牙 API 确认链路真的是连着的，否则会读到上一次的旧值。
// ---------------------------------------------------------------------------

#include "bt_pnp_battery.h"

#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>

#include <algorithm>
#include <cwctype>
#include <string>

#include "bt_link.h"

#pragma comment(lib, "setupapi.lib")

namespace {

const DEVPROPKEY kBluetoothBattery = {
    {0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}},
    2};

std::string narrow(const wchar_t* w) {
    if (!w || !w[0]) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 设备实例 ID 的尾巴形如 ...&0&5514CD864055_C00000000，从中抠出设备的
// 12 位十六进制地址。
std::string address_from_instance_id(const std::wstring& id) {
    size_t us = id.find_last_of(L'_');
    if (us == std::wstring::npos) return {};
    size_t amp = id.find_last_of(L'&', us);
    if (amp == std::wstring::npos || amp + 1 >= us) return {};

    std::wstring core = id.substr(amp + 1, us - amp - 1);
    if (core.size() != 12) return {};

    std::string out;
    out.reserve(12);
    for (wchar_t c : core) {
        if (!iswxdigit(c)) return {};
        out.push_back(static_cast<char>(towupper(c)));
    }
    return out;
}

}  // namespace

std::vector<BtBatteryDevice> scan_bluetooth_pnp_battery() {
    std::vector<BtBatteryDevice> out;

    // 先拿一次"真正连着"的地址集合，后面每个节点都拿它比对
    std::vector<std::string> connected;
    const bool link_known = query_connected_bluetooth(&connected);

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return out;

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        DEVPROPTYPE type = 0;
        BYTE value = 0;
        DWORD needed = 0;
        if (!SetupDiGetDevicePropertyW(set, &data, &kBluetoothBattery, &type,
                                       &value, sizeof(value), &needed, 0)) {
            continue;
        }
        if (type != DEVPROP_TYPE_BYTE) continue;

        // 节点在不在（PRESENT）不代表设备开着，必须查链路状态
        wchar_t instance[512] = {0};
        std::string addr;
        if (SetupDiGetDeviceInstanceIdW(set, &data, instance, 512, nullptr)) {
            addr = address_from_instance_id(instance);
        }
        if (!addr.empty() && link_known) {
            if (std::find(connected.begin(), connected.end(), addr) == connected.end()) {
                continue;   // 设备已关机 / 断开，这条是旧数据
            }
        }

        wchar_t name[512] = {0};
        DWORD reg_type = 0;
        SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_FRIENDLYNAME, &reg_type,
                                          reinterpret_cast<PBYTE>(name), sizeof(name), nullptr);
        if (!name[0]) {
            SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_DEVICEDESC, &reg_type,
                                              reinterpret_cast<PBYTE>(name), sizeof(name), nullptr);
        }

        BtBatteryDevice d;
        d.name = narrow(name);
        d.address = addr;
        d.percent = static_cast<int>(value);
        if (d.percent >= 0 && d.percent <= 100) out.push_back(d);
    }

    SetupDiDestroyDeviceInfoList(set);
    return out;
}
