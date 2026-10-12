# Restricted cleaning of old test runtime — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The cleaning is done after a user directly requests not to store old versions and a message of an exact list of files to be deleted. It's not about accepting the engine or a new version of the game.

## Actual result

- Removed **3 684** program copies in **41** historical `artifacts/<cohort>/<attempt>/runtime`.
- The total logical size of the deleted files: **82 790 787 072 bytes (82,79 GB) **.
- The observed increase in free space C: **78 086 017 024 bytes (78,09 GB) **: from 21 080 940 544 to 99 166 957 568 bytes. This is the size of the disk during the operation, not assuming that the logical size is equal to the physical size.
- After the operation `artifacts` takes the **29 027 501 580 logical bytes (29,03 GB) **. It is not claimed that all other historical packets or the entire `build/dist` have been cleaned.
- The actual process is completed with exit 0; The report `complete=true`, `error=null`.
- Special verification: **0** reported targets remained; **15/15** protected hash anchors have not changed.

## Limitations of Removal and Evidence

Only `.exe/.dll/.lib/.pdb/.obj/.ilk/.exp` are removed from the obvious historical cohorts 05–07.10: presentation, concept-rebuild/map-ground, HDR, ground-forward, material-bridge/master, local-occlusion, surface-lighting, outcrop-hud and relief-face. The catalogs were not deleted. Storages, source snapshots, art originals, textures, PNG and reports were not included in the list.

- [Accurate reported manifest](../.omx/plans/artifact-runtime-prune-2026-10-08-03.json), SHA-256 `0CD77A47B732E1875AE7C43927BF45785BA4ABE4F65459197D84BB3F6972E028`.
- [Factual log of removal](../.omx/plans/artifact-runtime-prune-2026-10-08-03.json.result.json), SHA-256 `B04B73CB2F6DA5A0DEFE6B255CC80F37EF7A6D831480EC4EDCDD51A0B54C45DF`.
- [Limited script](../tools/prune_artifact_runtime_copies.ps1), verified SHA-256 `16E745D9F8F4492A0B68AD3B4E5F68EAF7296DAFEBC79A331C197C2A58D763CF`.

Manifest has schema 2, unique absolute paths, size, UTC mtime and SHA-256 of each file. Prior to first removal and immediately before each re-checked target; The reparse transitions deviate. The absence of active processes in these runtimes was also verified after a long run. Only non-recursive `Remove-Item -LiteralPath` is used for a particular file. A negative check with an incorrect hash manifest gave exit 1 to be deleted. Buffered SHA-256 and conventional SHA-256 yielded the same results for the script, native EXE and master JSON.

Protected 15 files: `assets/art/world.json`, calibration master, targets 0261/0262/0263, authored05/06 and shadow09 EXE, current LocalOcclusion/GroundForward EXE, both EXE current launcher targets, `dist/current-build.txt`, and two `Play Ribterra*.cmd`. All their hashes after cleaning went with those measured to it.

Removal is done locally, not through the basket. Software copies can be retrieved; It's not a guarantee of restoring a historic byte-exact executable. Historical links to deleted EXE are no longer suitable for re-launch. GitHub uploads, commits, releases and launcher changes were not executed.

## Continued

[Save rule](ARTIFACT_RETENTION_UA.md): current runtime and one verified backup, compact evidence instead of a copy of full runtime in each test, separate diagnosis of failed attempts, and reporting specific targets before each subsequent removal. Active launcher targets and active dependencies shall not be removed until agreed replacement.

Apart from this operation, the [compact authored build-copies](AUTHORED_BUILD_COMPACTION_2026_10_08_UA.md): further 56 files / 228 214 270 logical bytes; the additional observed increase in free space 227 868 672 bytes. The above 82,79 GB / 78,09 GB remain the result of only the original cleaning of the artifacts; The logical and the physical dimensions are not mixed.
