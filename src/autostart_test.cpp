// ---------------------------------------------------------------------------
// autostart 自测：开 -> 读 -> 关 -> 读，再恢复原状
//
// 请以正常用户身份直接双击运行（不要从受限环境里跑），正常应打印 PASS。
// ---------------------------------------------------------------------------

#include <cstdio>

#include "autostart.h"

int main() {
    const bool initial = autostart_enabled();
    const std::string initial_how = autostart_backend();

    std::printf("exe          : %s\n", current_exe_path().c_str());
    std::printf("初始状态     : enabled=%d  backend=[%s]\n", initial ? 1 : 0,
                initial_how.c_str());

    int fail = 0;

    bool ok = set_autostart(true);
    bool on = autostart_enabled();
    std::string how = autostart_backend();
    std::printf("set(true)    : ret=%d enabled=%d backend=[%s]\n", ok ? 1 : 0, on ? 1 : 0,
                how.c_str());
    if (!ok || !on) {
        std::printf("  !! 开启失败: %s\n", autostart_last_error().c_str());
        fail++;
    } else if (how == "Startup") {
        std::printf("  (注册表被拦，已改用启动文件夹快捷方式)\n");
    }

    ok = set_autostart(false);
    bool off = autostart_enabled();
    std::printf("set(false)   : ret=%d enabled=%d\n", ok ? 1 : 0, off ? 1 : 0);
    if (!ok || off) {
        std::printf("  !! 关闭失败: %s\n", autostart_last_error().c_str());
        fail++;
    }

    // 恢复用户原本的设置
    set_autostart(initial);
    std::printf("已恢复       : enabled=%d  backend=[%s]\n", autostart_enabled() ? 1 : 0,
                autostart_backend().c_str());

    std::printf("%s\n", fail == 0 ? "PASS" : "FAIL");
    return fail == 0 ? 0 : 1;
}
