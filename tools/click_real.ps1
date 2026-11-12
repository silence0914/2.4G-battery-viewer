# 把窗口强行提到前台，再用真实鼠标事件点击（窗口被别的程序挡住时用）。
# 用法：click_real.ps1 -Title "外设电量" -X 132 -Y 66
param(
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][int]$X,
    [Parameter(Mandatory = $true)][int]$Y
)

$src = @'
using System;
using System.Runtime.InteropServices;

public class WinReal
{
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string cls, string title);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr pid);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();

    [DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hwnd, int cmd);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hwnd, ref POINT p);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);

    public static string Click(string title, int x, int y)
    {
        IntPtr h = FindWindowW(null, title);
        if (h == IntPtr.Zero) return "window not found";

        IntPtr fg = GetForegroundWindow();
        uint fgThread = GetWindowThreadProcessId(fg, IntPtr.Zero);
        uint myThread = GetCurrentThreadId();
        bool attached = AttachThreadInput(myThread, fgThread, true);
        ShowWindow(h, 9);                 // SW_RESTORE
        BringWindowToTop(h);
        bool ok = SetForegroundWindow(h);
        if (attached) AttachThreadInput(myThread, fgThread, false);
        System.Threading.Thread.Sleep(500);

        POINT p;
        p.X = x; p.Y = y;
        ClientToScreen(h, ref p);
        SetCursorPos(p.X, p.Y);
        System.Threading.Thread.Sleep(300);

        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);
        System.Threading.Thread.Sleep(120);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
        return "fg-ok=" + ok + " at " + p.X + "," + p.Y + " nowFG=" + (GetForegroundWindow() == h);
    }
}
'@

Add-Type -TypeDefinition $src
Write-Output ([WinReal]::Click($Title, $X, $Y))
