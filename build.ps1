param(
    [switch]$Tests,
    [switch]$Clean,
    [string]$Out = "",
    [switch]$Debug
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars"
}

$buildDir = Join-Path $root "build"
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$objDir = Join-Path $buildDir "obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

if ($Tests) {
    $sources = @(Get-ChildItem (Join-Path $root "src\core") -Filter *.cpp | ForEach-Object { $_.FullName })
    $sources += Join-Path $root "tests\parse_test.cpp"
} else {
    $sources = @(Get-ChildItem (Join-Path $root "src") -Recurse -Filter *.cpp | ForEach-Object { $_.FullName })
}

$flags = @("/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W3", "/wd4267", "/wd4244", "/MT", "/DUNICODE", "/D_UNICODE")
if ($Debug) {
    $flags += @("/Od", "/Zi")
} else {
    $flags += @("/O2", "/DNDEBUG")
}

$libs = @("d2d1.lib", "dwrite.lib", "comctl32.lib", "comdlg32.lib", "shell32.lib", "ole32.lib", "shlwapi.lib",
          "gdi32.lib", "user32.lib", "advapi32.lib", "dwmapi.lib", "uxtheme.lib", "propsys.lib",
          "dcomp.lib", "bcrypt.lib", "wintrust.lib", "crypt32.lib")

$exeName = if ($Tests) { "jumplens-parse-test.exe" } else { "jumplens.exe" }
if ($Out -ne "") {
    $exeName = $Out
}

$objDirFwd = ($objDir -replace '\\', '/')
$buildFwd = ($buildDir -replace '\\', '/')

$rsp = @()
$rsp += $flags
$rsp += "/Fd$objDirFwd/jumplens.pdb"
$rsp += "/Fo$objDirFwd/"
foreach ($source in $sources) {
    $rsp += "`"$($source -replace '\\', '/')`""
}
$rsp += "/Fe$buildFwd/$exeName"
$rsp += "/link"
$rsp += $libs
if ($Tests) {
    $rsp += "/SUBSYSTEM:CONSOLE"
}

$rspPath = Join-Path $buildDir "args.rsp"
Set-Content -Path $rspPath -Value $rsp -Encoding ASCII

$kitsRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkVersion = Get-ChildItem (Join-Path $kitsRoot "Include") -ErrorAction SilentlyContinue |
    Where-Object { $_.PSIsContainer } | Sort-Object Name -Descending | Select-Object -First 1
if ($null -eq $sdkVersion) {
    throw "no Windows SDK include folder found under $kitsRoot"
}
$sdkInc = Join-Path $kitsRoot "Include\$($sdkVersion.Name)"
$sdkLib = Join-Path $kitsRoot "Lib\$($sdkVersion.Name)"

$bat = Join-Path $buildDir "compile.bat"
$lines = @(
    "@echo off",
    "call `"$vcvars`" > nul",
    "set `"INCLUDE=%INCLUDE%;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared;$sdkInc\winrt`"",
    "set `"LIB=%LIB%;$sdkLib\ucrt\x64;$sdkLib\um\x64`"",
    "cd /d `"$root`"",
    "cl @`"$rspPath`"",
    "exit /b %ERRORLEVEL%"
)
Set-Content -Path $bat -Value $lines -Encoding ASCII

Write-Host "compiling $($sources.Count) source files -> $exeName"
$ErrorActionPreference = "Continue"
& cmd.exe /c "`"$bat`"" 2>&1 | Write-Host
$ErrorActionPreference = "Stop"
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "compilation failed with exit code $exitCode"
}
Write-Host "built $buildDir\$exeName"
