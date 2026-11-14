param(
    [string]$In = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($In -eq "") { $In = Join-Path $root "data\appids.txt" }
if ($Out -eq "") { $Out = Join-Path $root "src\core\appids_data.h" }

$rows = @()
foreach ($line in (Get-Content $In -Encoding UTF8)) {
    $trimmed = $line.Trim()
    if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
    $parts = $trimmed.Split("`t")
    if ($parts.Count -lt 2) {
        $parts = $trimmed.Split(" ", 2)
    }
    if ($parts.Count -lt 2) { continue }
    $id = $parts[0].Trim().ToUpperInvariant()
    $name = $parts[1].Trim()
    if ($id -eq "" -or $name -eq "") { continue }
    $rows += ,@($id, $name)
}

function Escape([string]$value) {
    $builder = New-Object System.Text.StringBuilder
    foreach ($ch in $value.ToCharArray()) {
        $code = [int]$ch
        if ($code -lt 32 -or $code -gt 126 -or $ch -eq '"' -or $ch -eq '\') {
            [void]$builder.Append("\u{0:x4}" -f $code)
        } else {
            [void]$builder.Append($ch)
        }
    }
    return $builder.ToString()
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("namespace jl {")
$lines.Add("")
$lines.Add("struct AppIdRow {")
$lines.Add("    const wchar_t* id;")
$lines.Add("    const wchar_t* name;")
$lines.Add("};")
$lines.Add("")
$lines.Add("inline const AppIdRow kAppIds[] = {")
foreach ($row in $rows) {
    $lines.Add("    {L`"$($row[0])`", L`"$(Escape $row[1])`"},")
}
$lines.Add("};")
$lines.Add("")
$lines.Add("inline const int kAppIdCount = $($rows.Count);")
$lines.Add("")
$lines.Add("}")
$lines.Add("")

Set-Content -Path $Out -Value $lines -Encoding UTF8
Write-Host "$($rows.Count) entries written to $Out"
