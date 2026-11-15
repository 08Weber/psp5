# psp5 - made by 08Weber
# Ritar topprandens ikoner (sok och installningar) 8 ganger storre och skalar ner med medelvarde,
# sa att de blir mjukt kantutjamnade i PSP:ns laga upplosning. Sparas som alfakartor i src/icons.h.
# Kors i Windows: powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
Add-Type -AssemblyName System.Drawing
$project = Split-Path $PSScriptRoot -Parent
$N = 16; $S = 8; $B = $N * $S  # ikonstorlek, overskalning, storlek vid ritning

function New-Canvas {
    $bmp = New-Object System.Drawing.Bitmap $B, $B
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black); $g.SmoothingMode = 'AntiAlias'
    return @($bmp, $g)
}
function New-Pen($w) { $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([float]($w * $S)); $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p.LineJoin = 'Round'; return $p }
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

# forstoringsglas: ring och skaft
$search = New-Canvas; $g = $search[1]
$g.DrawEllipse((New-Pen 1.7), [float](2.2 * $S), [float](2.2 * $S), [float](8.6 * $S), [float](8.6 * $S))
$g.DrawLine((New-Pen 2.0), [float](10.0 * $S), [float](10.0 * $S), [float](13.6 * $S), [float](13.6 * $S))

# kugghjul: 8 kuggar, hal i mitten
$gear = New-Canvas; $g = $gear[1]
$c = $B / 2; $path = New-Object System.Drawing.Drawing2D.GraphicsPath
$pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
for ($k = 0; $k -lt 8; $k++) {
    $a = $k * [Math]::PI / 4
    foreach ($d in @(@(-0.30, 5.0), @(-0.19, 7.0), @(0.19, 7.0), @(0.30, 5.0))) {
        $pts.Add((New-Object System.Drawing.PointF ([float]($c + [Math]::Cos($a + $d[0]) * $d[1] * $S)), ([float]($c + [Math]::Sin($a + $d[0]) * $d[1] * $S))))
    }
    $mid = $a + [Math]::PI / 8
    $pts.Add((New-Object System.Drawing.PointF ([float]($c + [Math]::Cos($mid) * 5.0 * $S)), ([float]($c + [Math]::Sin($mid) * 5.0 * $S))))
}
$path.AddPolygon($pts.ToArray())
$path.AddEllipse([float]($c - 2.3 * $S), [float]($c - 2.3 * $S), [float](4.6 * $S), [float](4.6 * $S))
$path.FillMode = 'Alternate'
$g.FillPath([System.Drawing.Brushes]::White, $path)

function New-PointList($pairs) {
    $list = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($pt in $pairs) {
        $list.Add((New-Object System.Drawing.PointF ([float]$pt[0]), ([float]$pt[1])))
    }
    return ,$list.ToArray()
}

# handkontroll (Games): rundad kapsel med genomskinligt kryss och knapphal
$games = New-Canvas; $g = $games[1]
$gpath = New-RoundRectPath (1.0 * $S) (3.8 * $S) (14.0 * $S) (8.4 * $S) (4.2 * $S)
$gx = 4.9 * $S; $gy = 8.0 * $S; $arm = 2.0 * $S; $ht = 0.65 * $S
[System.Drawing.PointF[]]$dpad = New-PointList @(
    @(($gx - $ht), ($gy - $arm)), @(($gx + $ht), ($gy - $arm)),
    @(($gx + $ht), ($gy - $ht)),  @(($gx + $arm), ($gy - $ht)),
    @(($gx + $arm), ($gy + $ht)), @(($gx + $ht), ($gy + $ht)),
    @(($gx + $ht), ($gy + $arm)), @(($gx - $ht), ($gy + $arm)),
    @(($gx - $ht), ($gy + $ht)),  @(($gx - $arm), ($gy + $ht)),
    @(($gx - $arm), ($gy - $ht)), @(($gx - $ht), ($gy - $ht))
)
$gpath.AddPolygon($dpad)
$gpath.AddEllipse([float](11.15 * $S), [float](5.65 * $S), [float](1.5 * $S), [float](1.5 * $S))
$gpath.AddEllipse([float](9.35 * $S), [float](8.65 * $S), [float](1.5 * $S), [float](1.5 * $S))
$gpath.FillMode = 'Alternate'
$g.FillPath([System.Drawing.Brushes]::White, $gpath)

# appar (Apps): 2x2 konturrutnat
$apps = New-Canvas; $g = $apps[1]
$apen = New-Pen 1.35
foreach ($ry in @(2.5, 9.5)) {
    foreach ($rx in @(2.5, 9.5)) {
        $rp = New-RoundRectPath ($rx * $S) ($ry * $S) (4.0 * $S) (4.0 * $S) (0.7 * $S)
        $g.DrawPath($apen, $rp)
    }
}

# bibliotek (Library): tre staplade lager
$library = New-Canvas; $g = $library[1]
$lpen = New-Pen 1.15
[System.Drawing.PointF[]]$topDiamond = New-PointList @(@((8.0 * $S), (2.4 * $S)), @((13.3 * $S), (5.0 * $S)), @((8.0 * $S), (7.6 * $S)), @((2.7 * $S), (5.0 * $S)))
$g.DrawPolygon($lpen, $topDiamond)
[System.Drawing.PointF[]]$midLayer = New-PointList @(@((2.7 * $S), (8.1 * $S)), @((8.0 * $S), (10.7 * $S)), @((13.3 * $S), (8.1 * $S)))
$g.DrawLines($lpen, $midLayer)
[System.Drawing.PointF[]]$botLayer = New-PointList @(@((2.7 * $S), (11.1 * $S)), @((8.0 * $S), (13.7 * $S)), @((13.3 * $S), (11.1 * $S)))
$g.DrawLines($lpen, $botLayer)

# musik (Music): dubbla attondelsnoter
$music = New-Canvas; $g = $music[1]
$mpen = New-Pen 1.2
$bpen = New-Pen 1.4
$g.DrawEllipse($mpen, [float](2.6 * $S), [float](9.8 * $S), [float](3.5 * $S), [float](3.5 * $S))
$g.DrawEllipse($mpen, [float](9.4 * $S), [float](8.7 * $S), [float](3.5 * $S), [float](3.5 * $S))
$g.DrawLine($mpen, [float](6.1 * $S), [float](11.5 * $S), [float](6.1 * $S), [float](3.9 * $S))
$g.DrawLine($mpen, [float](12.9 * $S), [float](10.4 * $S), [float](12.9 * $S), [float](2.8 * $S))
$g.DrawLine($bpen, [float](6.1 * $S), [float](3.9 * $S), [float](12.9 * $S), [float](2.8 * $S))

function Downsample($bmp) {
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
    @('ICON_SEARCH', $search[0]),
    @('ICON_GEAR', $gear[0]),
    @('ICON_TAB_GAMES', $games[0]),
    @('ICON_TAB_APPS', $apps[0]),
    @('ICON_TAB_LIBRARY', $library[0]),
    @('ICON_TAB_MUSIC', $music[0])
)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* psp5 - made by 08Weber */')
[void]$sb.AppendLine('/* Generated by tools/make_icons.ps1 - do not edit by hand. Alpha per pixel (0-255). */')
[void]$sb.AppendLine("#define ICON_N $N")
$preview = New-Object System.Drawing.Bitmap ($N * $icons.Count * 8 + ($icons.Count + 1) * 8), ($N * 8 + 16)
$pg = [System.Drawing.Graphics]::FromImage($preview); $pg.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$ox = 8
foreach ($icon in $icons) {
    $a = Downsample $icon[1]
    [void]$sb.AppendLine("static const unsigned char $($icon[0])[ICON_N * ICON_N] = {")
    for ($y = 0; $y -lt $N; $y++) { [void]$sb.AppendLine('    ' + (($a[($y * $N)..($y * $N + $N - 1)]) -join ',') + ',') }
    [void]$sb.AppendLine('};')
    for ($y = 0; $y -lt $N; $y++) { for ($x = 0; $x -lt $N; $x++) {
        $v = [int](30 + $a[$y * $N + $x] * 225 / 255)
        $pg.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($v, $v, $v + [int](4 * (255 - $a[$y * $N + $x]) / 255)))), $ox + $x * 8, 8 + $y * 8, 8, 8)
    } }
    $ox += $N * 8 + 8
}
[System.IO.File]::WriteAllText((Join-Path $project 'src\icons.h'), $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
$preview.Save((Join-Path $env:TEMP 'psp5_icons_preview.png'))
'icons ok'
