cmake_minimum_required(VERSION 3.21)
project(battery_viewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# hidapi (vendored, static build, Windows WinAPI backend)
# ---------------------------------------------------------------------------
add_library(hidapi STATIC third_party/hidapi/windows/hid.c)
target_include_directories(hidapi
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hidapi/hidapi
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hidapi/windows)
target_compile_definitions(hidapi PUBLIC HID_API_NO_EXPORT_DEFINE)
target_link_libraries(hidapi PUBLIC setupapi hid ole32)

# ---------------------------------------------------------------------------
# Dear ImGui (vendored)
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler)

# ---------------------------------------------------------------------------
# C++/WinRT 头文件随 Windows SDK 提供（蓝牙 GATT 用），不需要 NuGet
# ---------------------------------------------------------------------------
set(CPPWINRT_DIR "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt")
if (NOT EXISTS "${CPPWINRT_DIR}/winrt/base.h")
    file(GLOB _sdk_versions "C:/Program Files (x86)/Windows Kits/10/Include/10.*")
    list(SORT _sdk_versions)
    list(REVERSE _sdk_versions)
    foreach (_v ${_sdk_versions})
        if (EXISTS "${_v}/cppwinrt/winrt/base.h")
            set(CPPWINRT_DIR "${_v}/cppwinrt")
            break()
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# main application
# ---------------------------------------------------------------------------
add_executable(battery_viewer WIN32
    src/app_main.cpp
    src/providers.cpp
    src/ble_battery.cpp
    src/bt_pnp_battery.cpp
    src/bt_link.cpp)
target_include_directories(battery_viewer PRIVATE ${CPPWINRT_DIR})
target_link_libraries(battery_viewer PRIVATE hidapi imgui WindowsApp setupapi bthprops)
# WIN32 子系统默认找 WinMain，但代码里写的是 main()，显式指定入口
target_link_options(battery_viewer PRIVATE /ENTRY:mainCRTStartup)

# ---------------------------------------------------------------------------
# command line tools
# ---------------------------------------------------------------------------
add_executable(probe src/probe_main.cpp)
target_link_libraries(probe PRIVATE hidapi)

add_executable(rawprobe src/rawprobe.cpp)
target_link_libraries(rawprobe PRIVATE hidapi)

add_executable(hidpp_probe src/hidpp_probe.cpp)
target_link_libraries(hidpp_probe PRIVATE hidapi)

if (MSVC)
    foreach (t battery_viewer probe rawprobe hidpp_probe)
        target_compile_options(${t} PRIVATE /utf-8 /W3)
    endforeach()
    target_compile_options(hidapi PRIVATE /W3)
endif()
