// ---------------------------------------------------------------------------
// audio_probe -- dump 所有音频端点的属性，找耳机电量存在哪个 property key
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>

#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>

#include <cstdio>
#include <cwchar>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

static void dump_store(IPropertyStore* store) {
    DWORD n = 0;
    if (FAILED(store->GetCount(&n))) return;

    for (DWORD i = 0; i < n; ++i) {
        PROPERTYKEY key{};
        if (FAILED(store->GetAt(i, &key))) continue;

        PROPVARIANT v;
        PropVariantInit(&v);
        if (FAILED(store->GetValue(key, &v))) continue;

        wchar_t text[512] = {0};
        if (v.vt == VT_LPWSTR && v.pwszVal) {
            wcsncpy_s(text, v.pwszVal, _TRUNCATE);
        } else {
            PropVariantToString(v, text, 512);
        }

        wchar_t guid[64] = {0};
        StringFromGUID2(key.fmtid, guid, 64);
        std::wprintf(L"      %s pid=%u vt=%u  %s\n", guid, key.pid,
                     static_cast<unsigned>(v.vt), text);
        PropVariantClear(&v);
    }
}

static void dump_flow(IMMDeviceEnumerator* enumerator, EDataFlow flow, const wchar_t* label) {
    IMMDeviceCollection* coll = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)) || !coll) return;

    UINT count = 0;
    coll->GetCount(&count);
    std::wprintf(L"\n===== %s (%u active) =====\n", label, count);

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(coll->Item(i, &dev)) || !dev) continue;

        IPropertyStore* store = nullptr;
        if (FAILED(dev->OpenPropertyStore(STGM_READ, &store)) || !store) {
            dev->Release();
            continue;
        }

        PROPVARIANT name;
        PropVariantInit(&name);
        std::wstring friendly;
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
            friendly = name.pwszVal;
        }
        PropVariantClear(&name);

        std::wprintf(L"\n  [%u] %s\n", i, friendly.c_str());
        dump_store(store);

        store->Release();
        dev->Release();
    }
    coll->Release();
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr) || !enumerator) {
        std::printf("CoCreateInstance failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    dump_flow(enumerator, eRender, L"RENDER");
    dump_flow(enumerator, eCapture, L"CAPTURE");

    enumerator->Release();
    CoUninitialize();
    return 0;
}
