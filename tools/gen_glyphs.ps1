param(
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$icons = Join-Path $root "data\icons"
if ($Out -eq "") { $Out = Join-Path $root "src\ui\glyph_data.h" }
$size = 48

$glyphs = @(
    @{ Name = "kGlyphSearch"; File = "magnifier.svg" },
    @{ Name = "kGlyphRefresh"; File = "refresh.svg" },
    @{ Name = "kGlyphDownload"; File = "download.svg" },
    @{ Name = "kGlyphEmpty"; File = "list-magnifying-glass.svg" },
    @{ Name = "kGlyphFolder"; File = "folder.svg" },
    @{ Name = "kGlyphCopy"; File = "copy.svg" }
)

$chrome = "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $chrome)) { $chrome = "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe" }

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("namespace jl {")
$lines.Add("")
$lines.Add("inline const int kGlyphSize = $size;")
$lines.Add("")

foreach ($glyph in $glyphs) {
    $svg = Join-Path $icons $glyph.File
    $png = Join-Path $env:TEMP ("glyph-" + $glyph.Name + ".png")
    & $chrome --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 --window-size=$size,$size --screenshot="$png" "file:///$($svg -replace '\\','/')" | Out-Null
    $source = [System.Drawing.Bitmap]::FromFile($png)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $data = $source.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $source.UnlockBits($data)
    $source.Dispose()

    $lines.Add(("inline const unsigned char {0}[] = {{" -f $glyph.Name))
    $alpha = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $size * $size; $i++) {
        $alpha.Add([string]$bytes[$i * 4 + 3])
    }
    for ($i = 0; $i -lt $alpha.Count; $i += 24) {
        $chunk = $alpha.GetRange($i, [Math]::Min(24, $alpha.Count - $i))
        $lines.Add("    " + ($chunk -join ", ") + ",")
    }
    $lines.Add("};")
    $lines.Add("")
}

$lines.Add("}")
$lines.Add("")

Set-Content -Path $Out -Value $lines -Encoding ASCII
Write-Host "glyphs written to $Out"
