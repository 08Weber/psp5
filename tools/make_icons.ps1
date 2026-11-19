# psp5 - made by 08Weber
# Ritar ikonerna 8 ganger storre och skalar ner med medelvarde, sa att de blir mjukt
# kantutjamnade i PSP:ns laga upplosning. Sparas som alfakartor i src/icons.h.
# Topprad och installningar: 16 x 16, tunna konturer (som Figma-designen).
# Control Center: 24 x 24, fyllda former (som ovriga CC-ikoner).
# Kors i Windows: powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
Add-Type -AssemblyName System.Drawing
$project = Split-Path $PSScriptRoot -Parent
$S = 8  # overskalning

function New-Canvas($w, $h = 0) {
    if ($h -le 0) { $h = $w }
    $bmp = New-Object System.Drawing.Bitmap ($w * $S), ($h * $S)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black); $g.SmoothingMode = 'AntiAlias'
    return @($bmp, $g, $w, $h)
}
function New-Pen($w, $cap = 'Round', $join = 'Round') {
    $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([float]($w * $S))
    $p.StartCap = $cap; $p.EndCap = $cap; $p.LineJoin = $join
    return $p
}
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

# ---------------- PlayStation controller-ikoner (fran icons.figcraft) ----------------

# outline-triangle (16 x 16): yttre ring r=6.0 + skarp triangel inuti
$outtri = New-Canvas 16; $g = $outtri[1]
$g.DrawEllipse((New-Pen 1.2 'Flat' 'Miter'), (F 2.0), (F 2.0), (F 12.0), (F 12.0))
$g.DrawPolygon((New-Pen 1.15 'Flat' 'Miter'), (New-PointList @(@((F 8.0), (F 4.7)), @((F 10.7), (F 9.7)), @((F 5.3), (F 9.7)))))

# L / R axelknappar (18 x 13): figmas rundade kapselform + tydlig L/R-bokstav
$shldplate = New-Canvas 18 13; $g = $shldplate[1]
$sp = New-Object System.Drawing.Drawing2D.GraphicsPath
$sp.AddArc((F 0.0), (F 0.0), (F 2.4), (F 2.4), 180, 90)
$sp.AddArc((F 15.6), (F 0.0), (F 2.4), (F 2.4), 270, 90)
$sp.AddArc((F 5.0), (F 0.0), (F 13.0), (F 13.0), 0, 90)
$sp.AddArc((F 0.0), (F 0.0), (F 13.0), (F 13.0), 90, 90)
$sp.CloseFigure()
$g.FillPath([System.Drawing.Brushes]::White, $sp)

$shldl = New-Canvas 18 13; $g = $shldl[1]
$g.DrawLines((New-Pen 1.65 'Square' 'Miter'), (New-PointList @(@((F 7.2), (F 2.8)), @((F 7.2), (F 8.9)), @((F 11.4), (F 8.9)))))

$shldr = New-Canvas 18 13; $g = $shldr[1]
$rpen = New-Pen 1.55 'Square' 'Miter'
$g.DrawLine($rpen, (F 6.8), (F 2.8), (F 6.8), (F 9.0))
$rarc = New-Object System.Drawing.Drawing2D.GraphicsPath
$rarc.AddLine((F 6.8), (F 2.8), (F 9.3), (F 2.8))
$rarc.AddArc((F 7.6), (F 2.8), (F 3.4), (F 3.4), 270, 180)
$rarc.AddLine((F 9.3), (F 6.2), (F 6.8), (F 6.2))
$g.DrawPath($rpen, $rarc)
$g.DrawLine($rpen, (F 8.9), (F 6.2), (F 11.1), (F 9.0))

# PlayStation-logotyp (16 x 16, fran plain-small-PS)
$pslogo = New-Canvas 16; $g = $pslogo[1]
$psp = New-Object System.Drawing.Drawing2D.GraphicsPath
$psp.StartFigure()
$psp.AddBezier((F 10.4989), (F 8.7642), (F 10.9144), (F 8.7642), (F 11.2626), (F 8.5961), (F 11.5059), (F 8.2785))
$psp.AddBezier((F 11.5059), (F 8.2785), (F 11.7691), (F 7.9352), (F 11.9076), (F 7.4208), (F 11.9076), (F 6.7906))
$psp.AddBezier((F 11.9076), (F 6.7906), (F 11.9076), (F 4.9673), (F 11.2893), (F 4.1709), (F 9.3622), (F 3.5100))
$psp.AddBezier((F 9.3622), (F 3.5100), (F 8.7518), (F 3.3033), (F 7.3787), (F 2.8769), (F 6.3469), (F 2.6681))
$psp.AddLine((F 6.3469), (F 2.6681), (F 6.3469), (F 12.6859))
$psp.AddLine((F 6.3469), (F 12.6859), (F 8.3828), (F 13.3319))
$psp.AddLine((F 8.3828), (F 13.3319), (F 8.3828), (F 4.8911))
$psp.AddBezier((F 8.3828), (F 4.8911), (F 8.3828), (F 4.3687), (F 8.6757), (F 4.0452), (F 9.0486), (F 4.1689))
$psp.AddBezier((F 9.0486), (F 4.1689), (F 9.6066), (F 4.3232), (F 9.6066), (F 4.9554), (F 9.6066), (F 5.1938))
$psp.AddLine((F 9.6066), (F 5.1938), (F 9.6066), (F 8.5515))
$psp.AddBezier((F 9.6066), (F 8.5515), (F 9.9231), (F 8.6920), (F 10.2229), (F 8.7642), (F 10.4989), (F 8.7642))
$psp.CloseFigure()
$psp.StartFigure()
$psp.AddBezier((F 14.9189), (F 10.8517), (F 15.0069), (F 10.7409), (F 15.0346), (F 10.6261), (F 15.0000), (F 10.5123))
$psp.AddBezier((F 15.0000), (F 10.5123), (F 14.9575), (F 10.3729), (F 14.7754), (F 10.1038), (F 14.0454), (F 9.8723))
$psp.AddBezier((F 14.0454), (F 9.8723), (F 13.1372), (F 9.5280), (F 11.8917), (F 9.3835), (F 10.8718), (F 9.4934))
$psp.AddBezier((F 10.8718), (F 9.4934), (F 9.8875), (F 9.6022), (F 9.1742), (F 9.8515), (F 9.1663), (F 9.8545))
$psp.AddLine((F 9.1663), (F 9.8545), (F 9.1218), (F 9.8693))
$psp.AddLine((F 9.1218), (F 9.8693), (F 9.1218), (F 10.9536))
$psp.AddLine((F 9.1218), (F 10.9536), (F 11.4218), (F 10.1433))
$psp.AddBezier((F 11.4218), (F 10.1433), (F 11.8631), (F 9.9850), (F 12.5456), (F 9.9494), (F 12.9146), (F 10.0701))
$psp.AddBezier((F 12.9146), (F 10.0701), (F 13.1798), (F 10.1572), (F 13.2371), (F 10.2967), (F 13.2381), (F 10.3986))
$psp.AddBezier((F 13.2381), (F 10.3986), (F 13.2391), (F 10.5203), (F 13.1600), (F 10.6934), (F 12.7732), (F 10.8319))
$psp.AddLine((F 12.7732), (F 10.8319), (F 9.1218), (F 12.1338))
$psp.AddLine((F 9.1218), (F 12.1338), (F 9.1218), (F 13.1677))
$psp.AddLine((F 9.1218), (F 13.1677), (F 14.0187), (F 11.4097))
$psp.AddBezier((F 14.0187), (F 11.4097), (F 14.0246), (F 11.4077), (F 14.6686), (F 11.1673), (F 14.9189), (F 10.8517))
$psp.CloseFigure()
$psp.StartFigure()
$psp.AddLine((F 5.5981), (F 8.9295), (F 2.4522), (F 10.0477))
$psp.AddBezier((F 2.4522), (F 10.0477), (F 2.4414), (F 10.0517), (F 1.6579), (F 10.3119), (F 1.2058), (F 10.6464))
$psp.AddBezier((F 1.2058), (F 10.6464), (F 1.0455), (F 10.7652), (F 0.9733), (F 10.9116), (F 1.0000), (F 11.0591))
$psp.AddBezier((F 1.0000), (F 11.0591), (F 1.0505), (F 11.3342), (F 1.4105), (F 11.5905), (F 1.9616), (F 11.7459))
$psp.AddBezier((F 1.9616), (F 11.7459), (F 3.1140), (F 12.1269), (F 4.3229), (F 12.2239), (F 5.4912), (F 12.0319))
$psp.AddLine((F 5.4912), (F 12.0319), (F 5.5971), (F 12.0141))
$psp.AddLine((F 5.5971), (F 12.0141), (F 5.5971), (F 11.1392))
$psp.AddLine((F 5.5971), (F 11.1392), (F 4.6425), (F 11.4856))
$psp.AddBezier((F 4.6425), (F 11.4856), (F 4.2012), (F 11.6439), (F 3.5177), (F 11.6796), (F 3.1487), (F 11.5588))
$psp.AddBezier((F 3.1487), (F 11.5588), (F 2.8835), (F 11.4718), (F 2.8262), (F 11.3322), (F 2.8252), (F 11.2303))
$psp.AddBezier((F 2.8252), (F 11.2303), (F 2.8232), (F 11.1086), (F 2.9033), (F 10.9354), (F 3.2911), (F 10.7959))
$psp.AddLine((F 3.2911), (F 10.7959), (F 5.5981), (F 9.9695))
$psp.AddLine((F 5.5981), (F 9.9695), (F 5.5981), (F 8.9295))
$psp.CloseFigure()
$g.FillPath([System.Drawing.Brushes]::White, $psp)

# Knappsymboler inuti 24 x 24-cirkel (anvands av gfx_button i src/gfx.c)
$symcross = New-Canvas 24; $g = $symcross[1]
$cp = New-Pen 2.5 'Flat' 'Miter'
$g.DrawLine($cp, (F 8.0), (F 8.0), (F 16.0), (F 16.0))
$g.DrawLine($cp, (F 16.0), (F 8.0), (F 8.0), (F 16.0))

$symcircle = New-Canvas 24; $g = $symcircle[1]
$g.DrawEllipse((New-Pen 2.5 'Flat' 'Miter'), (F 6.25), (F 6.25), (F 11.5), (F 11.5))

$symtri = New-Canvas 24; $g = $symtri[1]
$g.DrawPolygon((New-Pen 2.5 'Flat' 'Miter'), (New-PointList @(@((F 12.0), (F 6.2)), @((F 17.8), (F 16.2)), @((F 6.2), (F 16.2)))))

$symsq = New-Canvas 24; $g = $symsq[1]
$g.DrawRectangle((New-Pen 2.5 'Flat' 'Miter'), (F 7.25), (F 7.25), (F 9.5), (F 9.5))

function Downsample($canvas) {
    $bmp = $canvas[0]; $W = $canvas[2]; $H = $canvas[3]; $BW = $W * $S; $BH = $H * $S
    $out = New-Object 'int[]' ($W * $H)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $BW, $BH
    $data = $bmp.LockBits($rect, 'ReadOnly', [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object 'byte[]' ($data.Stride * $BH)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    for ($y = 0; $y -lt $H; $y++) { for ($x = 0; $x -lt $W; $x++) {
        $sum = 0
        for ($j = 0; $j -lt $S; $j++) { $row = ($y * $S + $j) * $data.Stride + $x * $S * 4
            for ($i = 0; $i -lt $S; $i++) { $sum += $bytes[$row + $i * 4 + 2] } }
        $out[$y * $W + $x] = [int][Math]::Round($sum / ($S * $S))
    } }
    return ,$out
}

$icons = @(
    @('ICON_SEARCH', $search, 'ICON_N * ICON_N'),
    @('ICON_GEAR', $gear, 'ICON_N * ICON_N'),
    @('ICON_TAB_GAMES', $games, 'ICON_N * ICON_N'),
    @('ICON_TAB_APPS', $apps, 'ICON_N * ICON_N'),
    @('ICON_TAB_LIBRARY', $library, 'ICON_N * ICON_N'),
    @('ICON_TAB_MUSIC', $music, 'ICON_N * ICON_N'),
    @('ICON_CAT_PSP5', $cpsp5, 'ICON_N * ICON_N'),
    @('ICON_CAT_THEME', $ctheme, 'ICON_N * ICON_N'),
    @('ICON_CAT_PERF', $cperf, 'ICON_N * ICON_N'),
    @('ICON_CAT_SYSTEM', $csys, 'ICON_N * ICON_N'),
    @('ICON_CAT_PLUGINS', $cplug, 'ICON_N * ICON_N'),
    @('ICON_CAT_POWER', $cpower, 'ICON_N * ICON_N'),
    @('ICON_CC_USB', $ccusb, 'ICON_CC_N * ICON_CC_N'),
    @('ICON_CC_GEAR', $ccgear, 'ICON_CC_N * ICON_CC_N'),
    @('ICON_BTN_OUTLINE_TRI', $outtri, 'ICON_N * ICON_N'),
    @('ICON_SHLD_PLATE', $shldplate, 'ICON_SHLD_W * ICON_SHLD_H'),
    @('ICON_SHLD_L', $shldl, 'ICON_SHLD_W * ICON_SHLD_H'),
    @('ICON_SHLD_R', $shldr, 'ICON_SHLD_W * ICON_SHLD_H'),
    @('ICON_PS_LOGO', $pslogo, 'ICON_N * ICON_N')
)
$btnIcons = @(
    @('ICON_SYM_CROSS', $symcross, 'ICON_SYM_N * ICON_SYM_N'),
    @('ICON_SYM_CIRCLE', $symcircle, 'ICON_SYM_N * ICON_SYM_N'),
    @('ICON_SYM_TRIANGLE', $symtri, 'ICON_SYM_N * ICON_SYM_N'),
    @('ICON_SYM_SQUARE', $symsq, 'ICON_SYM_N * ICON_SYM_N')
)
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* psp5 - made by 08Weber */')
[void]$sb.AppendLine('/* Generated by tools/make_icons.ps1 - do not edit by hand. Alpha per pixel (0-255). */')
[void]$sb.AppendLine('#define ICON_N 16')
[void]$sb.AppendLine('#define ICON_CC_N 24')
[void]$sb.AppendLine('#define ICON_SHLD_W 18')
[void]$sb.AppendLine('#define ICON_SHLD_H 13')
[void]$sb.AppendLine('#define ICON_SYM_N 24')
[void]$sb.AppendLine('#ifndef ICONS_ONLY_BUTTONS')
$allCount = $icons.Count + $btnIcons.Count
$preview = New-Object System.Drawing.Bitmap (24 * 8 * $allCount + ($allCount + 1) * 8), (24 * 8 + 16)
$pg = [System.Drawing.Graphics]::FromImage($preview); $pg.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$ox = 8
foreach ($icon in $icons) {
    $W = $icon[1][2]; $H = $icon[1][3]
    $a = Downsample $icon[1]
    [void]$sb.AppendLine("static const unsigned char $($icon[0])[$($icon[2])] = {")
    for ($y = 0; $y -lt $H; $y++) { [void]$sb.AppendLine('    ' + (($a[($y * $W)..($y * $W + $W - 1)]) -join ',') + ',') }
    [void]$sb.AppendLine('};')
    for ($y = 0; $y -lt $H; $y++) { for ($x = 0; $x -lt $W; $x++) {
        $v = [int](30 + $a[$y * $W + $x] * 225 / 255)
        $pg.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($v, $v, $v + [int](4 * (255 - $a[$y * $W + $x]) / 255)))), $ox + $x * 8, 8 + $y * 8, 8, 8)
    } }
    $ox += 24 * 8 + 8
}
[void]$sb.AppendLine('#else')
foreach ($icon in $btnIcons) {
    $W = $icon[1][2]; $H = $icon[1][3]
    $a = Downsample $icon[1]
    [void]$sb.AppendLine("static const unsigned char $($icon[0])[$($icon[2])] = {")
    for ($y = 0; $y -lt $H; $y++) { [void]$sb.AppendLine('    ' + (($a[($y * $W)..($y * $W + $W - 1)]) -join ',') + ',') }
    [void]$sb.AppendLine('};')
    for ($y = 0; $y -lt $H; $y++) { for ($x = 0; $x -lt $W; $x++) {
        $v = [int](30 + $a[$y * $W + $x] * 225 / 255)
        $pg.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($v, $v, $v + [int](4 * (255 - $a[$y * $W + $x]) / 255)))), $ox + $x * 8, 8 + $y * 8, 8, 8)
    } }
    $ox += 24 * 8 + 8
}
[void]$sb.AppendLine('#endif')
[System.IO.File]::WriteAllText((Join-Path $project 'src\icons.h'), $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
$preview.Save((Join-Path $env:TEMP 'psp5_icons_preview.png'))
'icons ok'
