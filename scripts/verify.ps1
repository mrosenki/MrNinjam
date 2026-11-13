param(
  [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RootDir ("build/{0}" -f $BuildType)
$ClapArtifact = Join-Path $BuildDir "plugin/MrNinjam.clap"
$Vst3Artifact = Join-Path $BuildDir "plugin/MrNinjam.vst3"

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

foreach ($ArtifactPath in @($ClapArtifact, $Vst3Artifact)) {
  if (-not (Test-Path $ArtifactPath)) {
    Write-Error ("Expected plugin artifact not found: {0}" -f $ArtifactPath)
  }
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
