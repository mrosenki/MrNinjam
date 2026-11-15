param(
  [string]$BuildType = "Release",
  [string]$BuildDir = "",
  [string]$ClapArtifact = "",
  [string]$Vst3Artifact = ""
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Installer = Join-Path $RootDir "scripts/install_development.ps1"
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
  ("mrninjam-installer-test-{0}" -f [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $TestRoot | Out-Null
$TestRoot = (Resolve-Path -LiteralPath $TestRoot).Path
if (-not ($IsWindows -or $env:OS -eq "Windows_NT") -and
    $TestRoot.StartsWith("/var/")) {
  $TestRoot = "/private$TestRoot"
}
$OriginalPath = $env:PATH
$OriginalLocalAppData = $env:LOCALAPPDATA
$OriginalClapDir = $env:MRNINJAM_CLAP_INSTALL_DIR
$OriginalVst3Dir = $env:MRNINJAM_VST3_INSTALL_DIR
$OriginalBuildDir = $env:MRNINJAM_BUILD_DIR
$OriginalClapArtifact = $env:MRNINJAM_CLAP_ARTIFACT
$OriginalVst3Artifact = $env:MRNINJAM_VST3_ARTIFACT

function Invoke-ExpectedFailure {
  param(
    [scriptblock]$Action,
    [string]$Message
  )
  $Failed = $false
  try {
    & $Action
  } catch {
    $Failed = $true
  }
  if (-not $Failed) {
    throw $Message
  }
}

$TestFailure = $null
try {
  $ClapDir = Join-Path $TestRoot "clap"
  $Vst3Dir = Join-Path $TestRoot "vst3"
  New-Item -ItemType Directory -Force -Path $ClapDir, $Vst3Dir | Out-Null
  $env:LOCALAPPDATA = Join-Path $TestRoot "localappdata"
  $env:MRNINJAM_CLAP_INSTALL_DIR = $ClapDir
  $env:MRNINJAM_VST3_INSTALL_DIR = $Vst3Dir
  if ($BuildDir) {
    $env:MRNINJAM_BUILD_DIR = [System.IO.Path]::GetFullPath($BuildDir)
  }
  if ($ClapArtifact) {
    $env:MRNINJAM_CLAP_ARTIFACT = [System.IO.Path]::GetFullPath($ClapArtifact)
  }
  if ($Vst3Artifact) {
    $env:MRNINJAM_VST3_ARTIFACT = [System.IO.Path]::GetFullPath($Vst3Artifact)
  }

  & $Installer -BuildType $BuildType -DryRun -SkipVerify
  if (Test-Path -LiteralPath (Join-Path $ClapDir "MrNinjam.clap")) {
    throw "Dry run changed the CLAP install directory."
  }
  if (Test-Path -LiteralPath (Join-Path $Vst3Dir "MrNinjam.vst3")) {
    throw "Dry run changed the VST3 install directory."
  }

  & $Installer -BuildType $BuildType -SkipVerify
  & $Installer -BuildType $BuildType -SkipVerify
  Remove-Item -LiteralPath (Join-Path $ClapDir "MrNinjam.clap") -Recurse -Force
  Remove-Item -LiteralPath (Join-Path $Vst3Dir "MrNinjam.vst3") -Recurse -Force
  New-Item -ItemType Directory -Force -Path `
    (Join-Path $ClapDir "MrNinjam.clap"), (Join-Path $Vst3Dir "MrNinjam.vst3") | Out-Null
  Set-Content -LiteralPath (Join-Path $ClapDir "MrNinjam.clap/installed-marker") `
    -Value "verified-old-clap"
  Set-Content -LiteralPath (Join-Path $Vst3Dir "MrNinjam.vst3/installed-marker") `
    -Value "verified-old-vst3"

  $FakeCMakeDir = Join-Path $TestRoot "fake-cmake"
  New-Item -ItemType Directory -Force -Path $FakeCMakeDir | Out-Null
  if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $FakeCMake = Join-Path $FakeCMakeDir "cmake.cmd"
    Set-Content -LiteralPath $FakeCMake -Value "@exit /b 37"
  } else {
    $FakeCMake = Join-Path $FakeCMakeDir "cmake"
    Set-Content -LiteralPath $FakeCMake -Value "#!/usr/bin/env sh`nexit 37"
    & chmod +x $FakeCMake
  }
  $env:PATH = "$FakeCMakeDir$([System.IO.Path]::PathSeparator)$OriginalPath"
  Invoke-ExpectedFailure {
    & $Installer -BuildType $BuildType
  } "Verification failure did not stop installation."
  if ((Get-Content -LiteralPath (Join-Path $ClapDir "MrNinjam.clap/installed-marker")) -ne
      "verified-old-clap") {
    throw "CLAP install changed after verification failure."
  }
  if ((Get-Content -LiteralPath (Join-Path $Vst3Dir "MrNinjam.vst3/installed-marker")) -ne
      "verified-old-vst3") {
    throw "VST3 install changed after verification failure."
  }
  $env:PATH = $OriginalPath

  function Move-Item {
    [CmdletBinding()]
    param(
      [string]$LiteralPath,
      [string]$Destination,
      [System.Management.Automation.ActionPreference]$ErrorAction = `
        [System.Management.Automation.ActionPreference]::Stop
    )
    if ($LiteralPath -like "*.MrNinjam.vst3.install.*" -and
        $Destination -like "*MrNinjam.vst3") {
      throw "Simulated VST3 commit failure."
    }
    Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath `
      -Destination $Destination -ErrorAction $ErrorAction
  }

  Invoke-ExpectedFailure {
    & $Installer -BuildType $BuildType -SkipVerify
  } "Simulated commit failure unexpectedly succeeded."
  Remove-Item Function:\Move-Item
  if ((Get-Content -LiteralPath (Join-Path $ClapDir "MrNinjam.clap/installed-marker")) -ne
      "verified-old-clap") {
    throw "CLAP install was not restored after commit failure."
  }
  if ((Get-Content -LiteralPath (Join-Path $Vst3Dir "MrNinjam.vst3/installed-marker")) -ne
      "verified-old-vst3") {
    throw "VST3 install was not restored after commit failure."
  }
  $TransactionFiles = Get-ChildItem -LiteralPath $TestRoot -Recurse -Force |
    Where-Object { $_.Name -match '\.(install|backup)\.' }
  if ($TransactionFiles) {
    throw "Installer left transaction files after rollback."
  }
} catch {
  $TestFailure = $_
} finally {
  $env:PATH = $OriginalPath
  $env:LOCALAPPDATA = $OriginalLocalAppData
  $env:MRNINJAM_CLAP_INSTALL_DIR = $OriginalClapDir
  $env:MRNINJAM_VST3_INSTALL_DIR = $OriginalVst3Dir
  $env:MRNINJAM_BUILD_DIR = $OriginalBuildDir
  $env:MRNINJAM_CLAP_ARTIFACT = $OriginalClapArtifact
  $env:MRNINJAM_VST3_ARTIFACT = $OriginalVst3Artifact
  if (Test-Path Function:\Move-Item) {
    Remove-Item Function:\Move-Item
  }
  if (Test-Path -LiteralPath $TestRoot) {
    Remove-Item -LiteralPath $TestRoot -Recurse -Force
  }
}

if ($TestFailure) {
  Write-Error $TestFailure
  exit 1
}
Write-Host "Development installer regression tests passed."
