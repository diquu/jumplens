param(
    [string]$Png = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Png -eq "") { $Png = Join-Path $root "data\logo.png" }
if ($Out -eq "") { $Out = Join-Path $root "src\ui\logo_data.h" }

$source = [System.Drawing.Bitmap]::FromFile($Png)
$size = 128
$image = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($image)
$gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$gfx.DrawImage($source, 0, 0, $size, $size)
$gfx.Dispose()
$source.Dispose()

$rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
$data = $image.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $size)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$image.UnlockBits($data)
$image.Dispose()

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("#include <cstdint>")
$lines.Add("")
$lines.Add("namespace jl {")
$lines.Add("")
$lines.Add("inline const int kLogoWidth = $size;")
$lines.Add("inline const int kLogoHeight = $size;")
$lines.Add("")
$lines.Add("inline const uint32_t kLogoPixels[] = {")
for ($i = 0; $i -lt $size * $size; $i++) {
    $index = $i * 4
    $b = $bytes[$index]
    $g = $bytes[$index + 1]
    $r = $bytes[$index + 2]
    $a = $bytes[$index + 3]
    if ($a -lt 255) {
        $r = [int]($r * $a / 255)
        $g = [int]($g * $a / 255)
        $b = [int]($b * $a / 255)
    }
    $value = ([long]$a -shl 24) -bor ([long]$r -shl 16) -bor ([long]$g -shl 8) -bor $b
    $lines.Add(("    0x{0:X8}u," -f $value))
}
$lines.Add("};")
$lines.Add("")
$lines.Add("}")
$lines.Add("")

Set-Content -Path $Out -Value $lines -Encoding ASCII
$outBytes = (Get-Item $Out).Length
Write-Host "$size x $size premultiplied pixels written to $Out ($outBytes bytes)"
