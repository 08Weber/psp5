# Convert the supplied PSP5 boot-logo artwork to the compact texture used by boot.c.
# Run from the repository root with: powershell -ExecutionPolicy Bypass -File tools\make_logo.ps1
Add-Type -AssemblyName System.Drawing
$project = Split-Path $PSScriptRoot -Parent
$source = Join-Path $project 'res\BOOT_SOURCE.png'
$image = [System.Drawing.Bitmap]::new($source)

# Find the lit logo and retain a little black margin for the blue glow.
$x0 = $image.Width; $y0 = $image.Height; $x1 = -1; $y1 = -1
for ($y = 0; $y -lt $image.Height; $y++) {
    for ($x = 0; $x -lt $image.Width; $x++) {
        $c = $image.GetPixel($x, $y)
        if ([Math]::Max($c.R, [Math]::Max($c.G, $c.B)) -gt 5) {
            $x0 = [Math]::Min($x0, $x); $x1 = [Math]::Max($x1, $x)
            $y0 = [Math]::Min($y0, $y); $y1 = [Math]::Max($y1, $y)
        }
    }
}
if ($x1 -lt $x0) { throw 'No illuminated logo found in res/BOOT_SOURCE.png' }
$pad = 18
$x0 = [Math]::Max(0, $x0 - $pad); $y0 = [Math]::Max(0, $y0 - $pad)
$x1 = [Math]::Min($image.Width - 1, $x1 + $pad); $y1 = [Math]::Min($image.Height - 1, $y1 + $pad)
$crop = [System.Drawing.Rectangle]::new($x0, $y0, $x1 - $x0 + 1, $y1 - $y0 + 1)
$width = 256
$height = [Math]::Max(1, [int][Math]::Round($crop.Height * $width / $crop.Width))
$bitmap = [System.Drawing.Bitmap]::new($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::Black)
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$graphics.DrawImage($image, [System.Drawing.Rectangle]::new(0, 0, $width, $height), $crop, [System.Drawing.GraphicsUnit]::Pixel)
$graphics.Dispose(); $image.Dispose()

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('/* psp5 - made by 08Weber */')
[void]$sb.AppendLine('/* Generated from res/BOOT_SOURCE.png by tools/make_logo.ps1. */')
[void]$sb.AppendLine("#define LOGO_W $width")
[void]$sb.AppendLine("#define LOGO_H $height")
[void]$sb.AppendLine('static const unsigned short LOGO_PX[LOGO_W * LOGO_H] = {')
# GU_PSM_5650 uses the same channel packing as img_to_texture: red low, blue high.
# Expand System.Drawing's byte channels before left shifts to avoid 8-bit overflow.
for ($y = 0; $y -lt $height; $y++) {
    $row = for ($x = 0; $x -lt $width; $x++) {
        $c = $bitmap.GetPixel($x, $y)
        $r5 = [int]$c.R -shr 3
        $g6 = [int]$c.G -shr 2
        $b5 = [int]$c.B -shr 3
        '0x{0:x4}' -f [uint16](($b5 -shl 11) -bor ($g6 -shl 5) -bor $r5)
    }
    [void]$sb.AppendLine('    ' + ($row -join ',') + ',')
}
[void]$sb.AppendLine('};')
[System.IO.File]::WriteAllText((Join-Path $project 'src\logo.h'), $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
$preview = Join-Path $env:TEMP 'psp5_logo_preview.png'
$bitmap.Save($preview, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
"logo: $width x $height"
