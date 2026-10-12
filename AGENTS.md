# File preservation and build retention

User instruction: do not delete any file without first informing the user of
the concrete paths and reason. This includes temporary files, caches, downloaded
archives, generated artifacts, and cleanup performed by scripts or installers.

User instruction (2026-10-08): do not accumulate old full game versions in
`artifacts`. Follow [the retention policy](docs/ARTIFACT_RETENTION_UA.md).

- Keep the current runnable build and one verified rollback build. Preserve
  active launcher targets and running-process dependencies until replaced.
- Reuse the current runtime for checks; retain compact reports, hashes and
  necessary screenshots instead of a full runtime copy for every test attempt.
- After a replacement passes its checks, identify obsolete generated copies
  before creating further full versions. Make an exact dry-run file manifest,
  announce its paths, size, reason and recovery limits, then remove only those
  unchanged, explicitly identified files within the user's cleanup scope.
- Never age-prune or recursively wipe `artifacts`, `build` or `dist`. Preserve
  source, original art, concepts, saves and required validation evidence.
- Do not upload build folders, create GitHub releases or claim an archive exists
  merely because a repository exists. Publishing requires an explicit scope and
  a verified successful upload before relying on it for recovery.

Existing bounded prune helpers have historical allowlists. Do not reuse their
old plans as a cleaner for new versions.
