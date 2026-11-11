# Direct MSVC build script (no CMake).
#
# Why this exists: under the DSH sandbox, CMake's compiler detection hangs
# because it captures the compiler's output through a pipe. Invoking cl.exe
# directly, with output going straight to the console, works fine.
#
# CMakeLists.txt stays in the repo for CLion / normal machines.

param(
    [string]$Target = 'all',   # all | probe | rawprobe | hidpp | ikf | app
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$out = Join-Path $root 'out'

if ($Clean -and (Test-Path $out)) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$hidapi = @('third_party\hidapi\hidapi', 'third_party\hidapi\windows')
$imgui  = @('third_party\imgui', 'third_party\imgui\backends')

# C++/WinRT 头文件随 Windows SDK 提供，不需要 NuGet / vcpkg
$sdk_cppwinrt = (Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory -ErrorAction SilentlyContinue |
                 Sort-Object Name -Descending | Select-Object -First 1 |
                 ForEach-Object { Join-Path $_.FullName 'cppwinrt' })

$imgui_src = @(
    'third_party\imgui\imgui.cpp'
    'third_party\imgui\imgui_draw.cpp'
    'third_party\imgui\imgui_tables.cpp'
    'third_party\imgui\imgui_widgets.cpp'
    'third_party\imgui\backends\imgui_impl_win32.cpp'
    'third_party\imgui\backends\imgui_impl_dx11.cpp'
)

$hid_libs = @('setupapi.lib', 'hid.lib', 'ole32.lib')

$targets = @(
    @{ Name = 'probe';    Exe = 'probe.exe'
       Src  = @('src\probe_main.cpp', 'third_party\hidapi\windows\hid.c')
       Inc  = $hidapi
       Libs = $hid_libs }

    @{ Name = 'rawprobe'; Exe = 'rawprobe.exe'
       Src  = @('src\rawprobe.cpp', 'third_party\hidapi\windows\hid.c')
       Inc  = $hidapi
       Libs = $hid_libs }

    @{ Name = 'hidpp';    Exe = 'hidpp_probe.exe'
       Src  = @('src\hidpp_probe.cpp', 'third_party\hidapi\windows\hid.c')
       Inc  = $hidapi
       Libs = $hid_libs }

    @{ Name = 'ikf';      Exe = 'ikf_probe.exe'
       Src  = @('src\ikf_probe.cpp', 'third_party\hidapi\windows\hid.c')
       Inc  = $hidapi
       Libs = $hid_libs }

    @{ Name = 'pnp';      Exe = 'pnp_probe.exe'
       Src  = @('src\pnp_probe.cpp')
       Inc  = @()
       Libs = @('setupapi.lib', 'ole32.lib') }

    @{ Name = 'audio';    Exe = 'audio_probe.exe'
       Src  = @('src\audio_probe.cpp')
       Inc  = @()
       Libs = @('ole32.lib', 'propsys.lib') }

    @{ Name = 'bletest';  Exe = 'bletest.exe'
       Src  = @('src\ble_test.cpp', 'src\ble_battery.cpp', 'src\bt_pnp_battery.cpp')
       Inc  = @($sdk_cppwinrt)
       Libs = @('WindowsApp.lib', 'setupapi.lib') }

    @{ Name = 'app';      Exe = 'battery_viewer.exe'
       Src  = (@('src\app_main.cpp', 'src\providers.cpp', 'src\ble_battery.cpp', 'src\bt_pnp_battery.cpp') +
               $imgui_src + @('third_party\hidapi\windows\hid.c'))
       Inc  = ($hidapi + $imgui + @($sdk_cppwinrt))
       Libs = ($hid_libs + @('d3d11.lib', 'dxgi.lib', 'd3dcompiler.lib', 'WindowsApp.lib')) }
)

if ($Target -ne 'all') {
    $targets = $targets | Where-Object { $_.Name -eq $Target }
    if (-not $targets) { Write-Host "unknown target: $Target"; exit 2 }
}

$defs = '/DNDEBUG /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DHID_API_NO_EXPORT_DEFINE'

$failed = 0
foreach ($t in $targets) {
    $inc = ($t.Inc | ForEach-Object {
        $p = if ([System.IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $root $_ }
        "/I`"$p`""
    }) -join ' '
    $src = ($t.Src | ForEach-Object { '"' + (Join-Path $root $_) + '"' }) -join ' '
    $lib = ($t.Libs -join ' ')

    $cmd = "`"$vcvars`" >nul 2>&1 && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /W3 /utf-8 /O2 /MD " +
           "$defs $inc $src /link /INCREMENTAL:NO $lib /OUT:$($t.Exe)"

    Write-Host "building $($t.Exe)..."
    cmd /c $cmd
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK -> $out\$($t.Exe)"
    } else {
        Write-Host "  FAILED (exit $LASTEXITCODE)"
        $failed++
    }
}

exit $(if ($failed -eq 0) { 0 } else { 1 })
