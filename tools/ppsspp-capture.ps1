# psp5 – made by 08Weber
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
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static IntPtr FindLargestWindow(uint target) {
    IntPtr best = IntPtr.Zero; long bestArea = 0;
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == target && IsWindowVisible(h)) {
        RECT r; GetWindowRect(h, out r); long area = (long)(r.R-r.L)*(r.B-r.T);
        if (area > bestArea) { best = h; bestArea = area; }
      }
      return true;
    }, IntPtr.Zero);
    return best;
  }
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[Win]::SetProcessDPIAware() | Out-Null

$p = $null; $h = [IntPtr]::Zero
foreach ($candidate in (Get-Process PPSSPPWindows64 -ErrorAction Stop)) {
    $window = [Win]::FindLargestWindow([uint32]$candidate.Id)
    if ($window -ne [IntPtr]::Zero) { $p = $candidate; $h = $window; break }
}
if ($h -eq [IntPtr]::Zero) { throw 'Could not find a visible PPSSPP window.' }
[Win]::ShowWindow($h, 9) | Out-Null
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300

# PPSSPP läser riktiga tangenttryck (inte SendKeys-meddelanden), så vi använder keybd_event.
$vk = @{ '{UP}' = 0x26; '{DOWN}' = 0x28; '{LEFT}' = 0x25; '{RIGHT}' = 0x27; ' ' = 0x20; '{ESC}' = 0x1B; '{F12}' = 0x7B; '{L}' = 0x51; '{R}' = 0x57; '{START}' = 0x20; '{CROSS}' = 0x5A; '{CIRCLE}' = 0x58; '{SQUARE}' = 0x41; '{TRIANGLE}' = 0x53 }
foreach ($k in ($Keys -split ',' | Where-Object { $_ -ne '' })) {
    $parts = if ($k.StartsWith('{') -and $k.EndsWith('}') -and $k.Contains('+')) { $k.Trim('{}').Split('+') | ForEach-Object { "{$_}" } } else { @($k) }
    $pressed = @()
    foreach ($part in $parts) {
        $code = [byte]$(if ($vk.ContainsKey($part)) { $vk[$part] } else { [int][char]$part.ToUpper() })
        $ext = [uint32]$(if ($code -ge 0x25 -and $code -le 0x28) { 1 } else { 0 })
        $scan = [byte][Win]::MapVirtualKey($code, 0)
        [Win]::keybd_event($code, $scan, $ext, [UIntPtr]::Zero)
        $pressed += ,@($code, $scan, $ext)
    }
    Start-Sleep -Milliseconds 120
    for ($i = $pressed.Count - 1; $i -ge 0; $i--) {
        [Win]::keybd_event([byte]$pressed[$i][0], [byte]$pressed[$i][1], [uint32]($pressed[$i][2] -bor 2), [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds $Delay
}

$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hgt = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
try {
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
} catch {
    $dc = $g.GetHdc()
    [Win]::PrintWindow($h, $dc, 2) | Out-Null
    $g.ReleaseHdc($dc)
    Write-Warning 'Desktop capture was unavailable; used the window capture fallback.'
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out ($w x $hgt)"
