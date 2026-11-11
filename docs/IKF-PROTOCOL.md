# iKF 耳机 HID 协议（逆向自官方 Web 工具）

来源：`https://hub.ikeepfun.com` 的前端包 `assets/index-CjMBPDMB.js`（压缩混淆代码）。
本地副本在 `util/ikeepfun/`。

> 这些常量是从压缩代码里读出来的，不是官方文档。命令语义可信度高（代码里写死了
> 中文描述和参数），但**报文细节仍需实测确认**。

## 设备标识

```js
DEVICE_CONSTANTS = {
  VENDOR_ID: 14513,      // 0x38B1  ← iKF
  PRODUCT_ID: 13330,     // 0x3412  ← V11 Pro+
  USAGE_PAGE: 65280,     // 0xFF00
  USAGE: 1,
  REPORT_ID: { INPUT: 3, OUT: 2, FEATURE: 3 },
  HID_PACKET_SIZE: 64
}
```

注意：我们这台设备实测的厂商集合是 `0xFF02 / usage 0x0002`，不是 `0xFF00 / 0x0001`；
也没有 report id 3 的 feature 报告。可能是不同批次/不同连接方式的差异。

## 报文封装

真正的组包函数是 `packCommand`（不是 `buildAppProtocol`，后者是另一套遗留格式）：

```js
BASE_PROTOCOL = {
  IKF_PROTOCOL_ID: 0x55,
  PACKET_HEADER_SIZE: 4,
  APP_DATA_MAX_PER_PACKET: 58,
  FLAGS: { LAST_PACKET: 0x80, NEW_SESSION: 0x40, ACK_REQUIRED: 0x20 }
}

packCommand(payload):
  frame = [ 0x55, seq | (最后一包 ? 0x80 : 0), 0x00,
            totalLen & 0xFF, totalLen >> 8,
            ...payload ]
  补 0x00 到 63 字节
```

发送时 report id 用 `REPORT_ID.OUT = 2`，即 HID 写入缓冲区 = `[0x02, ...63 字节]`。

## 命令表（节选）

```js
SYSTEM_COMMANDS = {
  get_version:  { template: [13] },          // 0x0D
  get_pdid:     { template: [239, 1] },      // 0xEF 0x01
  read_prop:    { template: [250, {0}] },    // 0xFA + 属性号
  get_battery:  { template: [250, 12] },     // 0xFA 0x0C  ← 读电量
  find_device:  { template: [3, {0}] },
  factory_reset:{ template: [6] },
  cancel_shutdown: { template: [32, 255, 255] }
}
```

命令 ID 对照（代码里的中文描述）：

```
12  → 电量信息          13 → 固件版本        239 → PDID/MID
250 → 属性查询/设置      9  → ANC 状态        10 → 灯光状态
16  → LED 开关          41 → 提示音音量      106 → 振动强度
69  → 麦克风音量         97 → 麦克风静音      64 → EQ 设置
32  → 定时关机          254 → 确认(ACK)      15 → 音量调节
```

Dongle 专用指令：`10 → Dongle 连接状态`，`7 → Dongle 固件版本`。

## 应答解析

应答帧同样以 `0x55` 开头；另有 `0x5A`（Dongle）、`0xD6 0xBE`（Dongle RX）两种前缀。

电量（命令 12）的解析：

```js
case 12:
  battery = { left: a[0], right: a[1], case: a[2], charging: a[3] ?? 0 }
```

即 payload 去掉命令字节后：`[左, 右, 仓, 充电标志]`。

其它：`case 13 → 固件版本 = a[0..3].join(".")`；
`case 239 → pdid = a[0]|a[1]<<8, mid = a[2]|a[3]<<8`。

## 实测结果（本机，2026-11-11）

设备 `38B1:3412`，5 个 HID 集合：

| 集合 | usage | 可写 | 结果 |
|---|---|---|---|
| MI_04&Col02 | 0xFF02 / 0x0002 | ✅ report id 2 | 写入成功，**无任何应答** |
| MI_03 | 0xFF02 / 0x0002 | ✅ 无 report id | 写入成功，**无任何应答** |
| MI_04&Col01 | 0x000C / 0x0001 | ❌ | — |
| MI_04&Col03 | 0x000C / 0x0001 | ❌ | 唯一有 feature 报告(0xED, 15B)的集合 |
| MI_04&Col04 | 0x000B / 0x0005 | ❌ | — |

发送的帧（get_battery）：

```
02 55 80 00 02 00 FA 0C 00 ...    （64 字节，report id 2）
```

对全部 5 个集合同步监听 1.5 秒，**没有任何输入报告**。
`hid_get_feature_report` 扫描 report id 0x00–0x10 全部失败（这些集合 feature 长度为 0）。

## 未解的问题

1. 设备接受了写但没有回应 —— 是缺一个「建立会话」的握手（`FLAGS.NEW_SESSION = 0x40`），
   还是当前是 USB 有线模式、命令通道本就不工作？
2. 官方工具轮询的是 **feature report 3**，这台设备没有这个报告 —— 说明官方的
   `DEVICE_CONSTANTS` 可能对应的是另一个连接形态（比如 2.4G dongle）。
3. 需要实测：用官方网页工具连这台耳机，它能不能显示出电量。
   - 能 → 我们的帧或顺序还差一点，继续对比
   - 不能 → 这个型号/这个连接方式本来就不支持 HID 查电量，走蓝牙 GATT 或找厂商

## 下一步

- 让用户在 Edge/Chrome 打开 `https://hub.ikeepfun.com/#/device`，插上耳机试一次
- 如果手上有 2.4G dongle，换成 dongle 连接再抓一轮
- 找厂商要「USB HID 通信协议文档」（已给话术）

---

## 追加验证（当天深夜，设备切到蓝牙后）

三条通路全部试过，结论明确：

### 1. 官方网页工具（2.4G dongle 模式）

报错：

```
初始化失败：设备通信通道被占用。
请在桌面右下角的后台托盘找到对应的桌面驱动程序，右键选择"退出"后再刷新本页面重试。
```

代码里这个错误的触发条件是：

```js
const _ = await h("receiver.connection", v => v.connection !== null, "Dongle连接状态");
if (!_) { if (config.require_feature_report) throw initializationError("channel-occupied"); }
```

即 **只要 Dongle 不回应就报"通道被占用"**，并不是真的检测到冲突。
本机确认：没有安装任何 iKF 桌面程序，也没有相关进程在跑。

### 2. 蓝牙 LE GATT

设备切到蓝牙、配对成功（注册表 `BTHPORT` 里有 `iKF-V11 Pro+`，MAC `55:14:CD:86:40:55`），
但 `BluetoothLEDevice` 全量枚举里**没有这台设备**，只有一台 `midea`（MAC `70:86:CE:F8:A6:D4`）。

→ 它是**经典蓝牙**（A2DP / HFP）配对，不广告 BLE，没有 GATT 电池服务。

### 3. Windows 系统属性

`System.Devices.BatteryLife` 查询结果：

```
Xbox Wireless Controller   battery=n/a
iKF-V11 Pro⁺               battery=n/a
```

→ Windows 自己也拿不到这台设备的电量。

### 结论（已修正）

**Windows 自己能拿到这台耳机的电量**，但不在蓝牙设备对象上，也不在音频端点上，
而是存在 **HFP 服务设备节点**的自定义属性里：

```
设备节点: BTHENUM\{0000111E-...}_VID&000105D6_PID&000A\..._5514CD864055_C00000000
属性键:   {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2
类型:     DEVPROP_TYPE_BYTE
实测值:   100
```

也就是说：Windows 的蓝牙音频栈在 HFP 上从耳机取到电量后，把它写成了这个
devnode 属性，设置页显示的就是它。读这个属性即可，不需要任何厂商协议。

`Windows.Devices.Enumeration` 的 `System.Devices.BatteryLife` 查不到，
因为它不映射到这个 devnode 属性——这是最初判断失误的原因。

程序里对应实现见 `src/bt_pnp_battery.cpp`。

### 这条通路的适用范围

- 任何 Windows 设置页能显示电量的蓝牙设备
- 经典蓝牙（A2DP / HFP）也适用，不要求 BLE
- 前提是设备已连接、且 Windows 已收到过一次电量上报

`0x111E` 是 Handsfree 服务的 UUID，`0x110B` 是 A2DP Sink、`0x110C` 是 AVRCP。
电量属性只出现在 HFP 节点上。


