# 截取指定标题的窗口到 PNG。用法：capture.ps1 -Title "外设电量" -Out "out\shot.png"
param(
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Out
)

Add-Type -AssemblyName System.Drawing

$src = @'
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;

public class WinCap
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string cls, string title);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);

    public static string Capture(string title, string path)
    {
        IntPtr h = FindWindowW(null, title);
        if (h == IntPtr.Zero) return "window not found";
        SetForegroundWindow(h);
        System.Threading.Thread.Sleep(400);

        RECT r;
        GetWindowRect(h, out r);
        int w = r.Right - r.Left;
        int ht = r.Bottom - r.Top;
        if (w <= 0 || ht <= 0) return "bad window size";

        using (Bitmap bmp = new Bitmap(w, ht))
        {
            using (Graphics g = Graphics.FromImage(bmp))
            {
                IntPtr hdc = g.GetHdc();
                bool ok = PrintWindow(h, hdc, 2);
                g.ReleaseHdc(hdc);
                if (!ok) return "PrintWindow failed";
            }
            bmp.Save(path, ImageFormat.Png);
        }
        return "ok " + w + "x" + ht;
    }
}
'@

Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing

$full = [System.IO.Path]::GetFullPath($Out)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($full)) | Out-Null
Write-Output ([WinCap]::Capture($Title, $full))
