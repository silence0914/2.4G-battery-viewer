// ---------------------------------------------------------------------------
// bletest -- 蓝牙诊断
//
//  1. 列出所有 BLE 设备
//  2. 走一遍 GATT 电量扫描
//  3. Windows 自己记录的 System.Devices.BatteryLife
//  4. dump 指定名字的蓝牙设备的全部属性（找电量到底存在哪个 key）
//
// 用法: bletest [名字关键字]
// ---------------------------------------------------------------------------

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include "ble_battery.h"
#include "bt_pnp_battery.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;

namespace {

std::string narrow(std::wstring const& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

const char* prop_type_name(PropertyType t) {
    switch (t) {
        case PropertyType::UInt8: return "UInt8";
        case PropertyType::Int16: return "Int16";
        case PropertyType::UInt16: return "UInt16";
        case PropertyType::Int32: return "Int32";
        case PropertyType::UInt32: return "UInt32";
        case PropertyType::Int64: return "Int64";
        case PropertyType::UInt64: return "UInt64";
        case PropertyType::String: return "String";
        case PropertyType::Boolean: return "Boolean";
        case PropertyType::StringArray: return "StringArray";
        case PropertyType::Guid: return "Guid";
        default: return "other";
    }
}

void print_prop(std::string const& key, IInspectable const& value) {
    auto pv = value.try_as<IPropertyValue>();
    if (!pv) {
        std::printf("    %-52s <non-property>\n", key.c_str());
        return;
    }
    switch (pv.Type()) {
        case PropertyType::UInt8:
            std::printf("    %-52s UInt8  = %u\n", key.c_str(), pv.GetUInt8());
            break;
        case PropertyType::UInt16:
            std::printf("    %-52s UInt16 = %u\n", key.c_str(), pv.GetUInt16());
            break;
        case PropertyType::Int32:
            std::printf("    %-52s Int32  = %d\n", key.c_str(), pv.GetInt32());
            break;
        case PropertyType::UInt32:
            std::printf("    %-52s UInt32 = %u\n", key.c_str(), pv.GetUInt32());
            break;
        case PropertyType::UInt64:
            std::printf("    %-52s UInt64 = %llu\n", key.c_str(), (unsigned long long)pv.GetUInt64());
            break;
        case PropertyType::Boolean:
            std::printf("    %-52s Bool   = %s\n", key.c_str(), pv.GetBoolean() ? "true" : "false");
            break;
        case PropertyType::String:
            std::printf("    %-52s String = %s\n", key.c_str(),
                        narrow(std::wstring(pv.GetString().c_str())).c_str());
            break;
        default:
            std::printf("    %-52s <%s>\n", key.c_str(), prop_type_name(pv.Type()));
            break;
    }
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::string filter = (argc > 1) ? argv[1] : "ikf";

    std::printf("=== all BLE devices ===\n");
    auto all = scan_ble_devices();
    if (all.empty()) {
        std::printf("  (none)\n");
    } else {
        for (const auto& d : all) {
            std::printf("  %-32s %-18s battery=%s\n", d.name.c_str(), d.address.c_str(),
                        d.has_battery ? "yes" : "no");
        }
    }

    std::printf("\n=== GATT battery scan (0x180F / 0x2A19) ===\n");
    auto list = scan_ble_batteries(true);
    std::printf("  found %zu\n", list.size());
    for (const auto& b : list) {
        std::printf("  %-32s %-18s %d%%\n", b.name.c_str(), b.address.c_str(), b.percent);
    }

    std::printf("\n=== System.Devices.BatteryLife ===\n");
    for (const auto& w : scan_windows_battery()) {
        std::printf("  %-32s %s\n", w.name.c_str(), w.percent >= 0 ? std::to_string(w.percent).c_str() : "n/a");
    }

    std::printf("\n=== bluetooth PnP battery ({104EA319...},2) ===\n");
    auto pnp = scan_bluetooth_pnp_battery();
    if (pnp.empty()) {
        std::printf("  (none)\n");
    } else {
        for (const auto& b : pnp) {
            std::printf("  name='%s'  %d%%\n", b.name.c_str(), b.percent);
        }
    }

    // ---- 全属性 dump ----
    std::printf("\n=== property dump (filter='%s') ===\n", filter.c_str());
    try {
        init_apartment(apartment_type::multi_threaded);
        std::string lower_filter = filter;
        for (auto& c : lower_filter) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

        auto dump_for = [&](DeviceInformationKind kind, hstring const& selector, const char* label) {
            auto infos = DeviceInformation::FindAllAsync(selector, {}, kind).get();
            for (DeviceInformation const& di : infos) {
                std::string name = narrow(std::wstring(di.Name().c_str()));
                std::string low = name;
                for (auto& c : low) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                if (low.find(lower_filter) == std::string::npos) continue;

                std::printf("\n-- [%s] %s\n   id=%s\n", label, name.c_str(),
                            narrow(std::wstring(di.Id().c_str())).c_str());
                auto props = di.Properties();
                int n = 0;
                for (auto const& kv : props) {
                    print_prop(narrow(std::wstring(kv.Key().c_str())), kv.Value());
                    ++n;
                }
                std::printf("   (%d properties)\n", n);
            }
        };

        dump_for(DeviceInformationKind::AssociationEndpoint,
                 BluetoothDevice::GetDeviceSelectorFromPairingState(true), "AEP");
        dump_for(DeviceInformationKind::Device, BluetoothDevice::GetDeviceSelector(), "Device");
    } catch (std::exception const& e) {
        std::printf("  dump failed: %s\n", e.what());
    } catch (...) {
        std::printf("  dump failed: unknown\n");
    }

    return 0;
}
