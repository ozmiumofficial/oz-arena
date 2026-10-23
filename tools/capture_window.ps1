# Screenshot of a window by its title, including GPU-composited content (the main menu's web view,
# which Godot's own viewport and screen grabs can't see). Uses PrintWindow with PW_RENDERFULLCONTENT.
#   powershell -File capture_window.ps1 -Title "Q3Remaster" -Out shot.png [-Delay 8] [-Count 1] [-Every 3]
param([string]$Title = "Q3Remaster", [string]$Out = "shot.png", [double]$Delay = 8, [int]$Count = 1, [double]$Every = 3)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

Start-Sleep -Milliseconds ([int]($Delay * 1000))
$p = Get-Process | Where-Object { $_.MainWindowTitle -like "*$Title*" } | Select-Object -First 1
if (-not $p) { Write-Error "no window titled *$Title*"; exit 1 }
$h = $p.MainWindowHandle
for ($i = 0; $i -lt $Count; $i++) {
    $wr = New-Object Win+RECT
    [void][Win]::GetWindowRect($h, [ref]$wr)
    $w = $wr.Right - $wr.Left; $ht = $wr.Bottom - $wr.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [void][Win]::PrintWindow($h, $dc, 2)   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($dc)
    # crop to the client area
    $cr = New-Object Win+RECT
    [void][Win]::GetClientRect($h, [ref]$cr)
    $pt = New-Object Win+POINT
    [void][Win]::ClientToScreen($h, [ref]$pt)
    $rect = New-Object System.Drawing.Rectangle ($pt.X - $wr.Left), ($pt.Y - $wr.Top), $cr.Right, $cr.Bottom
    $client = $bmp.Clone($rect, $bmp.PixelFormat)
    $file = if ($Count -gt 1) { $Out -replace '\.png$', "_$i.png" } else { $Out }
    $client.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose(); $client.Dispose()
    Write-Output "saved $file"
    if ($i -lt $Count - 1) { Start-Sleep -Milliseconds ([int]($Every * 1000)) }
}
