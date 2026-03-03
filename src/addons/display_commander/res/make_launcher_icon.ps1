# Generate launcher_icon.ico for Display Commander Launcher exe.
# Run from project root or pass -ResDir. Uses .NET System.Drawing (no ImageMagick required).
param([string]$ResDir = (Split-Path -Parent $MyInvocation.MyCommand.Path))

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$icoPath = Join-Path $ResDir "launcher_icon.ico"

# Prefer ImageMagick if available (multi-size from vector)
$magick = Get-Command magick -ErrorAction SilentlyContinue
if ($magick) {
    Push-Location $ResDir
    try {
        & magick convert launcher_icon.svg -define icon:auto-resize=256,128,64,48,32,16 launcher_icon.ico
        if (Test-Path launcher_icon.ico) { Write-Host "Created $icoPath (ImageMagick)"; exit 0 }
    } finally { Pop-Location }
}

# Fallback: draw 32x32 icon with .NET (monitor frame + play triangle)
$size = 32
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

$blue = [System.Drawing.Color]::FromArgb(255, 29, 78, 216)
$dark = [System.Drawing.Color]::FromArgb(255, 30, 64, 175)
$light = [System.Drawing.Color]::FromArgb(255, 147, 197, 253)

$g.FillRectangle((New-Object System.Drawing.SolidBrush($blue)), 2, 4, 28, 20)
$g.FillRectangle((New-Object System.Drawing.SolidBrush($dark)), 4, 6, 24, 14)
$tri = @([System.Drawing.Point]::new(14, 10), [System.Drawing.Point]::new(14, 18), [System.Drawing.Point]::new(22, 14))
$g.FillPolygon((New-Object System.Drawing.SolidBrush($light)), $tri)

$g.Dispose()
$icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
$fs = [System.IO.File]::Create($icoPath)
$icon.Save($fs)
$fs.Close()
$icon.Dispose()
$bmp.Dispose()

Write-Host "Created $icoPath (.NET fallback)"
