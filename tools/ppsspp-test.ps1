# psp5 – made by Mille Weber
# Kör testbygget (dist-test) i PPSSPP med ett testskript – utan att skicka tangenter till skrivbordet.
#   powershell -ExecutionPolicy Bypass -File tools\ppsspp-test.ps1 -Script tools\tests\settings.txt -Out <mapp>
# -StartMark N låtsas att N starter i rad inte blev klara (startskyddet, tools\tests\safe.txt använder 1).
# -Exe och -Memstick: en portabel PPSSPP (minneskortet ligger då i dess mapp memstick).
# psp5 skriver ms0:/PSP5/shots/NAMN.req när en skärmdump ska tas; vi fotograferar då
# PPSSPP-fönstret med PrintWindow (fungerar även när det ligger bakom andra fönster)
# och tar bort filen, så fortsätter skriptet.
param([Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$Out, [int]$Timeout = 120, [int]$StartMark = 0,
      [string]$Exe = (Join-Path $env:ProgramFiles 'PPSSPP\PPSSPPWindows64.exe'),
      [string]$Memstick = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PPSSPP'))

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$memstick = $Memstick
$ppsspp = $Exe
$shots = Join-Path $memstick 'PSP5\shots'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class PspTest {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool Cb(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] static extern bool EnumWindows(Cb cb, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, Cb cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  // Klickar OK i PPSSPP:s felrutor (t.ex. "Graphics Error") utan tangenttryck.
  public static void DismissDialogs(uint pid) {
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var t = new StringBuilder(128); GetWindowText(h, t, 128);
      if (p == pid && t.ToString().Contains("Error"))
        EnumChildWindows(h, (c, l2) => { var s = new StringBuilder(32); GetWindowText(c, s, 32);
          if (s.ToString() == "OK") SendMessage(c, 0x00F5, IntPtr.Zero, IntPtr.Zero); return true; }, IntPtr.Zero);
      return true; }, IntPtr.Zero);
  }
}
'@
[PspTest]::SetProcessDPIAware() | Out-Null

function Save-Window($hwnd, $path) {
    $r = New-Object PspTest+RECT
    [PspTest]::GetClientRect($hwnd, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ([math]::Max(1, $r.R)), ([math]::Max(1, $r.B))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [PspTest]::PrintWindow($hwnd, $hdc, 3) | Out-Null  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
New-Item -ItemType Directory -Force (Join-Path $memstick 'PSP\GAME\PSP5'), (Join-Path $memstick 'PSP5'), $Out | Out-Null
Copy-Item -Force (Join-Path $project 'dist-test\PSP\GAME\PSP5\EBOOT.PBP') (Join-Path $memstick 'PSP\GAME\PSP5\EBOOT.PBP')
Copy-Item -Force $Script (Join-Path $memstick 'PSP5\test.txt')
Remove-Item -Recurse -Force $shots -ErrorAction SilentlyContinue
# PPSSPP stängs mitt i körningen, så startskyddets fil kan ligga kvar från förra testet
$mark = Join-Path $memstick 'PSP5\starting'
if ($StartMark -gt 0) { [IO.File]::WriteAllText($mark, "$StartMark") } else { Remove-Item $mark -ErrorAction SilentlyContinue }

$names = @(Get-Content $Script | Where-Object { $_ -match '^\s*shot\s+(\S+)' } | ForEach-Object { ($_ -split '\s+')[1] })

# starta normalt (minimerat kör PPSSPP inte), lägg fönstret längst bak och ge tillbaka fokus
$prev = [PspTest]::GetForegroundWindow()
$proc = Start-Process $ppsspp -ArgumentList "`"$memstick\PSP\GAME\PSP5\EBOOT.PBP`"" -PassThru
for ($i = 0; $i -lt 100 -and $proc.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 50; $proc.Refresh() }
[PspTest]::SetWindowPos($proc.MainWindowHandle, [IntPtr]1, 0, 0, 0, 0, 0x13) | Out-Null
[PspTest]::SetForegroundWindow($prev) | Out-Null

$done = 0
$t0 = Get-Date
while ($done -lt $names.Count -and ((Get-Date) - $t0).TotalSeconds -lt $Timeout) {
    [PspTest]::DismissDialogs([uint32]$proc.Id)
    $req = Join-Path $shots "$($names[$done]).req"
    if (Test-Path $req) {
        Start-Sleep -Milliseconds 250  # låt bildrutan visas
        $proc.Refresh()
        Save-Window $proc.MainWindowHandle (Join-Path $Out "$($names[$done]).png")
        Remove-Item $req
        $done++
    } else {
        Start-Sleep -Milliseconds 200
    }
}
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $memstick 'PSP5\test.txt') -ErrorAction SilentlyContinue
"$done av $($names.Count) skärmdumpar sparade i $Out"
