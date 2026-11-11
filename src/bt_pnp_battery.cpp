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
// ---------------------------------------------------------------------------

#include "bt_pnp_battery.h"

#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>

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

}  // namespace

std::vector<BtBatteryDevice> scan_bluetooth_pnp_battery() {
    std::vector<BtBatteryDevice> out;

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
        d.percent = static_cast<int>(value);
        if (d.percent >= 0 && d.percent <= 100) out.push_back(d);
    }

    SetupDiDestroyDeviceInfoList(set);
    return out;
}
