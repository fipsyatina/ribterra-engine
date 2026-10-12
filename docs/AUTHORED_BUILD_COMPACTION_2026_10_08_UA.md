# Compact storage authored engine-verification — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The operation is performed after a direct request from the user to remove the old versions, separate notification of all 56 accurate paths, and repeated independent manifest. It's cleaning out out old copies of software, not receiving engine or graphics.

## Actual result

- The actual Apply ended with exit 0; `complete=true`, `error=null`, `protected_error=null`.
- Remove **56** `.exe/.dll/.obj` from the four obvious roots `build/sky-authored-2026-10-08-attempt06`, `attempt07`, `attempt08`, `attempt09`. The catalogs were not deleted.
- The logical volume of deleted files: **228 214 270 bytes (228,21 MB) **.
- Free space C: **97 032 024 064 → 97 259 892 736 bytes**; The observed increase in **227 868 672 bytes (227,87 MB) **. This is the size of the disk during the operation; The physical increase is not the logical increase.
- The remaining reported targets are: **0**. All **4303** protected files are compiled in the exact path, scope, size, UTC timestamp and SHA-256 and have not changed.

All latest authored attempt10 and rollback attempt05 files are stored, all GameView engine build/native roots, 12 fresh native GameView proof files, source files, shader assets, textures, source snapshots, reports, logs, fonts, PNG, and current launcher targets. Removed old executable copies can no longer be restarted; Their records are historical evidence, not runtime available.

## Accurate Evidence and Limits

- [Message before deleting with all 56 paths ](AUTHORED_BUILD_CLEANUP_NOTICE_2026_10_08_02_UA.md).
- [Used plan04](../.omx/plans/authored-build-prune-2026-10-08-04.json), SHA-256 `67EFFB72CDE0A8FE14815D6E007B0AB6CDE5F319B2E83F8A290C36B81EEC7B32`.
- [The actual result is ](../.omx/plans/authored-build-prune-2026-10-08-04.json.result.json), SHA-256 `866AE1506C5D16DE9B280E4ED2687351EA165F00586D9CEAC348A24737C62517`.
- [Limited helper](../tools/prune_authored_build_copies.ps1), SHA-256 version used in plan04: `134854B8B3DE219D396284539D5DE93040E64EE5D2E9A5E693D54331F68F32FF`.

Helper has an explicit allowlist of exactly these 56 files, protection from reparse-transitions, a manifest and its own SHA scan, a complete scan of protected files before and after, checking each goal and active process directly before the non-recursive `Remove-Item -LiteralPath`. The modified SHA manifest is rejected by an actual negative start to deletion. Transferring protected records does not change the result of the keyed-matching; the subset SHA is deviated.

An independent audit after Apply re-checked all 4303 protected files on the disk and confirmed the absence of all 56 targets. Protected cohorts: rollback05 828 files, latest10 — 840, GameView — 1146, including from 12 native proof files.

Only a serialization error of the historical result is known: `remaining_files` is written as `[null]`, although `remaining_file_count=0` and independent factual verification confirmed zero residues. The result is not overwritten. For future reports only the empty array projection in the helper is corrected; the current source SHA-256 `68B39180AA59DDF5613392E74C8AA3F57D846A853DD67A6EAAB203C0F9097527`. In-memory checks gave `[]` for zero and one track array for one record; parser — 0 error. The new Apply after this source-only change was not executed; The old plan04 is attached to the historical SHA helper and is not suitable for new use with the modified helper.

Plan01–03 not applied: plan01 had a wrong aggregate amount, plan02 did not include 12 native proof files for protection, and Apply plan03 stopped until the first deletion due to positional matching of different record order. Fixed match on the road, without loosening metadata/hash checks; I've created a new plan04 and a new user message. All previous manifests and messages are stored as history, not successful cleanup-proof.

Removal is done locally, not through the basket. The code for the replay is saved, but recovery of the historical byte-exact executable is not guaranteed. GitHub uploads, commits, releases, installs and launcher changes were not executed. This is an additional operation after [cleaning old runtime artifacts](ARTIFACT_CLEANUP_2026_10_08_UA.md); It doesn't claim that all the historical packets in `artifacts/build/dist` have been deleted.

The [storage rule](ARTIFACT_RETENTION_UA.md) continues: current startup set and one verified backup; compact evidence instead of complete copies after confirmation of replacement; a specific message before each subsequent removal.
