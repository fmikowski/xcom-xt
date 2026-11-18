# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the converter for players: build\conv\XCOMCONV.EXE, one Windows x64
# program (PyInstaller bundles Python, PyYAML, the converter's modules, the
# OpenXcom rule files from data\ and the engine headers it reads from src\).
# It needs no Python on the player's PC.
#   .\tools\mkconv.ps1
param(
    [string]$Python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = "$root\build\conv"
$work = "$root\build\conv-work"

# PyInstaller and PyYAML for the build (pip, into this Python).
& $Python -c "import PyInstaller, yaml" 2>$null
if ($LASTEXITCODE) {
    & $Python -m pip install --quiet pyinstaller pyyaml
    if ($LASTEXITCODE) { throw 'pip install pyinstaller failed' }
}

New-Item -ItemType Directory -Force $out, $work | Out-Null
Remove-Item "$out\XCOMCONV.EXE" -ErrorAction SilentlyContinue
$env:PYTHONDONTWRITEBYTECODE = '1'
$piArgs = @(
    '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--console',
    '--name', 'XCOMCONV',
    '--distpath', $out, '--workpath', $work, '--specpath', $work,
    '--paths', "$root\tools",
    '--add-data', "$root\data\xcom1;data\xcom1",
    '--add-data', "$root\data\common;data\common",
    '--add-data', "$root\src\uipak.h;src",
    '--add-data', "$root\src\items.h;src",
    '--add-data', "$root\src\packver.h;src",
    '--exclude-module', 'tkinter', '--exclude-module', 'numpy', '--exclude-module', 'PIL',
    '--exclude-module', 'matplotlib', '--exclude-module', 'unittest', '--exclude-module', 'pydoc',
    "$root\tools\xcomconv.py"
)
$ErrorActionPreference = 'Continue'     # PyInstaller logs to stderr
& $Python @piArgs 2>&1 | ForEach-Object { "$_" } | Select-Object -Last 6
$ok = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($ok) { throw 'PyInstaller failed' }
$exe = Get-Item "$out\XCOMCONV.EXE"
Write-Host ("Built {0} ({1:N1} MB)" -f $exe.FullName, ($exe.Length / 1MB))


