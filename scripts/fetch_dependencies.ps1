$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$LockFile = Join-Path $RootDir "dependencies.lock"

function Invoke-Git([string[]]$Arguments) {
  & git @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw ("git {0} failed with exit code {1}" -f ($Arguments -join " "), $LASTEXITCODE)
  }
}

Get-Content $LockFile | ForEach-Object {
  if (-not $_ -or $_.StartsWith("#")) { return }
  $Name, $Repository, $Commit, $RelativePath = $_.Split("|")
  if ($Commit -notmatch '^[0-9a-fA-F]{40}$') { throw "$Name does not use a full commit ID." }
  if ($RelativePath -notmatch '^(deps|external)/') { throw "Unsafe dependency path: $RelativePath" }
  $Destination = Join-Path $RootDir $RelativePath
  if (Test-Path $Destination) {
    $ActualCommit = (& git -C $Destination rev-parse HEAD).Trim()
    $ActualRepository = (& git -C $Destination remote get-url origin).Trim()
    $DirtyStatus = & git -C $Destination status --porcelain --untracked-files=normal
    if ($LASTEXITCODE -ne 0 -or $ActualCommit -ne $Commit -or $ActualRepository -ne $Repository -or $DirtyStatus) {
      throw "$Name is not the expected clean locked checkout. Existing checkouts are never reset."
    }
    Write-Host "Verified $Name at $Commit"
    return
  }
  New-Item -ItemType Directory -Force -Path (Split-Path $Destination) | Out-Null
  $Temporary = "$Destination.fetching.$PID"
  if (Test-Path $Temporary) { throw "Temporary path already exists: $Temporary" }
  try {
    Invoke-Git @("clone", "--no-checkout", $Repository, $Temporary)
    Invoke-Git @("-C", $Temporary, "checkout", "--detach", $Commit)
    if (Test-Path (Join-Path $Temporary ".gitmodules")) {
      Invoke-Git @("-C", $Temporary, "submodule", "update", "--init", "--recursive")
    }
    Move-Item $Temporary $Destination
  }
  catch {
    if (Test-Path $Temporary) { Remove-Item -Recurse -Force $Temporary }
    throw
  }
  Write-Host "Prepared $Name at $Commit"
}

& (Join-Path $RootDir "scripts/check_submodule_clean.ps1")
Write-Host "PASS: all locked dependencies are present, pinned, and clean."
