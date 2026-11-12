// ---------------------------------------------------------------------------
// btlink_probe —— 用经典蓝牙 API 枚举本机记住的设备，打印它们的连接状态
//
// 目的：PnP 里的 BTHENUM 服务节点在耳机断电后**仍然存在**（Windows 会保留
// 已配对设备的节点），所以只看 SetupAPI 的 PRESENT 标志无法判断设备是否
// 真的连着。BLUETOOTH_DEVICE_INFO::fConnected 才是链路层的真实状态。
// ---------------------------------------------------------------------------

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bluetoothapis.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "bthprops.lib")

namespace {

std::string narrow(const wchar_t* w) {
    if (!w || !w[0]) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string addr_string(BLUETOOTH_ADDRESS a) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  a.rgBytes[5], a.rgBytes[4], a.rgBytes[3],
                  a.rgBytes[2], a.rgBytes[1], a.rgBytes[0]);
    return buf;
}

}  // namespace

int main() {
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnConnected = TRUE;
    search.fReturnUnknown = TRUE;
    search.fIssueInquiry = FALSE;
    search.cTimeoutMultiplier = 1;
    search.hRadio = nullptr;

    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);

    HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&search, &info);
    if (!find) {
        std::printf("BluetoothFindFirstDevice failed, err=%lu\n", GetLastError());
        return 1;
    }

    std::printf("%-10s %-8s %-8s %-8s  %s\n", "ADDRESS", "CONNECT", "PAIRED", "REMEMBER", "NAME");
    int count = 0;
    do {
        std::printf("%-10s %-8s %-8s %-8s  %s\n",
                    addr_string(info.Address).c_str(),
                    info.fConnected ? "YES" : "no",
                    info.fAuthenticated ? "yes" : "no",
                    info.fRemembered ? "yes" : "no",
                    narrow(info.szName).c_str());
        ++count;
        info.dwSize = sizeof(info);
    } while (BluetoothFindNextDevice(find, &info));

    BluetoothFindDeviceClose(find);
    std::printf("\n%d device(s)\n", count);
    return 0;
}
