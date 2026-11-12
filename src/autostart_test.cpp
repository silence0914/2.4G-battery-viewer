// ---------------------------------------------------------------------------
// autostart 注册表读写自测：跑一遍 开 -> 读 -> 关 -> 读，再恢复原状
// ---------------------------------------------------------------------------

#include <cstdio>

#include "autostart.h"

int main() {
    const bool initial = autostart_enabled();
    std::printf("exe          : %s\n", current_exe_path().c_str());
    std::printf("初始状态     : enabled=%d  cmd=[%s]\n", initial ? 1 : 0,
                autostart_command().c_str());

    int fail = 0;

    bool ok = set_autostart(true);
    bool on = autostart_enabled();
    std::string cmd = autostart_command();
    std::printf("set(true)    : ret=%d enabled=%d cmd=[%s]\n", ok ? 1 : 0, on ? 1 : 0,
                cmd.c_str());
    if (!ok || !on || cmd.empty()) {
        std::printf("  !! 开启失败\n");
        fail++;
    }

    ok = set_autostart(false);
    bool off = autostart_enabled();
    std::printf("set(false)   : ret=%d enabled=%d\n", ok ? 1 : 0, off ? 1 : 0);
    if (!ok || off) {
        std::printf("  !! 关闭失败\n");
        fail++;
    }

    // 恢复用户原本的设置
    set_autostart(initial);
    std::printf("已恢复       : enabled=%d\n", autostart_enabled() ? 1 : 0);

    std::printf("%s\n", fail == 0 ? "PASS" : "FAIL");
    return fail == 0 ? 0 : 1;
}
