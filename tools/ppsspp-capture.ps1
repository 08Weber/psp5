# Testhjälp: skickar valfria tangenter till PPSSPP och sparar en skärmdump av fönstret.
#   tools\ppsspp-capture.ps1 -Out shot.png [-Keys '{DOWN},{DOWN},z'] [-Delay 500]
# PPSSPP:s standardtangenter: piltangenter = styrkors, Z = kryss, X = cirkel, mellanslag = START.
param([string]$Out = 'ppsspp.png', [string]$Keys = '', [int]$Delay = 400)

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[Win]::SetProcessDPIAware() | Out-Null

$p = Get-Process PPSSPPWindows64 -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
$h = $p.MainWindowHandle
[Win]::ShowWindow($h, 9) | Out-Null
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300

# PPSSPP läser riktiga tangenttryck (inte SendKeys-meddelanden), så vi använder keybd_event.
$vk = @{ '{UP}' = 0x26; '{DOWN}' = 0x28; '{LEFT}' = 0x25; '{RIGHT}' = 0x27; ' ' = 0x20; '{ESC}' = 0x1B }
foreach ($k in ($Keys -split ',' | Where-Object { $_ -ne '' })) {
    $code = [byte]$(if ($vk.ContainsKey($k)) { $vk[$k] } else { [int][char]$k.ToUpper() })
    $ext = [uint32]$(if ($code -ge 0x25 -and $code -le 0x28) { 1 } else { 0 })
    $scan = [byte][Win]::MapVirtualKey($code, 0)
    [Win]::keybd_event($code, $scan, $ext, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [Win]::keybd_event($code, $scan, [uint32]($ext -bor 2), [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $Delay
}

$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hgt = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out ($w x $hgt)"
