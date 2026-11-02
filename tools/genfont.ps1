# =============================================================================
# tools/genfont.ps1
# Generates include/font8x16.h  --  an 8x16 monochrome bitmap font used by the
# Altitude OS framebuffer text renderer.
#
# The font is rasterised from Cascadia Mono (fallback: Consolas) on Windows.
# Each glyph is stored as 16 bytes; bit 7 of byte 0 is the left-most pixel of
# the top row.  Only printable ASCII (32..126) is included.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File tools/genfont.ps1
# =============================================================================

param(
    [string]$OutFile = "$PSScriptRoot\..\include\font8x16.h",
    [string]$PreviewFile = "$PSScriptRoot\..\build\font_preview.png"
)

Add-Type -AssemblyName System.Drawing

$W = 8
$H = 16
$First = 32
$Last = 126

$families = [System.Drawing.FontFamily]::Families | ForEach-Object { $_.Name }
$familyName = if ($families -contains 'Cascadia Mono') { 'Cascadia Mono' }
              elseif ($families -contains 'Consolas') { 'Consolas' }
              else { 'Courier New' }

$font = New-Object System.Drawing.Font($familyName, 12.0, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fmt = [System.Drawing.StringFormat]::GenericTypographic
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip

$glyphs = New-Object 'System.Byte[,]' ($Last - $First + 1), $H
$bmp = New-Object System.Drawing.Bitmap 8, 16
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Black)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$brush = [System.Drawing.Brushes]::White

for ($code = $First; $code -le $Last; $code++) {
    $ch = [char]$code
    $g.Clear([System.Drawing.Color]::Black)
    $size = $g.MeasureString([string]$ch, $font, 100, $fmt)
    # Centre horizontally, and vertically place the baseline consistently.
    $x = [math]::Floor(($W - $size.Width) / 2.0)
    if ($x -lt -1) { $x = -1 }
    # Tune the baseline: caps top at row 3, descenders use rows 12..14.
    $y = 2
    $g.DrawString([string]$ch, $font, $brush, [float]$x, [float]$y, $fmt)

    $idx = $code - $First
    for ($row = 0; $row -lt $H; $row++) {
        $bits = 0
        for ($col = 0; $col -lt $W; $col++) {
            $p = $bmp.GetPixel($col, $row)
            if ($p.R -gt 96) { $bits = $bits -bor (1 -shl (7 - $col)) }
        }
        $glyphs[$idx, $row] = [byte]$bits
    }
}

$g.Dispose()
$bmp.Dispose()
$font.Dispose()

# â”€â”€ Emit C header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* ============================================================================')
[void]$sb.AppendLine(' * include/font8x16.h  -  Generated 8x16 bitmap font (ASCII 32..126)')
[void]$sb.AppendLine(' *')
[void]$sb.AppendLine(" * Rasterised from $familyName by tools/genfont.ps1.  Do not edit by hand.")
[void]$sb.AppendLine(' * Each glyph is 16 bytes; MSB of each byte is the left-most pixel.')
[void]$sb.AppendLine(' * ============================================================================ */')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#ifndef FONT8X16_H')
[void]$sb.AppendLine('#define FONT8X16_H')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#define FONT_FIRST_CHAR 32')
[void]$sb.AppendLine('#define FONT_LAST_CHAR  126')
[void]$sb.AppendLine('#define FONT_GLYPH_W    8')
[void]$sb.AppendLine('#define FONT_GLYPH_H    16')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const unsigned char font8x16[FONT_LAST_CHAR - FONT_FIRST_CHAR + 1][FONT_GLYPH_H] = {')
for ($i = 0; $i -le ($Last - $First); $i++) {
    $code = $First + $i
    $values = for ($row = 0; $row -lt $H; $row++) { '0x{0:X2}' -f $glyphs[$i, $row] }
    $label = if ($code -eq 32) { 'space' } else { ([char]$code) }
    [void]$sb.AppendLine(('    {{ {0} }},  /* {1,3}  {2} */' -f ($values -join ','), $code, $label))
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#endif /* FONT8X16_H */')

$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
[System.IO.File]::WriteAllText($OutFile, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Wrote $OutFile using $familyName"

# â”€â”€ Optional preview sheet (3x scale) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$cols = 16
$rows = [math]::Ceiling(($Last - $First + 1) / $cols)
$scale = 3
$pad = 4
$sheet = New-Object System.Drawing.Bitmap (($W * $scale + $pad) * $cols + $pad), (($H * $scale + $pad) * $rows + $pad)
$sg = [System.Drawing.Graphics]::FromImage($sheet)
$sg.Clear([System.Drawing.Color]::FromArgb(12, 16, 30))
$cellBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(56, 189, 248))
for ($i = 0; $i -le ($Last - $First); $i++) {
    $cx = $pad + ($i % $cols) * ($W * $scale + $pad)
    $cy = $pad + [math]::Floor($i / $cols) * ($H * $scale + $pad)
    for ($row = 0; $row -lt $H; $row++) {
        $bits = $glyphs[$i, $row]
        for ($col = 0; $col -lt $W; $col++) {
            if (($bits -band (1 -shl (7 - $col))) -ne 0) {
                $sg.FillRectangle($cellBrush, $cx + $col * $scale, $cy + $row * $scale, $scale, $scale)
            }
        }
    }
}
$sg.Dispose()
$pdir = Split-Path -Parent $PreviewFile
if (-not (Test-Path $pdir)) { New-Item -ItemType Directory -Path $pdir -Force | Out-Null }
$sheet.Save($PreviewFile, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "Wrote preview $PreviewFile"
