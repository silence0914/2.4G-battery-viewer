#pragma once

#include <string>

// 开机自动启动：写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run，
// 不需要管理员权限。
bool autostart_enabled();

// 打开 / 关闭开机启动。成功返回 true。
bool set_autostart(bool enable);

// 当前进程的完整路径（UTF-8）
std::string current_exe_path();

// 注册表里记录的启动命令（UTF-8），没设置时返回空串
std::string autostart_command();
