param(
  [string]$BuildType = "Release",
  [switch]$DryRun,
  [switch]$SkipVerify
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ($BuildType -notmatch '^[A-Za-z0-9._-]+$') {
  throw "Invalid build type: $BuildType"
}
if (-not $env:LOCALAPPDATA) {
  throw "LOCALAPPDATA is required for a per-user Windows installation."
}

$DefaultClapInstallDir = Join-Path $env:LOCALAPPDATA "Programs/Common/CLAP"
$DefaultVst3InstallDir = Join-Path $env:LOCALAPPDATA "Programs/Common/VST3"
$ClapInstallDir = if ($env:NINJAMPLUGIN_CLAP_INSTALL_DIR) {
  $env:NINJAMPLUGIN_CLAP_INSTALL_DIR
} else {
  $DefaultClapInstallDir
}
$Vst3InstallDir = if ($env:NINJAMPLUGIN_VST3_INSTALL_DIR) {
  $env:NINJAMPLUGIN_VST3_INSTALL_DIR
} else {
  $DefaultVst3InstallDir
}

function Assert-SafeInstallDirectory {
  param([string]$Path)

  if (-not [System.IO.Path]::IsPathRooted($Path)) {
    throw "Install destination must be absolute: $Path"
  }
  $FullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
  )
  $RootPrefix = $RootDir.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
  ) + [System.IO.Path]::DirectorySeparatorChar
  if ($FullPath.Equals($RootDir, [System.StringComparison]::OrdinalIgnoreCase) -or
      $FullPath.StartsWith($RootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to install into the source checkout: $FullPath"
  }
  return $FullPath
}

$ClapInstallDir = Assert-SafeInstallDirectory $ClapInstallDir
$Vst3InstallDir = Assert-SafeInstallDirectory $Vst3InstallDir
$BuildDir = Join-Path $RootDir ("build/{0}" -f $BuildType)
$ClapArtifact = Join-Path $BuildDir "plugin/MrNinjam.clap"
$Vst3Artifact = Join-Path $BuildDir "plugin/MrNinjam.vst3"

if (-not $SkipVerify) {
  & (Join-Path $RootDir "scripts/verify.ps1") -BuildType $BuildType
}

foreach ($ArtifactPath in @($ClapArtifact, $Vst3Artifact)) {
  if (-not (Test-Path -LiteralPath $ArtifactPath)) {
    throw "Expected development artifact not found: $ArtifactPath"
  }
  Get-ChildItem -LiteralPath $ArtifactPath -Recurse -File | ForEach-Object {
    cmake "-DARTIFACT=$($_.FullName)" "-DFORBIDDEN_PATHS=$RootDir" `
      -P (Join-Path $RootDir "scripts/check_artifact_paths.cmake")
    if ($LASTEXITCODE -ne 0) {
      throw "Packaged path scan failed for $($_.FullName)"
    }
  }
}

function Write-LegacyWarning {
  param([string]$Path)

  if (Test-Path -LiteralPath $Path) {
    Write-Warning "Legacy development bundle retained at $Path"
    Write-Warning "Remove it manually only after confirming old projects no longer need it."
  }
}

Write-LegacyWarning (Join-Path $ClapInstallDir "NinjamPlugin.clap")
Write-LegacyWarning (Join-Path $Vst3InstallDir "NinjamPlugin.vst3")

function Install-DevelopmentArtifact {
  param(
    [string]$SourcePath,
    [string]$InstallDir,
    [string]$ArtifactName
  )

  $TargetPath = Join-Path $InstallDir $ArtifactName
  if ($DryRun) {
    Write-Host ("Would install: {0} -> {1}" -f $SourcePath, $TargetPath)
    return
  }

  New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
  $ResolvedInstallDir = (Resolve-Path -LiteralPath $InstallDir).Path
  $ResolvedInstallDir = Assert-SafeInstallDirectory $ResolvedInstallDir
  $TargetPath = Join-Path $ResolvedInstallDir $ArtifactName
  $TemporaryPath = Join-Path $ResolvedInstallDir (".{0}.install.{1}" -f $ArtifactName, $PID)
  $BackupPath = Join-Path $ResolvedInstallDir (".{0}.backup.{1}" -f $ArtifactName, $PID)

  if ((Test-Path -LiteralPath $TemporaryPath) -or (Test-Path -LiteralPath $BackupPath)) {
    throw "Temporary install path already exists for $ArtifactName"
  }

  try {
    Copy-Item -LiteralPath $SourcePath -Destination $TemporaryPath -Recurse
    if (Test-Path -LiteralPath $TargetPath) {
      Move-Item -LiteralPath $TargetPath -Destination $BackupPath
    }
    try {
      Move-Item -LiteralPath $TemporaryPath -Destination $TargetPath
    } catch {
      if (Test-Path -LiteralPath $BackupPath) {
        Move-Item -LiteralPath $BackupPath -Destination $TargetPath
      }
      throw
    }
    if (Test-Path -LiteralPath $BackupPath) {
      Remove-Item -LiteralPath $BackupPath -Recurse -Force
    }
  } finally {
    if (Test-Path -LiteralPath $TemporaryPath) {
      Remove-Item -LiteralPath $TemporaryPath -Recurse -Force
    }
  }

  Write-Host ("Installed: {0}" -f $TargetPath)
}

Install-DevelopmentArtifact $ClapArtifact $ClapInstallDir "MrNinjam.clap"
Install-DevelopmentArtifact $Vst3Artifact $Vst3InstallDir "MrNinjam.vst3"

if ($DryRun) {
  Write-Host "Dry run complete; no files were changed."
} else {
  Write-Host "Development install complete. Close and rescan the plug-ins in the DAW before testing."
}
