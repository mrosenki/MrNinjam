# Codex project instructions

## GitHub Actions

- Never wait for GitHub Actions jobs to finish.
- Never poll, watch, or repeatedly query CI status while waiting for completion.
- Do not use `gh run watch` or equivalent blocking commands.
- When authorised, Codex may trigger CI and retrieve the run URL or its immediate
  status once. If the run is still in progress, report it as pending, stop, and
  ask the user to return after GitHub Actions has finished.
- Inspect completed CI results and logs only in a subsequent interaction after
  the user confirms that the run has finished.
- Do not arrange automatic or recurring checks for CI completion.
