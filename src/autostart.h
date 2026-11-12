#pragma once

#include <string>

// 开机自动启动：首选写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run；
// 如果被安全软件 / 组策略锁住，退回到「启动」文件夹里的快捷方式。
// 两条路都不需要管理员权限。
bool autostart_enabled();

// 打开 / 关闭开机启动。成功返回 true（无论走的哪条路）。
bool set_autostart(bool enable);

// 当前生效方式："Run"（注册表） / "Startup"（启动文件夹） / 空串（没开）
std::string autostart_backend();

// 最近一次 set_autostart(true) 的失败原因（UTF-8），成功时为空
const std::string& autostart_last_error();

// 当前进程的完整路径（UTF-8）
std::string current_exe_path();

// 注册表里记录的启动命令（UTF-8），没设置时返回空串
std::string autostart_command();
