// ---------------------------------------------------------------------------
// 外设电量 -- 主程序（Win32 + D3D11 + Dear ImGui）
// ---------------------------------------------------------------------------

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "providers.h"

// ---------------------------------------------------------------------------
// D3D11
// ---------------------------------------------------------------------------
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool g_running = true;

static void create_render_target() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void cleanup_render_target() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool create_device_d3d(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &level, &g_ctx);
    }
    if (FAILED(hr)) return false;

    create_render_target();
    return true;
}

static void cleanup_device_d3d() {
    cleanup_render_target();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

// ---------------------------------------------------------------------------
// 后台轮询
// ---------------------------------------------------------------------------
struct SharedState {
    std::mutex mtx;
    std::vector<DeviceInfo> devices;
    std::string last_time;
    std::atomic<bool> stop{false};
    std::atomic<bool> refresh{true};
};

static SharedState g_shared;

static void worker_main() {
    while (!g_shared.stop) {
        if (g_shared.refresh.exchange(false)) {
            std::vector<DeviceInfo> res = poll_devices();

            std::time_t t = std::time(nullptr);
            std::tm tmv{};
            localtime_s(&tmv, &t);
            char stamp[32] = {0};
            std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);

            {
                std::lock_guard<std::mutex> lock(g_shared.mtx);
                g_shared.devices = std::move(res);
                g_shared.last_time = stamp;
            }
        }

        // 等 5 秒，期间允许手动刷新打断
        for (int i = 0; i < 50 && !g_shared.stop; ++i) {
            if (g_shared.refresh) break;
            Sleep(100);
        }
        if (!g_shared.stop) g_shared.refresh = true;
    }
}

// ---------------------------------------------------------------------------
// 界面
// ---------------------------------------------------------------------------
static const ImU32 kTrackBg = IM_COL32(52, 56, 63, 255);
static const ImU32 kGood = IM_COL32(86, 200, 120, 255);
static const ImU32 kWarn = IM_COL32(235, 180, 70, 255);
static const ImU32 kBad = IM_COL32(230, 92, 82, 255);
static const ImU32 kUnknown = IM_COL32(110, 116, 126, 255);
static const ImU32 kBorder = IM_COL32(70, 75, 84, 255);

static ImU32 level_color(int pct) {
    if (pct < 0) return kUnknown;
    if (pct <= 20) return kBad;
    if (pct <= 50) return kWarn;
    return kGood;
}

static void draw_battery(ImDrawList* dl, ImVec2 p, float w, float h, const DeviceInfo& d) {
    // 外壳
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), kBorder, 4.0f, 0, 2.0f);
    // 正极凸起
    dl->AddRectFilled(ImVec2(p.x + w + 2.0f, p.y + h * 0.32f),
                      ImVec2(p.x + w + 6.0f, p.y + h * 0.68f), kBorder, 1.5f);

    if (d.status != BatteryStatus::Ok || d.percent < 0) {
        // 未知：只画空槽 + 斜线
        dl->AddLine(ImVec2(p.x + 6.0f, p.y + h - 6.0f),
                    ImVec2(p.x + w - 6.0f, p.y + 6.0f), kTrackBg, 3.0f);
        return;
    }

    float inner_w = w * (static_cast<float>(d.percent) / 100.0f) - 6.0f;
    if (inner_w < 2.0f) inner_w = 2.0f;
    dl->AddRectFilled(ImVec2(p.x + 3.0f, p.y + 3.0f),
                      ImVec2(p.x + 3.0f + inner_w, p.y + h - 3.0f),
                      level_color(d.percent), 2.0f);
}

static void draw_card(const DeviceInfo& d) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.118f, 0.126f, 0.145f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);

    ImGui::BeginChild(d.kind_label.c_str(), ImVec2(0.0f, 112.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool has_data = (d.status == BatteryStatus::Ok);

    // 类型
    ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.62f, 0.72f, 1.0f));
    ImGui::TextUnformatted(d.kind_label.c_str());
    ImGui::PopStyleColor();

    // 型号
    ImGui::SetCursorPos(ImVec2(16.0f, 30.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          d.status == BatteryStatus::Absent
                              ? ImVec4(0.60f, 0.61f, 0.65f, 1.0f)
                              : ImVec4(0.93f, 0.94f, 0.96f, 1.0f));
    ImGui::TextUnformatted(d.model.c_str());
    ImGui::PopStyleColor();

    // 电量条
    const float bar_w = 230.0f, bar_h = 20.0f;
    ImGui::SetCursorPos(ImVec2(16.0f, 58.0f));
    ImVec2 bp = ImGui::GetCursorScreenPos();
    draw_battery(ImGui::GetWindowDrawList(), bp, bar_w, bar_h, d);
    ImGui::Dummy(ImVec2(bar_w, bar_h));

    ImGui::SameLine(0.0f, 14.0f);
    if (has_data) {
        ImGui::SetCursorPosY(59.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(level_color(d.percent)));
        ImGui::Text("%d%%", d.percent);
        ImGui::PopStyleColor();

        if (d.millivolts > 0) {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.53f, 0.58f, 1.0f));
            ImGui::Text("%d mV", d.millivolts);
            ImGui::PopStyleColor();
        }
        if (d.charging) {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.78f, 0.47f, 1.0f));
            ImGui::TextUnformatted("充电中");
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::SetCursorPosY(59.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.47f, 0.51f, 1.0f));
        ImGui::TextUnformatted("电量不可读");
        ImGui::PopStyleColor();
    }

    // 底部说明
    std::string sub = d.transport;
    if (!d.note.empty()) {
        if (!sub.empty()) sub += "  ·  ";
        sub += d.note;
    }
    ImGui::SetCursorPos(ImVec2(16.0f, 86.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.53f, 0.58f, 1.0f));
    ImGui::TextUnformatted(sub.c_str());
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

// 卡片容器：圆角深色块
static void card_begin(const char* id, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.118f, 0.126f, 0.145f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::BeginChild(id, ImVec2(0.0f, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

static void card_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

static void section_title(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.62f, 0.72f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static void dim_text(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.53f, 0.58f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static void draw_devices() {
    std::vector<DeviceInfo> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_shared.mtx);
        snapshot = g_shared.devices;
    }

    // 只显示已连接的设备：没插接收器 / 没配对的一律不占位
    std::vector<DeviceInfo> shown;
    shown.reserve(snapshot.size());
    for (const DeviceInfo& d : snapshot) {
        if (d.status != BatteryStatus::Absent) shown.push_back(d);
    }

    if (shown.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 26.0f));
        ImGui::SetCursorPosX(18.0f);
        dim_text(snapshot.empty() ? "正在扫描外设…" : "没有检测到已连接的外设");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::SetCursorPosX(18.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.42f, 0.46f, 1.0f));
        ImGui::TextUnformatted("插好接收器或连上蓝牙后会自动出现");
        ImGui::PopStyleColor();
        return;
    }

    for (const DeviceInfo& d : shown) draw_card(d);
}

static void draw_ui() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // 标题栏
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
    ImGui::TextUnformatted("外设电量");
    ImGui::PopStyleColor();

    std::string last;
    {
        std::lock_guard<std::mutex> lock(g_shared.mtx);
        last = g_shared.last_time;
    }

    if (!last.empty()) {
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.48f, 0.53f, 1.0f));
        ImGui::Text("%s 更新", last.c_str());
        ImGui::PopStyleColor();
    }

    // 刷新按钮靠右
    const char* btn = "刷新";
    float btn_w = ImGui::CalcTextSize(btn).x + 26.0f;
    float target_x = ImGui::GetWindowWidth() - btn_w - 18.0f;
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() < target_x) ImGui::SetCursorPosX(target_x);
    if (ImGui::Button(btn)) g_shared.refresh = true;

    ImGui::Dummy(ImVec2(0, 4));

    draw_devices();

    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Win32
// ---------------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

    switch (msg) {
        case WM_SIZE:
            if (g_dev && wp != SIZE_MINIMIZED) {
                cleanup_render_target();
                g_swap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
                create_render_target();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();

    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\Deng.ttf",
    };

    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.FontNo = 0;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        ImFont* f = io.Fonts->AddFontFromFileTTF(
            path, 18.0f, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (f) return;
    }
    io.Fonts->AddFontDefault();
}

int main() {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"BatteryViewerWnd";
    RegisterClassExW(&wc);

    int win_w = 560, win_h = 380;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - win_w) / 2;
    int y = work.top + ((work.bottom - work.top) - win_h) / 2;

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"外设电量",
                              WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                              x, y, win_w, win_h,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    if (!create_device_d3d(hwnd)) {
        cleanup_device_d3d();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    load_fonts();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    std::thread worker(worker_main);

    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }
        if (!g_running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        draw_ui();

        ImGui::Render();
        const float clear[4] = {0.055f, 0.06f, 0.068f, 1.0f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    g_shared.stop = true;
    worker.join();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanup_device_d3d();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
