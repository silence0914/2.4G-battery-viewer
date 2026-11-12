#include "bt_link.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bluetoothapis.h>

#include <cstdio>

#pragma comment(lib, "bthprops.lib")

namespace {

void append_address(std::vector<std::string>* out, const BLUETOOTH_ADDRESS& a) {
    char buf[16];
    // Address.rgBytes[5] 是地址的高字节，依次往下拼才是书写顺序
    std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
                  a.rgBytes[5], a.rgBytes[4], a.rgBytes[3],
                  a.rgBytes[2], a.rgBytes[1], a.rgBytes[0]);
    out->emplace_back(buf);
}

}  // namespace

bool query_connected_bluetooth(std::vector<std::string>* out) {
    out->clear();

    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnConnected = TRUE;
    search.fReturnUnknown = TRUE;
    search.fIssueInquiry = FALSE;   // 不做搜索，只查本机缓存的配对记录
    search.cTimeoutMultiplier = 1;
    search.hRadio = nullptr;        // 全部本地蓝牙无线电

    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);

    HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&search, &info);
    if (!find) return false;        // 查询失败：交给调用方决定怎么办

    do {
        if (info.fConnected) append_address(out, info.Address);
        info.dwSize = sizeof(info);
    } while (BluetoothFindNextDevice(find, &info));

    BluetoothFindDeviceClose(find);
    return true;
}
