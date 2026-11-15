# Codex project instructions

## GitHub Actions

- Never wait for GitHub Actions jobs to finish.
- Never poll, watch, or repeatedly query CI status while waiting for completion.
- Do not use `gh run watch` or equivalent blocking commands.
- When authorised, Codex may trigger CI and retrieve the run URL or its immediate
  status once. If the run is still in progress, report it as pending and do not
  perform any work that depends on its result.
- Pending CI does not block local building, installation, testing, or unrelated
  development. Continue that work without polling CI when it is otherwise safe
  and authorised.
- Inspect completed CI results and logs only in a subsequent interaction after
  the user confirms that the run has finished.
- Do not arrange automatic or recurring checks for CI completion.
