# Kopierar launchern + testdata till PPSSPP:s minneskort och startar den i PPSSPP.
#   powershell -ExecutionPolicy Bypass -File tools\run-ppsspp.ps1 [-NoTestData]
param([switch]$NoTestData)

$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$memstick = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PPSSPP'
$ppsspp = Join-Path $env:ProgramFiles 'PPSSPP\PPSSPPWindows64.exe'

Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
New-Item -ItemType Directory -Force (Join-Path $memstick 'PSP\GAME') | Out-Null
Copy-Item -Recurse -Force (Join-Path $project 'dist\PSP') $memstick
if (-not $NoTestData) {
    Copy-Item -Recurse -Force (Join-Path $project 'testdata\ms0\*') $memstick
}

Start-Process $ppsspp -ArgumentList "`"$memstick\PSP\GAME\PSP5\EBOOT.PBP`""
