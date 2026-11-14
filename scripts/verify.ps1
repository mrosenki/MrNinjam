param(
  [string]$BuildType = "Release",
  [string]$BuildDir = "",
  [string]$ClapArtifact = "",
  [string]$Vst3Artifact = ""
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $PSScriptRoot "development_artifacts.ps1")
$BuildDir = if ($BuildDir) {
  [System.IO.Path]::GetFullPath($BuildDir)
} elseif ($env:NINJAMPLUGIN_BUILD_DIR) {
  [System.IO.Path]::GetFullPath($env:NINJAMPLUGIN_BUILD_DIR)
} else {
  Join-Path $RootDir ("build/{0}" -f $BuildType)
}
$RequestedClapArtifact = if ($ClapArtifact) {
  $ClapArtifact
} else {
  $env:NINJAMPLUGIN_CLAP_ARTIFACT
}
$RequestedVst3Artifact = if ($Vst3Artifact) {
  $Vst3Artifact
} else {
  $env:NINJAMPLUGIN_VST3_ARTIFACT
}

& (Join-Path $RootDir "scripts/check_submodule_clean.ps1")
if (-not $?) {
  throw "Locked dependency verification failed."
}

Write-Host ("Configuring MrNinjam ({0})..." -f $BuildType)
cmake -S $RootDir -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType `
  -DNINJAMPLUGIN_BUILD_UI_HARNESS=ON `
  -DNINJAMPLUGIN_ENABLE_VST3=ON
if ($LASTEXITCODE -ne 0) {
  throw "CMake configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Building MrNinjam targets..."
cmake --build $BuildDir -j 8
if ($LASTEXITCODE -ne 0) {
  throw "Build failed with exit code $LASTEXITCODE."
}

ctest --test-dir $BuildDir -C $BuildType --output-on-failure
if ($LASTEXITCODE -ne 0) {
  throw "CTest failed with exit code $LASTEXITCODE."
}

$ClapArtifact = Resolve-DevelopmentArtifactPath -Format "CLAP" `
  -BuildDir $BuildDir -BuildType $BuildType -ExplicitPath $RequestedClapArtifact
$Vst3Artifact = Resolve-DevelopmentArtifactPath -Format "VST3" `
  -BuildDir $BuildDir -BuildType $BuildType -ExplicitPath $RequestedVst3Artifact

foreach ($ArtifactPath in @($ClapArtifact, $Vst3Artifact)) {
  Write-Host ("Plugin artifact: {0}" -f $ArtifactPath)
}

foreach ($ArtifactPath in @($ClapArtifact, $Vst3Artifact)) {
  Get-ChildItem $ArtifactPath -Recurse -File | ForEach-Object {
    cmake "-DARTIFACT=$($_.FullName)" "-DFORBIDDEN_PATHS=$RootDir" `
      -P (Join-Path $RootDir "scripts/check_artifact_paths.cmake")
    if ($LASTEXITCODE -ne 0) {
      throw "Packaged path scan failed for $($_.FullName)."
    }
  }
}
