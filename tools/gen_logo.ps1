param(
    [string]$Png = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Png -eq "") { $Png = Join-Path $root "data\logo.png" }
if ($Out -eq "") { $Out = Join-Path $root "src\ui\logo_data.h" }

$image = [System.Drawing.Bitmap]::FromFile($Png)
$width = $image.Width
$height = $image.Height
$rect = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
$data = $image.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $height)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$image.UnlockBits($data)
$image.Dispose()

$alpha = New-Object System.Collections.Generic.List[string]
for ($y = 0; $y -lt $height; $y++) {
    for ($x = 0; $x -lt $width; $x++) {
        $index = $y * $data.Stride + $x * 4
        $a = $bytes[$index + 3]
        $alpha.Add($a)
    }
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("namespace jl {")
$lines.Add("")
$lines.Add("inline const int kLogoWidth = $width;")
$lines.Add("inline const int kLogoHeight = $height;")
$lines.Add("")
$lines.Add("inline const unsigned char kLogoAlpha[] = {")
for ($i = 0; $i -lt $alpha.Count; $i += 24) {
    $chunk = $alpha.GetRange($i, [Math]::Min(24, $alpha.Count - $i))
    $lines.Add("    " + ($chunk -join ", ") + ",")
}
$lines.Add("};")
$lines.Add("")
$lines.Add("}")
$lines.Add("")

Set-Content -Path $Out -Value $lines -Encoding UTF8
Write-Host "$width x $height alpha mask written to $Out"
