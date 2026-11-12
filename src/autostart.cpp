// ---------------------------------------------------------------------------
// 开机自动启动
//
// 主路径：写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 的 BatteryViewer 值。
// 这是最标准的做法，普通用户权限即可，不弹 UAC。
//
// 备用路径：部分机器上安全软件 / 组策略会把 Run 项锁成只读（写就报
// ERROR_ACCESS_DENIED = 5）。这时退回到「启动」文件夹里放一个快捷方式：
//   %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\外设电量.lnk
// 效果完全一样，而且不需要注册表写权限。
// ---------------------------------------------------------------------------

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>

#include "autostart.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValueName = L"BatteryViewer";
const wchar_t* kShortcutName = L"外设电量.lnk";

std::string g_last_error;
std::string g_backend;

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

// %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\外设电量.lnk
std::wstring shortcut_path() {
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_CREATE, nullptr, &dir)) || !dir) {
        if (dir) CoTaskMemFree(dir);
        return {};
    }
    std::wstring p(dir);
    CoTaskMemFree(dir);
    if (p.empty()) return {};
    if (p.back() != L'\\') p.push_back(L'\\');
    p += kShortcutName;
    return p;
}

bool file_exists(const std::wstring& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// 在「启动」文件夹里创建/删除快捷方式
bool create_shortcut(const std::wstring& target) {
    std::wstring lnk = shortcut_path();
    if (lnk.empty() || target.empty()) return false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool need_uninit = SUCCEEDED(hr);
    // RPC_E_CHANGED_MODE：别的线程/模块已经用 MTA 初始化过，继续用即可

    IShellLinkW* link = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        link->SetPath(target.c_str());

        // 工作目录取 exe 所在目录
        std::wstring workdir = target;
        size_t slash = workdir.find_last_of(L'\\');
        if (slash != std::wstring::npos) workdir.resize(slash);
        link->SetWorkingDirectory(workdir.c_str());
        link->SetDescription(L"显示鼠标与耳机电量");

        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            ok = SUCCEEDED(file->Save(lnk.c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }

    if (need_uninit) CoUninitialize();
    return ok;
}

void delete_shortcut() {
    std::wstring lnk = shortcut_path();
    if (!lnk.empty() && file_exists(lnk)) DeleteFileW(lnk.c_str());
}

// 返回 Run 键里的命令，没有则空串
std::wstring run_value() {
    wchar_t buf[2048] = {0};
    DWORD bytes = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                     nullptr, buf, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buf;
}

// 写入 Run 键，成功返回 ERROR_SUCCESS
LONG write_run_value(const std::wstring& cmd) {
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                             KEY_SET_VALUE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return r;

    r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r;
}

LONG erase_run_value() {
    HKEY key = nullptr;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (r != ERROR_SUCCESS) return ERROR_SUCCESS;   // 键都没有，本来就等于关着
    r = RegDeleteValueW(key, kValueName);
    RegCloseKey(key);
    return (r == ERROR_FILE_NOT_FOUND) ? ERROR_SUCCESS : r;
}

}  // namespace

std::string current_exe_path() {
    wchar_t buf[MAX_PATH * 4] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(MAX_PATH * 4));
    if (n == 0 || n >= MAX_PATH * 4) return {};
    return to_utf8(std::wstring(buf, n));
}

std::string autostart_command() {
    return to_utf8(run_value());
}

std::string autostart_backend() {
    std::wstring run = run_value();
    if (!run.empty()) return "Run";
    if (file_exists(shortcut_path())) return "Startup";
    return {};
}

bool autostart_enabled() {
    return !autostart_backend().empty();
}

const std::string& autostart_last_error() {
    return g_last_error;
}

bool set_autostart(bool enable) {
    g_last_error.clear();
    g_backend.clear();

    if (!enable) {
        LONG r1 = erase_run_value();
        delete_shortcut();
        if (r1 != ERROR_SUCCESS) {
            g_last_error = "写入注册表失败（错误码 " + std::to_string(r1) + "）";
            return false;
        }
        g_backend.clear();
        return true;
    }

    std::string exe = current_exe_path();
    if (exe.empty()) {
        g_last_error = "取不到程序自己的路径";
        return false;
    }
    std::wstring wexe = to_wide(exe);

    // 1) 首选：注册表 Run 项
    LONG r = write_run_value(L"\"" + wexe + L"\"");
    if (r == ERROR_SUCCESS) {
        delete_shortcut();   // 避免两处都启动、开两次
        g_backend = "Run";
        return true;
    }

    // 2) 退路：「启动」文件夹快捷方式
    if (create_shortcut(wexe)) {
        g_backend = "Startup";
        return true;
    }

    g_last_error = "注册表和启动文件夹都写入失败（错误码 " + std::to_string(r) + "）";
    return false;
}
