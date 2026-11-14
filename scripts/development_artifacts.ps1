function Resolve-DevelopmentArtifactPath {
  param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("CLAP", "VST3")]
    [string]$Format,
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$BuildType,
    [string]$ExplicitPath = ""
  )

  if ($ExplicitPath) {
    $Candidates = @([System.IO.Path]::GetFullPath($ExplicitPath))
  } elseif ($Format -eq "CLAP") {
    $Candidates = @(
      (Join-Path $BuildDir "plugin/MrNinjam.clap"),
      (Join-Path $BuildDir ("plugin/CLAP/{0}/MrNinjam.clap" -f $BuildType)),
      (Join-Path $BuildDir ("plugin/{0}/MrNinjam.clap" -f $BuildType)),
      (Join-Path $BuildDir ("{0}/plugin/MrNinjam.clap" -f $BuildType))
    )
  } else {
    $Candidates = @(
      (Join-Path $BuildDir "plugin/MrNinjam.vst3"),
      (Join-Path $BuildDir "plugin/VST3/MrNinjam.vst3"),
      (Join-Path $BuildDir ("{0}/plugin/MrNinjam.vst3" -f $BuildType))
    )
  }

  $Candidates = @($Candidates | ForEach-Object {
      [System.IO.Path]::GetFullPath($_)
    } | Select-Object -Unique)
  $Matches = @($Candidates | Where-Object { Test-Path -LiteralPath $_ })
  if ($Matches.Count -eq 1) {
    return $Matches[0]
  }

  $CandidateText = $Candidates -join [Environment]::NewLine
  if ($Matches.Count -eq 0) {
    throw ("Expected {0} development artifact not found. Checked:{1}{2}" -f `
      $Format, [Environment]::NewLine, $CandidateText)
  }
  $MatchText = $Matches -join [Environment]::NewLine
  throw ("Ambiguous {0} development artifacts found:{1}{2}" -f `
    $Format, [Environment]::NewLine, $MatchText)
}
