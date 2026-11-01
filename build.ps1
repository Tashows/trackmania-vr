# Builds TrackmaniaVR.dll and installs the plugin into OpenplanetNext\Plugins\TrackmaniaVR.
#   .\build.ps1             build (Release) and install
#   .\build.ps1 -NoInstall  build only
param(
    [switch]$NoInstall,
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = Join-Path $root "build"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Build Tools not found. Install them first (see README.md)."
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw "Visual Studio is installed but the C++ x64 tools are missing (workload: Desktop development with C++)."
}

$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "CMake not found. Add the 'C++ CMake tools for Windows' component." }
    $cmake = $cmd.Source
}

& $cmake -S (Join-Path $root "native") -B $buildDir -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$binDir = Join-Path $buildDir $Config
Write-Host "Built $binDir\TrackmaniaVR.dll"
if ($NoInstall) { return }

$target = Join-Path $env:USERPROFILE "OpenplanetNext\Plugins\TrackmaniaVR"
New-Item -ItemType Directory -Force $target | Out-Null
try {
    Copy-Item (Join-Path $root "plugin\*") $target -Recurse -Force
    Copy-Item (Join-Path $binDir "TrackmaniaVR.dll") $target -Force
    Copy-Item (Join-Path $binDir "openxr_loader.dll") $target -Force
    Copy-Item (Join-Path $binDir "dxgi_probe.exe") $target -Force
} catch {
    throw "Could not copy into $target. If Trackmania is running with the plugin loaded, close the game and run this again. ($_)"
}
Write-Host "Installed to $target"
