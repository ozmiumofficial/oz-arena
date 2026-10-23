param(
    [Parameter(Mandatory=$true)][int]$ProcId,
    [Parameter(Mandatory=$true)][string]$OutPath
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Cap {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

[Win32Cap]::SetProcessDPIAware() | Out-Null

# Wait up to 15s for the process to have a main window
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 30; $i++) {
    try {
        $p = Get-Process -Id $ProcId -ErrorAction Stop
        $p.Refresh()
        if ($p.MainWindowHandle -ne 0) { $hwnd = $p.MainWindowHandle; break }
    } catch {
        throw "Process $ProcId not found or exited"
    }
    Start-Sleep -Milliseconds 500
}
if ($hwnd -eq [IntPtr]::Zero) { throw "Process $ProcId has no visible window" }

# A minimized window has no pixels to render; restore it (without focusing) first
if ([Win32Cap]::IsIconic($hwnd)) { [Win32Cap]::ShowWindow($hwnd, 9) | Out-Null; Start-Sleep -Milliseconds 400 }

$rect = New-Object Win32Cap+RECT
[Win32Cap]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { throw "Window has zero size ($w x $h)" }

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)

# PrintWindow with PW_RENDERFULLCONTENT (2) captures the window's own surface,
# even when it is covered by other windows. No focus stealing.
$hdc = $gfx.GetHdc()
$printed = [Win32Cap]::PrintWindow($hwnd, $hdc, 2)
$gfx.ReleaseHdc($hdc)
$method = "PrintWindow"

if (-not $printed) {
    # Fallback: bring the window forward and copy screen pixels
    [Win32Cap]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 400
    [Win32Cap]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $method = "CopyFromScreen"
}

$dir = Split-Path -Parent $OutPath
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose()
$bmp.Dispose()
Write-Output "SAVED:$OutPath ($w x $h, $method)"
