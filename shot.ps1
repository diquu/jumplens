param(
    [string]$Out = "$env:TEMP\jumplens-shot.png",
    [int]$Wait = 12,
    [string]$ExeArgs = "",
    [switch]$Fresh
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$code = @'
using System;
using System.Runtime.InteropServices;
public class Cap {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
if (-not ("Cap" -as [type])) {
    Add-Type -TypeDefinition $code
}
[Cap]::SetProcessDPIAware() | Out-Null

$exe = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "build\jumplens.exe"
if ($Fresh) {
    Get-Process jumplens -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    if ($ExeArgs -ne "") {
        Start-Process -FilePath $exe -ArgumentList $ExeArgs
    } else {
        Start-Process -FilePath $exe
    }
    Start-Sleep -Seconds $Wait
}

$proc = Get-Process jumplens -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $proc) { throw "jumplens is not running" }
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "no main window yet" }

$rect = New-Object Cap+RECT
[Cap]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap($width, $height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[Cap]::PrintWindow($hwnd, $hdc, 1) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "$Out ${width}x${height}"

