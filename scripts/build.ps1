param(
  [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RootDir ("build/{0}" -f $BuildType)

& (Join-Path $RootDir "scripts/check_submodule_clean.ps1")

Write-Host ("Configuring NinjamPlugin ({0})..." -f $BuildType)
cmake -S $RootDir -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType `
  -DNINJAMPLUGIN_ENABLE_VST3=ON

Write-Host "Building NinjamPlugin targets..."
cmake --build $BuildDir -j 8

Write-Host ("Build complete: {0}" -f $BuildDir)
