# psp5 - made by 08Weber
# Ritar ikonerna 8 ganger storre och skalar ner med medelvarde, sa att de blir mjukt
# kantutjamnade i PSP:ns laga upplosning. Sparas som alfakartor i src/icons.h.
# Topprad och installningar: 16 x 16, tunna konturer (som Figma-designen).
# Control Center: 24 x 24, fyllda former (som ovriga CC-ikoner).
# Kors i Windows: powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
Add-Type -AssemblyName System.Drawing
$project = Split-Path $PSScriptRoot -Parent
$S = 8  # overskalning

function New-Canvas($n) {
    $bmp = New-Object System.Drawing.Bitmap ($n * $S), ($n * $S)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black); $g.SmoothingMode = 'AntiAlias'
    return @($bmp, $g, $n)
}
function New-Pen($w) { $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([float]($w * $S)); $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p.LineJoin = 'Round'; return $p }
function F($v) { return [float]($v * $S) }
function New-RoundRectPath($x, $y, $w, $h, $r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc([float]$x, [float]$y, [float]$d, [float]$d, 180, 90)
    $p.AddArc([float]($x + $w - $d), [float]$y, [float]$d, [float]$d, 270, 90)
    $p.AddArc([float]($x + $w - $d), [float]($y + $h - $d), [float]$d, [float]$d, 0, 90)
    $p.AddArc([float]$x, [float]($y + $h - $d), [float]$d, [float]$d, 90, 90)
    $p.CloseFigure()
    return $p
}
function New-PointList($pairs) {
    $list = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($pt in $pairs) {
        $list.Add((New-Object System.Drawing.PointF ([float]$pt[0]), ([float]$pt[1])))
    }
    return ,$list.ToArray()
}
# kugghjulets kontur: 8 kuggar runt (cx, cy) i ikonens enheter
function Get-GearPoints($cx, $cy, $root, $tip) {
    $pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    for ($k = 0; $k -lt 8; $k++) {
        $a = $k * [Math]::PI / 4
        foreach ($d in @(@(-0.30, $root), @(-0.17, $tip), @(0.17, $tip), @(0.30, $root))) {
            $pts.Add((New-Object System.Drawing.PointF ([float](($cx + [Math]::Cos($a + $d[0]) * $d[1]) * $S)), ([float](($cy + [Math]::Sin($a + $d[0]) * $d[1]) * $S))))
        }
        $mid = $a + [Math]::PI / 8
        $pts.Add((New-Object System.Drawing.PointF ([float](($cx + [Math]::Cos($mid) * $root) * $S)), ([float](($cy + [Math]::Sin($mid) * $root) * $S))))
    }
    return ,$pts.ToArray()
}
function Fill-Dot($g, $cx, $cy, $r) { $g.FillEllipse([System.Drawing.Brushes]::White, (F ($cx - $r)), (F ($cy - $r)), (F (2 * $r)), (F (2 * $r))) }

# ---------------- topprad (16 x 16) ----------------

# forstoringsglas: ring och skaft
$search = New-Canvas 16; $g = $search[1]
$g.DrawEllipse((New-Pen 1.7), (F 2.2), (F 2.2), (F 8.6), (F 8.6))
$g.DrawLine((New-Pen 2.0), (F 10.0), (F 10.0), (F 13.6), (F 13.6))

# kugghjul: kontur med ring i mitten (samma tunna stil som flikikonerna, som i Figma)
$gear = New-Canvas 16; $g = $gear[1]
$g.DrawPolygon((New-Pen 1.3), (Get-GearPoints 8.0 8.0 5.0 6.9))
$g.DrawEllipse((New-Pen 1.3), (F 5.9), (F 5.9), (F 4.2), (F 4.2))

# handkontroll (Games): rundad kapsel som kontur, styrkors och tva knappar
$games = New-Canvas 16; $g = $games[1]
$g.DrawPath((New-Pen 1.3), (New-RoundRectPath (F 1.2) (F 4.0) (F 13.6) (F 8.2) (F 4.1)))
$dp = New-Pen 1.25
$g.DrawLine($dp, (F 3.5), (F 8.1), (F 6.5), (F 8.1))
$g.DrawLine($dp, (F 5.0), (F 6.6), (F 5.0), (F 9.6))
Fill-Dot $g 10.4 8.9 0.95
Fill-Dot $g 11.9 7.2 0.95

# appar (Apps): 2x2 konturrutnat
$apps = New-Canvas 16; $g = $apps[1]
$apen = New-Pen 1.35
foreach ($ry in @(2.5, 9.5)) {
    foreach ($rx in @(2.5, 9.5)) {
        $g.DrawPath($apen, (New-RoundRectPath ($rx * $S) ($ry * $S) (4.0 * $S) (4.0 * $S) (0.7 * $S)))
    }
}

# bibliotek (Library): tre staplade lager
$library = New-Canvas 16; $g = $library[1]
$lpen = New-Pen 1.2
$g.DrawPolygon($lpen, (New-PointList @(@((F 8.0), (F 2.4)), @((F 13.3), (F 5.0)), @((F 8.0), (F 7.6)), @((F 2.7), (F 5.0)))))
$g.DrawLines($lpen, (New-PointList @(@((F 2.7), (F 8.1)), @((F 8.0), (F 10.7)), @((F 13.3), (F 8.1)))))
$g.DrawLines($lpen, (New-PointList @(@((F 2.7), (F 11.1)), @((F 8.0), (F 13.7)), @((F 13.3), (F 11.1)))))

# musik (Music): dubbla attondelsnoter
$music = New-Canvas 16; $g = $music[1]
$mpen = New-Pen 1.2
$g.DrawEllipse($mpen, (F 2.6), (F 9.8), (F 3.5), (F 3.5))
$g.DrawEllipse($mpen, (F 9.4), (F 8.7), (F 3.5), (F 3.5))
$g.DrawLine($mpen, (F 6.1), (F 11.5), (F 6.1), (F 3.9))
$g.DrawLine($mpen, (F 12.9), (F 10.4), (F 12.9), (F 2.8))
$g.DrawLine((New-Pen 1.4), (F 6.1), (F 3.9), (F 12.9), (F 2.8))

# ---------------- installningarnas kategorier (16 x 16) ----------------

# psp5: fylld rosett (som Figma) med ett litet hal
$cpsp5 = New-Canvas 16; $g = $cpsp5[1]
$ros = New-Object System.Collections.Generic.List[System.Drawing.PointF]
for ($k = 0; $k -lt 24; $k++) {
    $a = $k * [Math]::PI / 12 - [Math]::PI / 2; $r = if ($k % 2) { 5.3 } else { 6.5 }
    $ros.Add((New-Object System.Drawing.PointF (F (8.0 + [Math]::Cos($a) * $r)), (F (8.0 + [Math]::Sin($a) * $r))))
}
$rp = New-Object System.Drawing.Drawing2D.GraphicsPath
$rp.AddClosedCurve($ros.ToArray(), 0.35)
$rp.AddEllipse((F 6.3), (F 6.3), (F 3.4), (F 3.4))
$rp.FillMode = 'Alternate'
$g.FillPath([System.Drawing.Brushes]::White, $rp)

# tema: palett med fargklickar
$ctheme = New-Canvas 16; $g = $ctheme[1]
$g.DrawEllipse((New-Pen 1.3), (F 2.0), (F 2.0), (F 12.0), (F 12.0))
Fill-Dot $g 5.4 7.0 1.05
Fill-Dot $g 7.9 4.9 1.05
Fill-Dot $g 10.6 6.2 1.05
Fill-Dot $g 6.0 10.2 1.05

# prestanda: hastighetsmatare
$cperf = New-Canvas 16; $g = $cperf[1]
$g.DrawArc((New-Pen 1.3), (F 2.0), (F 3.0), (F 12.0), (F 12.0), 150, 240)
$g.DrawLine((New-Pen 1.4), (F 8.0), (F 9.0), (F 10.9), (F 5.9))
Fill-Dot $g 8.0 9.0 1.4

# system: processor med ben
$csys = New-Canvas 16; $g = $csys[1]
$g.DrawPath((New-Pen 1.25), (New-RoundRectPath (F 3.8) (F 3.8) (F 8.4) (F 8.4) (F 1.4)))
$g.FillRectangle([System.Drawing.Brushes]::White, (F 6.4), (F 6.4), (F 3.2), (F 3.2))
$pin = New-Pen 1.1
foreach ($p in @(6.4, 9.6)) {
    $g.DrawLine($pin, (F $p), (F 1.6), (F $p), (F 3.6)); $g.DrawLine($pin, (F $p), (F 12.4), (F $p), (F 14.4))
    $g.DrawLine($pin, (F 1.6), (F $p), (F 3.6), (F $p)); $g.DrawLine($pin, (F 12.4), (F $p), (F 14.4), (F $p))
}

# plugin: stickpropp med sladd
$cplug = New-Canvas 16; $g = $cplug[1]
$ppen = New-Pen 1.3
$g.DrawLine($ppen, (F 6.0), (F 1.8), (F 6.0), (F 4.6))
$g.DrawLine($ppen, (F 10.0), (F 1.8), (F 10.0), (F 4.6))
$bp = New-Object System.Drawing.Drawing2D.GraphicsPath
$bp.AddLine((F 3.6), (F 4.9), (F 12.4), (F 4.9))
$bp.AddLine((F 12.4), (F 4.9), (F 12.4), (F 7.4))
$bp.AddArc((F 3.6), (F 3.0), (F 8.8), (F 8.8), 0, 180)
$bp.CloseFigure()
$g.DrawPath($ppen, $bp)
$g.DrawLine($ppen, (F 8.0), (F 11.8), (F 8.0), (F 14.4))

# strom och USB: av/pa-symbolen
$cpower = New-Canvas 16; $g = $cpower[1]
$g.DrawArc((New-Pen 1.35), (F 2.3), (F 2.8), (F 11.4), (F 11.4), 310, 280)
$g.DrawLine((New-Pen 1.45), (F 8.0), (F 1.8), (F 8.0), (F 7.6))

# ---------------- Control Center (24 x 24, fyllda) ----------------

# USB-symbolen (treudd)
$ccusb = New-Canvas 24; $g = $ccusb[1]
$up = New-Pen 1.8
$g.DrawLine($up, (F 12.0), (F 5.5), (F 12.0), (F 19.0))
$g.FillPolygon([System.Drawing.Brushes]::White, (New-PointList @(@((F 12.0), (F 1.8)), @((F 14.6), (F 6.0)), @((F 9.4), (F 6.0)))))
Fill-Dot $g 12.0 19.8 2.4
$g.DrawLines($up, (New-PointList @(@((F 12.0), (F 15.2)), @((F 7.0), (F 12.2)), @((F 7.0), (F 9.6)))))
Fill-Dot $g 7.0 8.4 1.8
$g.DrawLines($up, (New-PointList @(@((F 12.0), (F 13.0)), @((F 17.0), (F 10.0)), @((F 17.0), (F 7.8)))))
$g.FillRectangle([System.Drawing.Brushes]::White, (F 15.3), (F 5.0), (F 3.4), (F 3.4))

# kugghjul med raka kuggar och hal
$ccgear = New-Canvas 24; $g = $ccgear[1]
$gp = New-Object System.Drawing.Drawing2D.GraphicsPath
$gp.AddPolygon((Get-GearPoints 12.0 12.0 7.6 10.3))
$gp.AddEllipse((F 8.6), (F 8.6), (F 6.8), (F 6.8))
$gp.FillMode = 'Alternate'
$g.FillPath([System.Drawing.Brushes]::White, $gp)

function Downsample($canvas) {
    $bmp = $canvas[0]; $N = $canvas[2]; $B = $N * $S
    $out = New-Object 'int[]' ($N * $N)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $B, $B
    $data = $bmp.LockBits($rect, 'ReadOnly', [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object 'byte[]' ($data.Stride * $B)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    for ($y = 0; $y -lt $N; $y++) { for ($x = 0; $x -lt $N; $x++) {
        $sum = 0
        for ($j = 0; $j -lt $S; $j++) { $row = ($y * $S + $j) * $data.Stride + $x * $S * 4
            for ($i = 0; $i -lt $S; $i++) { $sum += $bytes[$row + $i * 4 + 2] } }
        $out[$y * $N + $x] = [int][Math]::Round($sum / ($S * $S))
    } }
    return ,$out
}

$icons = @(
    @('ICON_SEARCH', $search, 'ICON_N'),
    @('ICON_GEAR', $gear, 'ICON_N'),
    @('ICON_TAB_GAMES', $games, 'ICON_N'),
    @('ICON_TAB_APPS', $apps, 'ICON_N'),
    @('ICON_TAB_LIBRARY', $library, 'ICON_N'),
    @('ICON_TAB_MUSIC', $music, 'ICON_N'),
    @('ICON_CAT_PSP5', $cpsp5, 'ICON_N'),
    @('ICON_CAT_THEME', $ctheme, 'ICON_N'),
    @('ICON_CAT_PERF', $cperf, 'ICON_N'),
    @('ICON_CAT_SYSTEM', $csys, 'ICON_N'),
    @('ICON_CAT_PLUGINS', $cplug, 'ICON_N'),
    @('ICON_CAT_POWER', $cpower, 'ICON_N'),
    @('ICON_CC_USB', $ccusb, 'ICON_CC_N'),
    @('ICON_CC_GEAR', $ccgear, 'ICON_CC_N')
)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* psp5 - made by 08Weber */')
[void]$sb.AppendLine('/* Generated by tools/make_icons.ps1 - do not edit by hand. Alpha per pixel (0-255). */')
[void]$sb.AppendLine('#define ICON_N 16')
[void]$sb.AppendLine('#define ICON_CC_N 24')
$preview = New-Object System.Drawing.Bitmap (24 * 8 * $icons.Count + ($icons.Count + 1) * 8), (24 * 8 + 16)
$pg = [System.Drawing.Graphics]::FromImage($preview); $pg.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$ox = 8
foreach ($icon in $icons) {
    $N = $icon[1][2]
    $a = Downsample $icon[1]
    [void]$sb.AppendLine("static const unsigned char $($icon[0])[$($icon[2]) * $($icon[2])] = {")
    for ($y = 0; $y -lt $N; $y++) { [void]$sb.AppendLine('    ' + (($a[($y * $N)..($y * $N + $N - 1)]) -join ',') + ',') }
    [void]$sb.AppendLine('};')
    for ($y = 0; $y -lt $N; $y++) { for ($x = 0; $x -lt $N; $x++) {
        $v = [int](30 + $a[$y * $N + $x] * 225 / 255)
        $pg.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($v, $v, $v + [int](4 * (255 - $a[$y * $N + $x]) / 255)))), $ox + $x * 8, 8 + $y * 8, 8, 8)
    } }
    $ox += 24 * 8 + 8
}
[System.IO.File]::WriteAllText((Join-Path $project 'src\icons.h'), $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
$preview.Save((Join-Path $env:TEMP 'psp5_icons_preview.png'))
'icons ok'
