// ---------------------------------------------------------------------------
// 开机自动启动 —— HKCU 的 Run 项，普通用户权限即可写入
// ---------------------------------------------------------------------------

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "autostart.h"

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValueName = L"BatteryViewer";

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                   nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), need);
    return out;
}

}  // namespace

std::string current_exe_path() {
    wchar_t buf[MAX_PATH * 4] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(MAX_PATH * 4));
    if (n == 0 || n >= MAX_PATH * 4) return {};
    return to_utf8(std::wstring(buf, n));
}

std::string autostart_command() {
    wchar_t buf[2048] = {0};
    DWORD bytes = sizeof(buf);
    LONG r = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                          nullptr, buf, &bytes);
    if (r != ERROR_SUCCESS) return {};
    return to_utf8(buf);
}

bool autostart_enabled() {
    return !autostart_command().empty();
}

bool set_autostart(bool enable) {
    if (enable) {
        std::string exe = current_exe_path();
        if (exe.empty()) return false;

        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
            return false;
        }

        std::wstring cmd = L"\"" + to_wide(exe) + L"\"";
        LONG r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(cmd.c_str()),
                                static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return r == ERROR_SUCCESS;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return true;  // 键都不存在，等于已经关掉了
    }
    LONG r = RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}
