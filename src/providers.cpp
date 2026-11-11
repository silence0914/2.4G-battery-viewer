#include "providers.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "hidapi.h"
#include "ble_battery.h"

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
namespace {

std::string narrow(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return s;
}

// 判断某个 VID 是否在线，顺便取一个产品名
bool vid_online(unsigned short vid, std::string* product) {
    hid_device_info* devs = hid_enumerate(vid, 0x0);
    bool found = false;
    for (hid_device_info* d = devs; d; d = d->next) {
        if (d->vendor_id != vid) continue;
        found = true;
        if (product && product->empty() && d->product_string) *product = narrow(d->product_string);
    }
    hid_free_enumeration(devs);
    return found;
}

// ---------------------------------------------------------------------------
// HID++ 2.0 链路
//   请求 11 | dev | featIdx | (fn<<4)|swid | params[16]
//   应答 11 | dev | featIdx | (fn<<4)|swid | ret[16]
// ---------------------------------------------------------------------------
struct HidppLink {
    hid_device* h = nullptr;
    unsigned char dev = 1;
    int timeout_ms = 700;

    bool call(unsigned char feat, unsigned char fn,
              const unsigned char* params, int nparams,
              unsigned char* ret, int& nret) {
        unsigned char req[20] = {0};
        req[0] = 0x11;
        req[1] = dev;
        req[2] = feat;
        req[3] = static_cast<unsigned char>(fn << 4);
        for (int i = 0; i < nparams && i < 16; ++i) req[4 + i] = params[i];

        if (hid_write(h, req, sizeof(req)) < 0) return false;

        unsigned char buf[64];
        ULONGLONG t0 = GetTickCount64();
        while (GetTickCount64() - t0 < static_cast<ULONGLONG>(timeout_ms)) {
            int r = hid_read_timeout(h, buf, sizeof(buf), 40);
            if (r < 7) continue;
            if (buf[1] != dev) continue;
            if (buf[2] == 0xFF && buf[3] == 0x8F) return false;  // HID++ error
            if (buf[2] != feat) continue;
            if ((buf[3] >> 4) != fn) continue;
            nret = std::min(r - 4, 16);
            std::memcpy(ret, buf + 4, static_cast<size_t>(nret));
            return true;
        }
        return false;
    }

    bool get_feature(unsigned short id, unsigned char& idx) {
        unsigned char p[2] = {static_cast<unsigned char>(id >> 8),
                              static_cast<unsigned char>(id & 0xFF)};
        unsigned char ret[16] = {0};
        int n = 0;
        if (!call(0x00, 0x00, p, 2, ret, n)) return false;
        idx = ret[0];
        return idx != 0;
    }

    std::string device_name() {
        unsigned char nameIdx = 0;
        if (!get_feature(0x0005, nameIdx)) return std::string();

        unsigned char ret[16] = {0};
        int n = 0;
        if (!call(nameIdx, 0x00, nullptr, 0, ret, n)) return std::string();
        int len = ret[0];
        if (len <= 0 || len > 256) return std::string();

        std::string s;
        s.reserve(static_cast<size_t>(len));
        for (int off = 0; off < len; off += 16) {
            unsigned char pi[1] = {static_cast<unsigned char>(off)};
            unsigned char chunk[16] = {0};
            int cn = 0;
            if (!call(nameIdx, 0x01, pi, 1, chunk, cn)) break;
            int take = std::min(16, len - off);
            for (int i = 0; i < take && i < cn; ++i) s.push_back(static_cast<char>(chunk[i]));
        }
        return s;
    }

    // 0x1001 BATTERY_VOLTAGE : fn0 -> [mv_hi, mv_lo, flags]
    bool battery_voltage(int& mv, bool& charging, bool& critical) {
        unsigned char battIdx = 0;
        if (!get_feature(0x1001, battIdx)) return false;

        unsigned char ret[16] = {0};
        int n = 0;
        if (!call(battIdx, 0x00, nullptr, 0, ret, n)) return false;
        if (n < 2) return false;

        mv = (ret[0] << 8) | ret[1];   // 大端
        unsigned char flags = (n > 2) ? ret[2] : 0;
        critical = (flags & 0x01) != 0;
        charging = (flags & 0x02) != 0;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Logitech
// ---------------------------------------------------------------------------
bool poll_logitech(DeviceInfo& di) {
    hid_device_info* devs = hid_enumerate(0x046D, 0x0);
    hid_device_info* ch = nullptr;
    for (hid_device_info* d = devs; d; d = d->next) {
        if (d->usage_page == 0xFF00 && d->usage == 0x0002) { ch = d; break; }
    }
    if (!ch) { hid_free_enumeration(devs); return false; }

    hid_device* h = hid_open_path(ch->path);
    hid_free_enumeration(devs);
    if (!h) {
        di.status = BatteryStatus::Error;
        di.model = "Logitech 接收器";
        di.note = "无法打开 HID++ 通道（可能被其他程序独占）";
        return true;
    }

    HidppLink link;
    link.h = h;

    // 找哪个设备槽有意应答
    bool found = false;
    for (unsigned char slot = 1; slot <= 6; ++slot) {
        link.dev = slot;
        link.timeout_ms = (slot == 1) ? 500 : 200;
        unsigned char ret[16] = {0};
        int n = 0;
        unsigned char p[2] = {0x00, 0x01};
        if (link.call(0x00, 0x00, p, 2, ret, n)) { found = true; break; }
    }
    if (!found) {
        hid_close(h);
        di.status = BatteryStatus::Error;
        di.model = "Logitech 接收器";
        di.note = "接收器在，但鼠标无应答（可能已关机或休眠）";
        return true;
    }

    link.timeout_ms = 700;

    std::string name = link.device_name();
    di.model = name.empty() ? "Logitech 无线设备" : name;
    di.transport = "2.4G · Lightspeed 接收器";

    // 名字里带 Keyboard 就当键盘，否则当鼠标
    if (lower(name).find("keyboard") != std::string::npos) {
        di.kind = DeviceKind::Keyboard;
        di.kind_label = "键盘";
    }

    int mv = -1;
    bool charging = false, critical = false;
    if (link.battery_voltage(mv, charging, critical)) {
        di.millivolts = mv;
        di.percent = voltage_to_percent(mv);
        di.charging = charging;
        di.status = BatteryStatus::Ok;
        di.note = critical ? "电量危急" : "";
    } else {
        di.status = BatteryStatus::Unsupported;
        di.note = "设备无 BATTERY_VOLTAGE 特征（0x1001）";
    }

    // 顺带探测一下有没有 0x1000 / 0x1004，写进备注便于以后排查
    unsigned char tmp = 0;
    const char* extras[] = {nullptr, nullptr};
    int nextra = 0;
    if (link.get_feature(0x1000, tmp)) extras[nextra++] = "有特征 0x1000";
    if (link.get_feature(0x1004, tmp)) extras[nextra++] = "有特征 0x1004";
    for (int i = 0; i < nextra; ++i) {
        if (!di.note.empty()) di.note += " · ";
        di.note += extras[i];
    }

    hid_close(h);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 电压 -> 百分比（单节锂电静止电压的粗略曲线）
// ---------------------------------------------------------------------------
int voltage_to_percent(int mv) {
    struct Point { int mv; int pct; };
    static const Point curve[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3950, 75}, {3900, 70},
        {3850, 63},  {3800, 55}, {3750, 45}, {3700, 35}, {3650, 25},
        {3600, 15},  {3500, 6},  {3400, 2},  {3300, 0},
    };
    const int n = static_cast<int>(sizeof(curve) / sizeof(curve[0]));

    if (mv >= curve[0].mv) return 100;
    if (mv <= curve[n - 1].mv) return 0;

    for (int i = 0; i + 1 < n; ++i) {
        if (mv <= curve[i].mv && mv >= curve[i + 1].mv) {
            int hi = curve[i].mv, lo = curve[i + 1].mv;
            int hp = curve[i].pct, lp = curve[i + 1].pct;
            double t = static_cast<double>(mv - lo) / static_cast<double>(hi - lo);
            return static_cast<int>(lp + t * (hp - lp) + 0.5);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 主入口
// ---------------------------------------------------------------------------
std::vector<DeviceInfo> poll_devices() {
    hid_init();

    std::vector<DeviceInfo> out;

    // --- 鼠标：罗技 ---
    DeviceInfo mouse;
    mouse.kind = DeviceKind::Mouse;
    mouse.kind_label = "鼠标";
    mouse.transport = "2.4G · Lightspeed 接收器";
    if (!poll_logitech(mouse)) {
        mouse.status = BatteryStatus::Absent;
        mouse.model = "未检测到 Logitech 设备";
        mouse.transport = "插好接收器或换到 USB 口再试";
        mouse.note.clear();
    }
    out.push_back(mouse);

    // --- 键盘：Valkyrie（伪装成 Apple VID）---
    DeviceInfo kb;
    kb.kind = DeviceKind::Keyboard;
    kb.kind_label = "键盘";
    kb.transport = "2.4G 接收器";
    {
        std::string product;
        if (vid_online(0x05AC, &product)) {
            kb.model = product.empty() ? "Valkyrie VK75-Loki" : product;
            kb.status = BatteryStatus::Unsupported;
            kb.note = "厂商协议未解，电量暂不可读";
        } else {
            kb.status = BatteryStatus::Absent;
            kb.model = "未检测到 Valkyrie 键盘";
            kb.transport = "切到 2.4G 模式或插好接收器";
        }
    }
    out.push_back(kb);

    // --- 耳机：iKF（Jieli 方案）---
    DeviceInfo hs;
    hs.kind = DeviceKind::Headset;
    hs.kind_label = "耳机";
    hs.transport = "USB / 2.4G dongle";
    {
        std::string product;
        if (vid_online(0x38B1, &product)) {
            hs.model = "iKF V11 Pro+";
            hs.status = BatteryStatus::Unsupported;
            hs.note = "2.4G 命令无应答 · 蓝牙为经典音频，无 GATT 电量";
        } else {
            hs.status = BatteryStatus::Absent;
            hs.model = "未检测到 iKF 耳机";
            hs.transport = "插好 USB 或 dongle";
        }
    }
    out.push_back(hs);

    // --- 蓝牙 LE：标准 GATT Battery Service（0x180F / 0x2A19）---
    // 走这条路不需要任何厂商协议，只要设备在蓝牙模式下实现了标准电池服务。
    // 2.4G / USB 有线状态下设备不会出现在这里，所以扫描不到是正常的。
    std::vector<BleDeviceBattery> ble = scan_ble_batteries();

    auto find_ble = [&ble](std::initializer_list<const char*> keys) -> const BleDeviceBattery* {
        for (const BleDeviceBattery& b : ble) {
            std::string n = lower(b.name);
            for (const char* k : keys) {
                if (n.find(k) != std::string::npos) return &b;
            }
        }
        return nullptr;
    };

    auto upgrade = [](DeviceInfo& di, const BleDeviceBattery* b) {
        if (!b || di.status == BatteryStatus::Ok) return;
        di.status = BatteryStatus::Ok;
        di.percent = b->percent;
        di.millivolts = -1;
        di.charging = false;
        if (di.model.rfind("未检测到", 0) == 0 || di.model.empty()) di.model = b->name;
        di.transport = "蓝牙 LE · GATT Battery";
        di.note = b->address;
    };

    upgrade(kb, find_ble({"vk 75", "vk75", "valkyrie"}));
    upgrade(hs, find_ble({"ikf"}));

    hid_exit();
    return out;
}
