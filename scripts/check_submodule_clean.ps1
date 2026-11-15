$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$LockFile = Join-Path $RootDir "dependencies.lock"

Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue
Remove-Item Env:GIT_DIR -ErrorAction SilentlyContinue
Remove-Item Env:GIT_WORK_TREE -ErrorAction SilentlyContinue

Get-Content $LockFile | ForEach-Object {
  if (-not $_ -or $_.StartsWith("#")) { return }
  $Name, $Repository, $ExpectedCommit, $RelativePath = $_.Split("|")
  $Checkout = Join-Path $RootDir $RelativePath
  if (-not (Test-Path $Checkout)) { throw "Locked dependency is missing: $RelativePath" }
  $ActualCommit = (& git -C $Checkout rev-parse HEAD).Trim()
  $ActualRepository = (& git -C $Checkout remote get-url origin).Trim()
  $DirtyStatus = & git -C $Checkout status --porcelain --untracked-files=normal
  if ($LASTEXITCODE -ne 0) { throw "Failed to inspect $Name at $RelativePath" }
  if ($ActualCommit -ne $ExpectedCommit) { throw "$Name is at $ActualCommit; expected $ExpectedCommit." }
  if ($ActualRepository -ne $Repository) { throw "$Name origin is $ActualRepository; expected $Repository." }
  if ($DirtyStatus) { throw "$Name contains local modifications.`n$DirtyStatus" }
  if (Test-Path (Join-Path $Checkout ".gitmodules")) {
    $NestedStatus = & git -C $Checkout submodule status --recursive
    if ($LASTEXITCODE -ne 0 -or ($NestedStatus | Where-Object { $_ -match '^[+\-U]' })) {
      throw "$Name nested dependencies are missing or differ from the pinned tree."
    }
  }
}

Write-Host "PASS: locked dependency checkouts are pinned and clean."
