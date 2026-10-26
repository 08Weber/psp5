# psp5 – made by 08Weber
# Fotograferar ett fönster även om det ligger bakom andra (PrintWindow).
#   tools\window-shot.ps1 -Process PPSSPPWindows64 -Out shot.png
param([string]$Process = 'PPSSPPWindows64', [Parameter(Mandatory)][string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class WinShot {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[WinShot]::SetProcessDPIAware() | Out-Null
$wins = Get-Process $Process -ErrorAction Stop | Where-Object MainWindowHandle -ne 0
foreach ($p in $wins) {
    $r = New-Object WinShot+RECT
    [WinShot]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ([math]::Max(1, $r.R - $r.L)), ([math]::Max(1, $r.B - $r.T))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [WinShot]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null  # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "saved $Out ($($p.MainWindowTitle))"
}
