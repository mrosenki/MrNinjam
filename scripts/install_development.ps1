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

  $ExistingAncestor = $FullPath
  while (-not (Test-Path -LiteralPath $ExistingAncestor)) {
    $Parent = [System.IO.Path]::GetDirectoryName($ExistingAncestor)
    if (-not $Parent -or $Parent -eq $ExistingAncestor) {
      throw "Could not resolve install destination: $FullPath"
    }
    $ExistingAncestor = $Parent
  }
  $Cursor = Get-Item -LiteralPath $ExistingAncestor -Force
  while ($null -ne $Cursor) {
    if (($Cursor.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Refusing install destination with symbolic-link or junction ancestor: $FullPath"
    }
    $Cursor = $Cursor.Parent
  }
  return $FullPath
}

$ClapInstallDir = Assert-SafeInstallDirectory $ClapInstallDir
$Vst3InstallDir = Assert-SafeInstallDirectory $Vst3InstallDir
$BuildDir = if ($env:NINJAMPLUGIN_BUILD_DIR) {
  [System.IO.Path]::GetFullPath($env:NINJAMPLUGIN_BUILD_DIR)
} else {
  Join-Path $RootDir ("build/{0}" -f $BuildType)
}
$ClapArtifact = Join-Path $BuildDir "plugin/MrNinjam.clap"
$Vst3Artifact = Join-Path $BuildDir "plugin/MrNinjam.vst3"

if (-not $SkipVerify) {
  & (Join-Path $RootDir "scripts/verify.ps1") -BuildType $BuildType -BuildDir $BuildDir
  if (-not $?) {
    throw "Development build verification failed; installed plug-ins were not changed."
  }
}

foreach ($ArtifactPath in @($ClapArtifact, $Vst3Artifact)) {
  if (-not (Test-Path -LiteralPath $ArtifactPath)) {
    throw "Expected development artifact not found: $ArtifactPath"
  }
  $ArtifactEntries = @(
    Get-Item -LiteralPath $ArtifactPath -Force
    Get-ChildItem -LiteralPath $ArtifactPath -Recurse -Force
  )
  if ($ArtifactEntries | Where-Object {
      ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    }) {
    throw "Refusing symbolic links or junctions in development artifact: $ArtifactPath"
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

if ($DryRun) {
  Write-Host ("Would install: {0} -> {1}" -f $ClapArtifact,
    (Join-Path $ClapInstallDir "MrNinjam.clap"))
  Write-Host ("Would install: {0} -> {1}" -f $Vst3Artifact,
    (Join-Path $Vst3InstallDir "MrNinjam.vst3"))
  Write-Host "Dry run complete; no files were changed."
  return
}

New-Item -ItemType Directory -Force -Path $ClapInstallDir | Out-Null
New-Item -ItemType Directory -Force -Path $Vst3InstallDir | Out-Null
$ResolvedClapInstallDir = (Resolve-Path -LiteralPath $ClapInstallDir).Path
$ResolvedVst3InstallDir = (Resolve-Path -LiteralPath $Vst3InstallDir).Path
$ClapInstallDir = Assert-SafeInstallDirectory $ResolvedClapInstallDir
$Vst3InstallDir = Assert-SafeInstallDirectory $ResolvedVst3InstallDir

$InstallItems = @(
  [PSCustomObject]@{
    Source = $ClapArtifact
    Directory = $ClapInstallDir
    Name = "MrNinjam.clap"
    Target = Join-Path $ClapInstallDir "MrNinjam.clap"
    Temporary = Join-Path $ClapInstallDir (".MrNinjam.clap.install.{0}" -f $PID)
    Backup = Join-Path $ClapInstallDir (".MrNinjam.clap.backup.{0}" -f $PID)
    HadTarget = $false
    Installed = $false
  },
  [PSCustomObject]@{
    Source = $Vst3Artifact
    Directory = $Vst3InstallDir
    Name = "MrNinjam.vst3"
    Target = Join-Path $Vst3InstallDir "MrNinjam.vst3"
    Temporary = Join-Path $Vst3InstallDir (".MrNinjam.vst3.install.{0}" -f $PID)
    Backup = Join-Path $Vst3InstallDir (".MrNinjam.vst3.backup.{0}" -f $PID)
    HadTarget = $false
    Installed = $false
  }
)

try {
  foreach ($Item in $InstallItems) {
    $ExistingTarget = Get-Item -LiteralPath $Item.Target -Force -ErrorAction SilentlyContinue
    if ($ExistingTarget -and
        (($ExistingTarget.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
      throw "Refusing to replace symbolic-link or junction install target: $($Item.Target)"
    }
    if ((Test-Path -LiteralPath $Item.Temporary) -or
        (Test-Path -LiteralPath $Item.Backup)) {
      throw "Temporary install path already exists for $($Item.Name)"
    }
    Copy-Item -LiteralPath $Item.Source -Destination $Item.Temporary -Recurse
  }

  foreach ($Item in $InstallItems) {
    if (Test-Path -LiteralPath $Item.Target) {
      Move-Item -LiteralPath $Item.Target -Destination $Item.Backup
      $Item.HadTarget = $true
    }
  }

  foreach ($Item in $InstallItems) {
    Move-Item -LiteralPath $Item.Temporary -Destination $Item.Target
    $Item.Installed = $true
  }
} catch {
  $InstallError = $_
  foreach ($Item in $InstallItems) {
    if ($Item.Installed -and (Test-Path -LiteralPath $Item.Target)) {
      Remove-Item -LiteralPath $Item.Target -Recurse -Force -ErrorAction Continue
    }
  }
  foreach ($Item in $InstallItems) {
    if ($Item.HadTarget -and (Test-Path -LiteralPath $Item.Backup)) {
      Move-Item -LiteralPath $Item.Backup -Destination $Item.Target -ErrorAction Continue
    }
  }
  throw $InstallError
} finally {
  foreach ($Item in $InstallItems) {
    if (Test-Path -LiteralPath $Item.Temporary) {
      Remove-Item -LiteralPath $Item.Temporary -Recurse -Force -ErrorAction Continue
    }
  }
}

foreach ($Item in $InstallItems) {
  if (Test-Path -LiteralPath $Item.Backup) {
    try {
      Remove-Item -LiteralPath $Item.Backup -Recurse -Force
    } catch {
      Write-Warning "Installed successfully but could not remove backup: $($Item.Backup)"
    }
  }
  Write-Host ("Installed: {0}" -f $Item.Target)
}

Write-Host "Development install complete. Close and rescan the plug-ins in the DAW before testing."
