# 协议探测记录

日期：2026-11-11　机器：Win11 / AMD64

## 结论速览

| 设备 | 链路 | 状态 | 电量来源 |
|---|---|---|---|
| Logitech G903 | 2.4G Lightspeed 接收器 `046D:C539` | ✅ 已打通 | HID++ 2.0 特征 `0x1001` 索引 4 |
| Valkyrie VK75-Loki | 2.4G 接收器 `05AC:024F`（伪装 Apple VID） | 🟡 有应答，未定性 | 64 字节厂商通道，命令空间未解 |
| iKF V11 Pro⁺ | USB 直连 `38B1:3412`（Jieli 杰理方案） | ⛔ 未打通 | 未知 |

---

## 1. Logitech G903 —— 已确认

HID 集合（`VID_046D&PID_C539`，产品名 `USB Receiver`，设备索引 1 返回名为 `Logitech G903 Wi…`）：

| 接口 | usage page / usage | 用途 |
|---|---|---|
| MI_02&Col01 | 0xFF00 / 0x0001 | HID++ 短报文（0x10，6 字节） |
| MI_02&Col02 | 0xFF00 / 0x0002 | **HID++ 长报文（0x11，19 字节）—— 实际通信走这里** |
| MI_02&Col03 | 0xFF00 / 0x0004 | 0x20 / 0x21，32 字节 |
| MI_01&Col04 | 0xFFBC / 0x0088 | 1 字节厂商事件 |

**短报文通道写进去没有回应，必须用长报文（0x11）。**

报文格式：

```
请求  11 | dev | featIdx | (fn<<4)|swid | params[16]
应答  11 | dev | featIdx | (fn<<4)|swid | ret[16]
错误  11 | dev | 0xFF    | 0x8F        | errCode
```

设备索引：`1`（接收器本身是 `0xFF`）。

### 特征表（dev 索引 1，共 28 项）

```
[ 1] 0x0001  FEATURE_SET          [15] 0x8110
[ 2] 0x0003  DEVICE_FW_VERSION    [16] 0x1850
[ 3] 0x0005  DEVICE_NAME          [17] 0x00C2
[ 4] 0x1001  BATTERY_VOLTAGE      [18] 0x1801
[ 5] 0x1863                       [19] 0x1802
[ 6] 0x18A1                       [20] 0x1803
[ 7] 0x1E00                       [21] 0x1805
[ 8] 0x1E20                       [22] 0x1809
[ 9] 0x1EB0                       [23] 0x1890
[10] 0x2201                       [24] 0x1811
[11] 0x2230                       [25] 0x8111
[12] 0x2240                       [26] 0x8070
[13] 0x8060                       [27] 0x1300
[14] 0x8100                       [28] 0x1830
```

注意：这台 G903 **没有** `0x1000`（BATTERY_STATUS），也没有 `0x1004`（UNIFIED_BATTERY），
只有 `0x1001`（BATTERY_VOLTAGE）。

### 读电量

```
请求  11 01 04 00 00 00 ...        （featIdx=4, fn=0）
应答  11 01 04 00 0F 12 00 ...     -> 0x0F12 = 3858 mV, flags=0x00
```

- 电压按 **大端** 解释：`0x0F12` = 3858 mV（小端会是 4623 mV，超出单节锂电上限，可排除）
- `flags` bit0 = 电量危急，bit1 = 充电中
- 结果稳定，多次读取一致

### 其它可用特征

- `0x0005` DEVICE_NAME @idx 3：fn0 返回长度（0x29），fn1 带偏移取 16 字节分片，
  拼出 `Logitech G903 Wireless Gaming Mouse`
- `0x0001` FEATURE_SET @idx 1：fn0 取数量，fn1 取 featureId —— 用于枚举特征表

---

## 2. Valkyrie VK75-Loki —— 有应答，未定性

伪装成 `05AC:024F`（Apple 内部键盘的 VID/PID），产品名 `VK 75 Wireless Receiver`。
**注意：注册表里 `VID_1D57&PID_FA65/FA98` 是陈旧条目，设备并不在线。**

9 个 HID 集合：

| 接口 | usage page / usage | 说明 |
|---|---|---|
| MI_00 | 0x0001 / 0x0006 | 启动协议键盘报文，8 字节，**最后 1 字节是厂商字段**（page 0xFF usage 0x03） |
| MI_01 | 0x0001 / 0x0002 | 鼠标 |
| MI_02&Col01 | 0x000C / 0x0001 | 消费类控制 |
| MI_02&Col02 | 0x0001 / 0x0080 | 系统控制 |
| MI_02&Col03 | 0x0001 / 0x0006 | 键盘（report ID 3，15 字节位图） |
| MI_02&Col04 | 0x0001 / 0x000E | 系统多轴 |
| MI_02&Col05 | 0xFFFF / 0x0001 | **3 字节厂商输入报告（report ID 5）** |
| MI_03 | 0xFF60 / 0x0061 | **VIA 原始 HID，32 字节（无 report ID）** |
| MI_04 | 0xFF60 / 0x0061 | 64 字节厂商通道 |

### 已做的测试

- `MI_03`（32 字节，VIA 握手 `00 01 …`）：**无应答**
- `MI_04`（64 字节）：**有应答**
- `MI_02&Col05`（report ID 5）：**无主动上报**

### MI_04 的应答（64 字节，确定性、可复现）

发送 65 字节，首字节 `00`，第二字节为命令：

| 命令 | 应答前 4 字节 |
|---|---|
| `00 00 …` | `0F 01 FE 0E` |
| `00 01 …` | `0F 01 01 11` |
| `00 02 … ~ 00 0C …` | `0F 01 01 11`（与 01 相同） |

观察：
- `0F 01` 两字节像是固定包头
- 后两字节 `FE 0E` / `01 11` 稳定不漂移，不像是会随时间变化的电量百分比
- 若按小端当成 16 位：`0x0EFE` = 3838，`0x1101` = 4353 —— 数值恰好落在锂电池电压量级，
  但两个值同时存在说不通（一个 3.84 V 一个 4.35 V）

**未解：哪个字段才是电量，或者电量根本在 MI_00 的第 8 字节里。**

### 下一步可做的实验

1. 让用户按住键盘按键，同时监听 `MI_00`（8 字节）和 `MI_02&Col05`（3 字节），看是否随事件上报
2. 在键盘切到蓝牙模式后，用 BLE GATT 的 `0x180F/0x2A19` 读（如果有）
3. 装官方驱动后用 USBPcap 抓一次"电量显示"的报文，对照我们抓到的通道

---

## 3. iKF V11 Pro⁺ —— 未打通

`38B1:3412`，厂商字符串 `Jieli Technology`（杰理），产品名 `USB Composite Device`，
Windows 显示名 `iKF-V11 Pro⁺`，驱动 `oem76.inf / ikf_v11max`。

| 接口 | usage page / usage | 大小 | 说明 |
|---|---|---|---|
| MI_03 | 0xFF02 / 0x0002 | 65/65 | 厂商命令通道（64 字节 payload，无 report ID） |
| MI_04&Col01 | 0x000C / 0x0001 | 3 | 消费类控制 |
| MI_04&Col02 | 0xFF02 / 0x0002 | 65/65 | 厂商命令通道（report ID 2，64 字节） |
| MI_04&Col03 | 0x000C / 0x0001 | 32/32，feature 16 | **report 0xFD：1 + 30 字节厂商输入；feature 0xED：15 字节** |
| MI_04&Col04 | 0x000B / 0x0005 | 3/3 | 电话页面（耳机 HID 控制） |

- 所有通道**都没有主动上报**
- 两个 `0xFF02` 通道是典型的"写命令 / 读应答"结构
- `0xFD` 输入报告的 30 字节厂商数据 + `0xED` 特征报告最像状态/电量载体，但需要知道命令序列

**未解：Jieli 的私有命令集。** 需要官方软件抓包或协议文档。

---

## 工具

| 工具 | 用途 |
|---|---|
| `probe.exe` | 枚举所有 HID 集合、打印 caps + report descriptor，可被动读 N 秒 |
| `rawprobe.exe` | 按 VID/usage 挑选集合，发任意报文、收应答、被动监听 |
| `hidpp_probe.exe` | 罗技专用：枚举 HID++ 特征表并查询电量特征 |

`rawprobe` 用法：

```
rawprobe --vid 046D --up FF00 --usage 0002
    -> 列出匹配的集合

rawprobe --vid 046D --up FF00 --usage 0002 --send "11 01 04 00 00 ..." --read 700
    -> 发送并打印应答

rawprobe --vid 05AC --up FF60 --usage 0061 --index 0 --listen 5000
    -> 只监听，不发数据
```

## 构建

`build.ps1` 直接用 `cl.exe` 编译（CMake 在沙箱下会卡在编译器探测）。

```
.\build.ps1              # 全部
.\build.ps1 -Target hidpp
.\build.ps1 -Clean
```

产物在 `out\`。
