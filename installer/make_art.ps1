# Generates the installer artwork (no game assets: plain shapes and text drawn with GDI+).
#   powershell -NoProfile -ExecutionPolicy Bypass -File installer/make_art.ps1
# Writes installer/art/wizard-<w>x<h>.bmp (welcome/finish panel) and installer/art/small-<n>.bmp (page header icon),
# each at 100 % and 200 % DPI; Inno Setup picks the best size.
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot 'art'
New-Item -ItemType Directory -Force $out | Out-Null

$bg = [System.Drawing.Color]::FromArgb(14, 14, 16)
$red = [System.Drawing.Color]::FromArgb(160, 24, 24)
$white = [System.Drawing.Color]::FromArgb(236, 236, 236)
$grey = [System.Drawing.Color]::FromArgb(140, 140, 146)

function Font([string]$name, [float]$size, [System.Drawing.FontStyle]$style) {
    $f = New-Object System.Drawing.Font($name, $size, $style, [System.Drawing.GraphicsUnit]::Pixel)
    if ($f.Name -ne $name) { $f = New-Object System.Drawing.Font('Segoe UI', $size, $style, [System.Drawing.GraphicsUnit]::Pixel) }
    return $f
}

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear($bg)
    return $bmp, $g
}

function Wizard([int]$w, [int]$h) {
    $s = $w / 164.0
    $bmp, $g = New-Canvas $w $h
    $brushRed = New-Object System.Drawing.SolidBrush($red)
    # red accent bar on the left and a thin rule
    $g.FillRectangle($brushRed, 0, 0, [int](10 * $s), $h)
    $g.FillRectangle($brushRed, [int](24 * $s), [int](150 * $s), [int](116 * $s), [int](3 * $s))
    $title = Font 'Copperplate Gothic Bold' (21 * $s) ([System.Drawing.FontStyle]::Bold)
    $sub = Font 'Copperplate Gothic Bold' (15 * $s) ([System.Drawing.FontStyle]::Bold)
    $small = Font 'Segoe UI' (10.5 * $s) ([System.Drawing.FontStyle]::Regular)
    $g.DrawString('HITMAN 2', $title, (New-Object System.Drawing.SolidBrush($white)), [float](22 * $s), [float](96 * $s))
    $g.DrawString('ENHANCED', $sub, $brushRed, [float](24 * $s), [float](124 * $s))
    $rect = New-Object System.Drawing.RectangleF([float](24 * $s), [float](236 * $s), [float](132 * $s), [float](70 * $s))
    $g.DrawString("Unofficial fixes for`nHitman 2: Silent Assassin`n(Steam)", $small, (New-Object System.Drawing.SolidBrush($grey)), $rect)
    $bmp.Save((Join-Path $out "wizard-${w}x${h}.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
    $g.Dispose(); $bmp.Dispose()
}

function Small([int]$n) {
    $s = $n / 55.0
    $bmp, $g = New-Canvas $n $n
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($red)), 0, [int](44 * $s), $n, [int](11 * $s))
    $f = Font 'Copperplate Gothic Bold' (22 * $s) ([System.Drawing.FontStyle]::Bold)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $g.DrawString('H2', $f, (New-Object System.Drawing.SolidBrush($white)), (New-Object System.Drawing.RectangleF(0, 0, $n, [float](44 * $s))), $fmt)
    $bmp.Save((Join-Path $out "small-$n.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
    $g.Dispose(); $bmp.Dispose()
}

# GitHub social preview (Settings > Social preview), 1280x640 PNG.
function Social {
    $w = 1280; $h = 640
    $bmp, $g = New-Canvas $w $h
    $brushRed = New-Object System.Drawing.SolidBrush($red)
    $brushWhite = New-Object System.Drawing.SolidBrush($white)
    $brushGrey = New-Object System.Drawing.SolidBrush($grey)
    $g.FillRectangle($brushRed, 0, 0, 28, $h)
    $g.FillRectangle($brushRed, 110, 318, 760, 6)
    $g.DrawString('HITMAN 2', (Font 'Copperplate Gothic Bold' 120 ([System.Drawing.FontStyle]::Bold)), $brushWhite, [float]100, [float]120)
    $g.DrawString('ENHANCED', (Font 'Copperplate Gothic Bold' 72 ([System.Drawing.FontStyle]::Bold)), $brushRed, [float]108, [float]232)
    $g.DrawString('Widescreen, high-FPS and menu fix for Hitman 2: Silent Assassin (PC)', (Font 'Segoe UI' 30 ([System.Drawing.FontStyle]::Regular)), $brushWhite, [float]110, [float]350)
    $g.DrawString('1080p / 1440p / 4K fullscreen  -  full-screen menus  -  FPS limit  -  correct game speed', (Font 'Segoe UI' 24 ([System.Drawing.FontStyle]::Regular)), $brushGrey, [float]110, [float]410)
    $g.DrawString('HD fonts  -  in-game settings (F10)  -  one-click installer  -  free & open source', (Font 'Segoe UI' 24 ([System.Drawing.FontStyle]::Regular)), $brushGrey, [float]110, [float]450)
    $g.DrawString('github.com/VictorG238/Hitman2Enhanced', (Font 'Segoe UI' 26 ([System.Drawing.FontStyle]::Bold)), $brushRed, [float]110, [float]540)
    $bmp.Save((Join-Path $out 'social-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

Wizard 164 314
Wizard 328 628
Social
Small 55
Small 110
Get-ChildItem $out | ForEach-Object { "$($_.Name) $($_.Length)" }
