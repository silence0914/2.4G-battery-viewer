// ---------------------------------------------------------------------------
// 蓝牙 LE 电量读取
//
// 走标准 GATT：
//   Battery Service            0x180F
//   Battery Level characteristic 0x2A19  (UINT8, 0..100)
//
// 任何实现这两个 UUID 的蓝牙外设都能读，不需要厂商协议。
// ---------------------------------------------------------------------------

#include "ble_battery.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <mutex>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage::Streams;

namespace {

std::string narrow(std::wstring const& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string mac_of(uint64_t addr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>((addr >> 40) & 0xFF),
                  static_cast<unsigned>((addr >> 32) & 0xFF),
                  static_cast<unsigned>((addr >> 24) & 0xFF),
                  static_cast<unsigned>((addr >> 16) & 0xFF),
                  static_cast<unsigned>((addr >> 8) & 0xFF),
                  static_cast<unsigned>(addr & 0xFF));
    return buf;
}

void init_winrt_once() {
    static thread_local bool done = false;
    if (!done) {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        done = true;
    }
}

IAsyncAction scan_async(std::vector<BleDeviceBattery>* out) {
    hstring selector = GattDeviceService::GetDeviceSelectorFromUuid(GattServiceUuids::Battery());
    DeviceInformationCollection infos = co_await DeviceInformation::FindAllAsync(selector);

    for (DeviceInformation const& di : infos) {
        BleDeviceBattery item;
        item.name = narrow(std::wstring(di.Name().c_str()));

        bool ok = false;
        try {
            GattDeviceService service = co_await GattDeviceService::FromIdAsync(di.Id());
            if (service) {
                try {
                    BluetoothLEDevice dev = service.Device();
                    if (dev) item.address = mac_of(dev.BluetoothAddress());
                } catch (...) {}

                GattCharacteristicsResult chars = co_await service.GetCharacteristicsForUuidAsync(
                    GattCharacteristicUuids::BatteryLevel(), BluetoothCacheMode::Uncached);
                if (chars.Status() == GattCommunicationStatus::Success &&
                    chars.Characteristics().Size() > 0) {
                    GattCharacteristic ch = chars.Characteristics().GetAt(0);
                    GattReadResult rd = co_await ch.ReadValueAsync(BluetoothCacheMode::Uncached);
                    if (rd.Status() == GattCommunicationStatus::Success) {
                        DataReader reader = DataReader::FromBuffer(rd.Value());
                        if (reader.UnconsumedBufferLength() > 0) {
                            item.percent = static_cast<int>(reader.ReadByte());
                            ok = true;
                        }
                    }
                }
                try { service.Close(); } catch (...) {}
            }
        } catch (...) {
            ok = false;
        }

        if (ok && item.percent >= 0 && item.percent <= 100) out->push_back(item);
    }
}

}  // namespace

std::vector<BleDeviceBattery> scan_ble_batteries(bool force) {
    static std::mutex mtx;
    static std::vector<BleDeviceBattery> cache;
    static std::chrono::steady_clock::time_point last_scan{};
    static bool have_scanned = false;

    std::lock_guard<std::mutex> lock(mtx);

    auto now = std::chrono::steady_clock::now();
    if (!force && have_scanned && now - last_scan < std::chrono::seconds(30)) {
        return cache;
    }

    std::vector<BleDeviceBattery> result;
    try {
        init_winrt_once();
        scan_async(&result).get();
    } catch (...) {
        if (have_scanned) return cache;  // 扫描失败就沿用上次结果
        return {};
    }

    cache = result;
    last_scan = now;
    have_scanned = true;
    return cache;
}
