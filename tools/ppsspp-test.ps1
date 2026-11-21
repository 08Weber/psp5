# PSP5 REMAKE – fork by medhatk (based on psp5 by 08Weber)
# Run the PSP5_TEST build in PPSSPP and capture the visible emulator window at each screenshot request.
#   powershell -ExecutionPolicy Bypass -File tools\ppsspp-test.ps1 -Script tools\tests\settings.txt -Out <mapp>
# -StartMark N låtsas att N starter i rad inte blev klara (startskyddet, tools\tests\safe.txt använder 1).
# -Exe och -Memstick: en portabel PPSSPP (minneskortet ligger då i dess mapp memstick).
# psp5 writes ms0:/PSP5/shots/NAME.req for each screenshot; this script captures the foreground
# PPSSPP window and removes the request so the automated sequence can continue.
param([Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$Out, [int]$Timeout = 120, [int]$StartMark = 0,
      [string]$Exe = (Join-Path $env:ProgramFiles 'PPSSPP\PPSSPPWindows64.exe'),
      [string]$Memstick = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PPSSPP'))

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$memstick = $Memstick
$ppsspp = $Exe
$shots = Join-Path $memstick 'PSP5\shots'
$testGame = Join-Path $memstick ("PSP\GAME\PSP5_TEST_{0}_{1}" -f (Get-Date -Format 'yyyyMMdd_HHmmss'), $PID)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class PspTest {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool Cb(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref RECT p);
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
    [PspTest]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 200
    if ([PspTest]::GetForegroundWindow() -ne $hwnd) { return $false }
    $r = New-Object PspTest+RECT
    [PspTest]::GetClientRect($hwnd, [ref]$r) | Out-Null
    $w = [math]::Max(1, $r.R); $h = [math]::Max(1, $r.B)
    $p = New-Object PspTest+RECT
    [PspTest]::ClientToScreen($hwnd, [ref]$p) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($p.L, $p.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    return $true
}

$running = @(Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue)
if ($running.Count) { throw 'Close PPSSPP before starting the automated capture; this script will not stop an existing session.' }
$binary = Join-Path $project 'dist-test\PSP\GAME\PSP5\EBOOT.PBP'
if (-not (Test-Path -LiteralPath $binary)) { throw "Missing PSP5_TEST build: $binary. Run tools/build.sh test first." }
$names = @(Get-Content $Script | Where-Object { $_ -match '^\s*shot\s+(\S+)' } | ForEach-Object { ($_ -split '\s+')[1] })
if (-not $names.Count) { throw 'The test script does not contain any shot steps.' }
if (@($names | Where-Object { $_ -notmatch '^[A-Za-z0-9_-]{1,31}$' }).Count) { throw 'Screenshot names must use 1–31 letters, digits, underscores, or hyphens.' }
$testFile = Join-Path $memstick 'PSP5\test.txt'
$fake = Join-Path $memstick 'PSP5\fake.txt'
$mark = Join-Path $memstick 'PSP5\starting'
$preserved = @{}
foreach ($path in @($testFile, $fake, $mark)) {
    $preserved[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$shotFiles = @{}
# Leave the test build's generated sounds and any other files in PSP5/shots alone.
foreach ($name in $names) {
    foreach ($ext in @('req', 'png')) {
        $path = Join-Path $shots "$name.$ext"
        $shotFiles[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
    }
}

$proc = $null; $createdTestGame = $false; $done = 0; $saved = 0
try {
    if (Test-Path -LiteralPath $testGame) { throw "Temporary game folder already exists: $testGame" }
    New-Item -ItemType Directory -Force $testGame | Out-Null
    $createdTestGame = $true
    New-Item -ItemType Directory -Force (Join-Path $memstick 'PSP5'), $shots, $Out | Out-Null
    $outDir = (Resolve-Path -LiteralPath $Out).Path
    foreach ($name in $names) { Remove-Item -LiteralPath (Join-Path $outDir "$name.png") -ErrorAction SilentlyContinue }
    Copy-Item -Force $binary (Join-Path $testGame 'EBOOT.PBP')
    Copy-Item -Force $Script $testFile
    [IO.File]::WriteAllText($fake, 'model=0 ark=5')
    foreach ($path in $shotFiles.Keys) { Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue }
    if ($StartMark -gt 0) { [IO.File]::WriteAllText($mark, "$StartMark") } else { Remove-Item -LiteralPath $mark -ErrorAction SilentlyContinue }

    $proc = Start-Process $ppsspp -ArgumentList "`"$testGame\EBOOT.PBP`"" -PassThru
    for ($i = 0; $i -lt 100 -and $proc.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 50; $proc.Refresh() }
    if ($proc.MainWindowHandle -eq 0) { throw 'PPSSPP did not create a window.' }
    [PspTest]::ShowWindow($proc.MainWindowHandle, 9) | Out-Null
    [PspTest]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null

    $t0 = Get-Date
    while ($done -lt $names.Count -and ((Get-Date) - $t0).TotalSeconds -lt $Timeout) {
        [PspTest]::DismissDialogs([uint32]$proc.Id)
        $req = Join-Path $shots "$($names[$done]).req"
        if (Test-Path -LiteralPath $req) {
            if (-not (Save-Window $proc.MainWindowHandle (Join-Path $outDir "$($names[$done]).png"))) {
                Write-Warning "PPSSPP was not the foreground window; waiting before capturing $($names[$done])."
                Start-Sleep -Milliseconds 500
                continue
            }
            $saved++
            Remove-Item -LiteralPath $req
            $done++
        } else {
            Start-Sleep -Milliseconds 200
        }
    }
} finally {
    if ($proc) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    foreach ($path in $preserved.Keys) {
        if ($null -eq $preserved[$path]) { Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue }
        else { [IO.File]::WriteAllBytes($path, $preserved[$path]) }
    }
    foreach ($path in $shotFiles.Keys) {
        if ($null -eq $shotFiles[$path]) { Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue }
        else { [IO.File]::WriteAllBytes($path, $shotFiles[$path]) }
    }
    if ($createdTestGame) { Remove-Item -LiteralPath $testGame -Recurse -Force -ErrorAction SilentlyContinue }
}
"$saved of $($names.Count) PPSSPP window screenshots saved in $Out ($done steps completed)"
