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
# main application
# ---------------------------------------------------------------------------
add_executable(battery_viewer WIN32 src/app_main.cpp src/providers.cpp)
target_link_libraries(battery_viewer PRIVATE hidapi imgui)

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
