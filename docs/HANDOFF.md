# 交接说明（HANDOFF）

写于 2026-11-12 凌晨。给「下一个人」（包括未来的我自己）看的：现在到哪一步、下一步怎么走、哪些坑已经踩过。

---

## 一句话状态

三个设备里通了两个：**鼠标 G903 ✅、耳机 iKF ✅、键盘 VK75 🟡**（只差最后一次抓包）。

---

## 已验证的三条通路

| 设备 | 通路 | 关键点 |
|---|---|---|
| Logitech G903 | HID++ 2.0 | 长报文 `0x11`，特征 `0x1001` 在索引 4，电压**大端** mV |
| iKF V11 Pro+ | Windows 蓝牙 PnP 属性 | `{104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2`，BYTE 0–100 |
| VK75-Loki | 未打通 | 见下 |

细节见 `PROTOCOL-NOTES.md` 和 `IKF-PROTOCOL.md`。

---

## 键盘：只剩 USBPcap 这一条路

### 已经排除的方式（不要重复试）

| 方式 | 结论 |
|---|---|
| 监听 9 个 HID 集合的 input report | 只有心跳和设置应答 |
| 驱动启动瞬间抓 | 同样只有心跳 |
| 切页面 / 改灯光 / 改按键时抓 | 只有 `10 1C xx` 这类按键设置应答 |
| `hid_get_input_report` 扫 0x00–0x10 | 全部失败 |
| `hid_get_feature_report` 扫 0x00–0x10 | 全部失败（`feature` 长度 = 0） |
| 注册表 `HKCU\Software\Valkyrie Driver` | 只有 LCID / theme_type / DeviceType=104 |
| `%LOCALAPPDATA%\Valkyrie Driver Files\db\*.db` | SQLite，只有灯光/触发/去抖，无 battery |

驱动里确实有电量控件（`BatteryCtrl` / `SetBatteryInfo` / `icon_batteryvalue.png` / `show_battery`），
功能存在，但数据走的是用户态观测不到的那一层。

### 下一步

1. 安装 **Wireshark**，安装时勾选 **USBPcap**（会装一个 USB 抓包驱动，可能需重启）
2. 打开 Wireshark，选 `USBPcap1`（或对应根集线器）
3. 开始抓包 → 启动 Valkyrie 驱动 → 等电量图标出现 → 停止
4. 过滤 `usb.idVendor == 0x05ac`（或看 `usb.device_address`）
5. 找 `URB_CONTROL out` / `URB_INTERRUPT out` 里发往 dongle 的短报文，以及对应的 `in` 应答

**判读要点**：已知设备上报的帧都是 32 字节、末字节 = 前面求和取低 8 位，
所以驱动写入的帧很可能也是这个格式，只是我们没见过。

抓到后把命令填进 `src/providers.cpp` 的键盘分支即可，UI 不用改。

---

## 环境坑（这条最重要，能省半天）

1. **CMake 在 DSH 沙箱里会卡死**——它用管道收集子进程输出，被沙箱挡住。
   **构建一律用 `build.ps1`**（直接调 `cl.exe`）。`CMakeLists.txt` 只给 CLion 用。
2. **`Start-Process` 会阻塞不返回**。要后台跑东西就交给工具的后台任务，用 `&` 调用。
3. **GUI 程序无法从沙箱启动**：Valkyrie 驱动用 `&` 启动会秒退、用 `Start-Process` 会挂住。
   这类程序必须由用户手动双击。
4. **工作区权限**：`2.4G-battery-viewer` 内外两层目录曾被 Windows 文件权限挡住，
   已用 ACL 修复脚本处理过，备份和回滚命令在 `..\dsh-acl-recovery\`。
5. **沙箱里 WMI/CIM 不可用**：`Get-PnpDevice`、`Get-CimInstance` 都会拒绝访问。
   枚举设备请用注册表或 SetupAPI。
6. **沙箱里 PowerShell/curl 的 TLS 拿不到凭证**（`SEC_E_NO_CREDENTIALS`），
   但 **git 自带的 OpenSSL 能联网**——下载依赖用 `git clone`。

---

## 工具

| 工具 | 用途 | 示例 |
|---|---|---|
| `probe.exe` | 枚举 HID 集合、打印描述符、被动读 | `probe.exe 5` |
| `rawprobe.exe` | 发任意报文、收应答、监听、读写 feature | `rawprobe.exe --vid 046D --up FF00 --usage 0002 --send "11 01 04 00 …" --read 700` |
| `hidpp_probe.exe` | 罗技特征表枚举 | `hidpp_probe.exe 046D` |
| `ikf_probe.exe` | iKF 全集合同时写/听 | `ikf_probe.exe` |
| `listen_all.exe` | 某 VID 下所有集合同时监听 | `listen_all.exe 05AC 300` |
| `report_probe.exe` | 扫 input/feature report | `report_probe.exe 05AC` |
| `pnp_probe.exe` | dump PnP 设备节点全部属性 | `pnp_probe.exe ikf` |
| `audio_probe.exe` | dump 音频端点属性 | `audio_probe.exe` |
| `bletest.exe` | 蓝牙诊断（BLE / 电量 / 属性 dump） | `bletest.exe ikf` |
| `tools\capture.ps1` | 窗口截图 | `capture.ps1 -Title "外设电量" -Out out\shot.png` |

构建：`.\build.ps1`（全部）或 `.\build.ps1 -Target app`。

---

## 代码地图

```
src/
  app_main.cpp        Win32 + D3D11 + ImGui 界面，后台线程每 5s 轮询
  providers.cpp       设备枚举 + 三条通路的调度（改这里接入新设备）
  ble_battery.cpp     BLE GATT 0x180F/0x2A19（C++/WinRT）
  bt_pnp_battery.cpp  Windows 蓝牙电量（SetupAPI 读 devnode 属性）
  device.h            DeviceInfo / BatteryStatus / 电压换算曲线
```

**改设备支持只需要动 `providers.cpp`**：构造 `DeviceInfo`，填 `status/percent/model`，UI 自动渲染。

---

## 踩过的逻辑坑

`poll_devices()` 里曾把升级逻辑写在 `out.push_back(...)` 之后——`push_back` 是拷贝，
改的是局部变量，蓝牙结果从未生效。**所有修改必须在 push_back 之前完成**（已修）。

---

## 提交历史

```
4899a94  新增键盘协议探测工具与 Valkyrie 驱动逆向记录
ca88a5d  耳机电量打通：读 Windows 蓝牙 PnP 设备属性
3572417  验证三条通路：iKF 耳机不向 PC 上报电量
2af9a53  新增蓝牙 LE GATT 电量通路，并逆向 iKF 官方 HID 协议
734d6f4  首个可用版本：G903 电量读取 + 三设备界面
d00a611  first commit
```
