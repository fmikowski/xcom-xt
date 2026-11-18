# SPDX-License-Identifier: GPL-3.0-or-later
# Saves a PNG of a process's main window without needing focus (PrintWindow).
#   .\tools\snapwin.ps1 -ProcessId 1234 -Out shot.png
param(
    [Parameter(Mandatory)] [int]$ProcessId,
    [Parameter(Mandatory)] [string]$Out
)
$ErrorActionPreference = 'Stop'
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
Add-Type -AssemblyName System.Drawing
if (-not ('SnapWin' -as [type])) { Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SnapWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@ }
$proc = Get-Process -Id $ProcessId
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "process $ProcessId has no main window" }
$r = New-Object SnapWin+RECT
[void][SnapWin]::GetWindowRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][SnapWin]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "Saved $Out"
