// ---------------------------------------------------------------------------
// pnp_probe -- 枚举 PnP 设备节点并 dump 全部属性
//
// 用法: pnp_probe <匹配串>   匹配串会同时比对实例 ID 和友好名（不区分大小写）
// 例:   pnp_probe 5514CD864055
//       pnp_probe ikf
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")

static std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(::towlower(c));
    return s;
}

static const char* type_name(DEVPROPTYPE t) {
    switch (t) {
        case DEVPROP_TYPE_STRING: return "STRING";
        case DEVPROP_TYPE_STRING_LIST: return "STR_LIST";
        case DEVPROP_TYPE_UINT32: return "UINT32";
        case DEVPROP_TYPE_UINT16: return "UINT16";
        case DEVPROP_TYPE_UINT64: return "UINT64";
        case DEVPROP_TYPE_BYTE: return "BYTE";
        case DEVPROP_TYPE_BOOLEAN: return "BOOL";
        case DEVPROP_TYPE_GUID: return "GUID";
        case DEVPROP_TYPE_FILETIME: return "FILETIME";
        default: return "other";
    }
}

static void dump_device(HDEVINFO set, SP_DEVINFO_DATA& data, const wchar_t* instance) {
    std::wprintf(L"\n-- %s\n", instance);

    DWORD count = 0;
    // 第一次调用故意返回 FALSE + ERROR_INSUFFICIENT_BUFFER，用于取数量
    SetupDiGetDevicePropertyKeys(set, &data, nullptr, 0, &count, 0);
    if (count == 0) {
        std::wprintf(L"   (no property keys)\n");
        return;
    }
    std::vector<DEVPROPKEY> keys(count);
    if (!SetupDiGetDevicePropertyKeys(set, &data, keys.data(), count, &count, 0)) {
        std::wprintf(L"   (keys failed, err=%lu)\n", GetLastError());
        return;
    }

    for (DWORD i = 0; i < count; ++i) {
        DEVPROPTYPE type = 0;
        BYTE buf[2048] = {0};
        DWORD needed = 0;
        if (!SetupDiGetDevicePropertyW(set, &data, &keys[i], &type, buf, sizeof(buf), &needed, 0)) {
            continue;
        }

        wchar_t guid[64] = {0};
        StringFromGUID2(keys[i].fmtid, guid, 64);

        std::wstring text;
        switch (type) {
            case DEVPROP_TYPE_STRING:
                text = reinterpret_cast<wchar_t*>(buf);
                break;
            case DEVPROP_TYPE_UINT32: {
                wchar_t t[32];
                swprintf_s(t, L"%u", *reinterpret_cast<DWORD*>(buf));
                text = t;
                break;
            }
            case DEVPROP_TYPE_UINT16: {
                wchar_t t[32];
                swprintf_s(t, L"%u", *reinterpret_cast<WORD*>(buf));
                text = t;
                break;
            }
            case DEVPROP_TYPE_UINT64: {
                wchar_t t[32];
                swprintf_s(t, L"%llu", *reinterpret_cast<unsigned long long*>(buf));
                text = t;
                break;
            }
            case DEVPROP_TYPE_BYTE: {
                wchar_t t[32];
                swprintf_s(t, L"%u", static_cast<unsigned>(buf[0]));
                text = t;
                break;
            }
            case DEVPROP_TYPE_BOOLEAN:
                text = buf[0] ? L"true" : L"false";
                break;
            default: {
                text = L"0x";
                wchar_t t[8];
                for (DWORD b = 0; b < needed && b < 16; ++b) {
                    swprintf_s(t, L"%02X", buf[b]);
                    text += t;
                }
                break;
            }
        }

        std::wprintf(L"   %s pid=%-3u %-9hs %s\n", guid, keys[i].pid,
                     type_name(type), text.c_str());
    }
    std::wprintf(L"   (%lu properties)\n", static_cast<unsigned long>(count));
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    std::wstring needle = L"ikf";
    if (argc > 1) {
        std::string a = argv[1];
        needle.assign(a.begin(), a.end());
    }
    needle = lower(needle);

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        std::printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return 1;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    int matched = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        wchar_t instance[512] = {0};
        if (!SetupDiGetDeviceInstanceIdW(set, &data, instance, 512, nullptr)) continue;

        wchar_t friendly[512] = {0};
        DWORD type = 0;
        SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_FRIENDLYNAME, &type,
                                          reinterpret_cast<PBYTE>(friendly),
                                          sizeof(friendly), nullptr);

        if (lower(instance).find(needle) == std::wstring::npos &&
            lower(friendly).find(needle) == std::wstring::npos) {
            continue;
        }

        ++matched;
        dump_device(set, data, instance);
    }

    SetupDiDestroyDeviceInfoList(set);
    std::printf("\nmatched %d device node(s)\n", matched);
    return 0;
}
