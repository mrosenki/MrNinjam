param(
  [string]$BuildType = "Release",
  [string]$BuildDir = ""
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
$OriginalClapDir = $env:NINJAMPLUGIN_CLAP_INSTALL_DIR
$OriginalVst3Dir = $env:NINJAMPLUGIN_VST3_INSTALL_DIR
$OriginalBuildDir = $env:NINJAMPLUGIN_BUILD_DIR

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
  New-Item -ItemType File -Force -Path (Join-Path $ClapDir "NinjamPlugin.clap") | Out-Null
  New-Item -ItemType File -Force -Path (Join-Path $Vst3Dir "NinjamPlugin.vst3") | Out-Null
  $env:LOCALAPPDATA = Join-Path $TestRoot "localappdata"
  $env:NINJAMPLUGIN_CLAP_INSTALL_DIR = $ClapDir
  $env:NINJAMPLUGIN_VST3_INSTALL_DIR = $Vst3Dir
  if ($BuildDir) {
    $env:NINJAMPLUGIN_BUILD_DIR = [System.IO.Path]::GetFullPath($BuildDir)
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
  if (-not (Test-Path -LiteralPath (Join-Path $ClapDir "NinjamPlugin.clap"))) {
    throw "Legacy CLAP marker was removed."
  }
  if (-not (Test-Path -LiteralPath (Join-Path $Vst3Dir "NinjamPlugin.vst3"))) {
    throw "Legacy VST3 marker was removed."
  }

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
  $env:NINJAMPLUGIN_CLAP_INSTALL_DIR = $OriginalClapDir
  $env:NINJAMPLUGIN_VST3_INSTALL_DIR = $OriginalVst3Dir
  $env:NINJAMPLUGIN_BUILD_DIR = $OriginalBuildDir
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
