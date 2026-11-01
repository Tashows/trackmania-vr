# Builds TrackmaniaVR.dll and installs the plugin into OpenplanetNext\Plugins\TrackmaniaVR.
#   .\build.ps1             build (Release) and install
#   .\build.ps1 -NoInstall  build only
#   .\build.ps1 -Package    build and create build\package\TrackmaniaVR-<version>.op for release
#                           (never replaces an existing version; -Overwrite to rebuild it)
param(
    [switch]$NoInstall,
    [switch]$Package,
    [switch]$Overwrite,
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

# Everything the plugin ships with, copied flat into $target.
function Copy-PluginFiles($target) {
    New-Item -ItemType Directory -Force $target | Out-Null
    Copy-Item (Join-Path $root "plugin\*") $target -Recurse -Force
    Copy-Item (Join-Path $root "LICENSE") $target -Force
    Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.txt") $target -Force
    Copy-Item (Join-Path $binDir "TrackmaniaVR.dll") $target -Force
    Copy-Item (Join-Path $binDir "openxr_loader.dll") $target -Force
    Copy-Item (Join-Path $binDir "dxgi_probe.exe") $target -Force
}

if ($Package) {
    $info = Get-Content (Join-Path $root "plugin\info.toml") -Raw
    if ($info -notmatch 'version\s*=\s*"([^"]+)"') { throw "No version in plugin\info.toml" }
    $version = $Matches[1]
    $packageDir = Join-Path $buildDir "package"
    $op = Join-Path $packageDir "TrackmaniaVR-$version.op"
    # Released packages are kept as a record; bump the version in info.toml instead.
    if ((Test-Path $op) -and -not $Overwrite) {
        throw "$op already exists. Bump the version in plugin\info.toml, or pass -Overwrite to replace it."
    }
    $stage = Join-Path $packageDir "TrackmaniaVR"
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -Confirm:$false }
    Copy-PluginFiles $stage
    # An .op file is a zip with info.toml at its root.
    $zip = Join-Path $packageDir "TrackmaniaVR-$version.zip"
    Remove-Item $zip, $op -Force -Confirm:$false -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
    Move-Item $zip $op
    Write-Host "Packaged $op"
    return
}
if ($NoInstall) { return }

$target = Join-Path $env:USERPROFILE "OpenplanetNext\Plugins\TrackmaniaVR"
try {
    Copy-PluginFiles $target
} catch {
    throw "Could not copy into $target. If Trackmania is running with the plugin loaded, close the game and run this again. ($_)"
}
Write-Host "Installed to $target"
