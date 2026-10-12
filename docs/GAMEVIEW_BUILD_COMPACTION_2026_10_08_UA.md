# Compact storage GameView engine-checking — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Only the reported clearance of 102 compiled-copies was performed.** The code, art, player savings, reports, PNG and current startup files were not deleted. It's not a full-engine take-off.

On the direct request not to accumulate old versions after a successful replacement, the excess OBJ/EXE/DLL/LIB/EXP from the old `build/gameview-engine-2026-10-08-attempt06`–`attempt09` is removed. A complete list of absolute paths, cause and recovery were reported **to** Apply: [cleanup notice](GAMEVIEW_BUILD_CLEANUP_NOTICE_2026_10_08_UA.md).

| Root | Files are deleted | Logical bytes |
| --- | ---: | ---: |
| attempt06 | 21 OBJ | 59 793 017 |
| attempt07 | 27 compiled files | 113 414 047 |
| attempt08 | 27 compiled files | 113 424 631 |
| attempt09 | 27 compiled files | 113 427 269 |
| Together | **102** | **400 058 964** |

## Actual result

- Plan created by actual exit0; valid Apply ended with actual exit0.
- `complete=true`, `exit_code=0`, `removed_file_count=102`, `remaining_file_count=0`, exactly `remaining_files: []`.
- All **3859 protected files** re-checked after cleaning: bytes/mtime/SHA-256 unchanged; Actually, all 102 targets are missing.
- `protected_changed=false`, `protected_error=null`, `error=null`.
- Free space before/after: 95 676 100 608 → 96 076 316 672 bytes.
- The observed increase in — **400 216 064 bytes / 400,22 MB**. This is an observed change in disk, not a promise of logical bytes and physical space or the absence of parallel Windows activity.

Wrong-SHA negative ended with actual exit1 before deletion:102/102candidate bytes/mtime/hash remained unchanged, the result file was not created. Only then is the correct constant SHA applied.

## What's Left

The full GameView05 rollback and GameView10 latest, native01–07, sky-authored05/10 and authored-native01– 06, the entire check05 kit (export-a/export-b/negative evidence),96 live source bindings, launchers/pointer, and the actual launcher EXE are preserved. The non-target snapshots/logs/reports/assets/shaders/generated/compiler-temp old roots are also preserved. The catalogs were not deleted.

The operation is the final local removal of compiled copies, not a basket and not an GitHub archive. The saved resources/dependencies allow the relevant program to be rebuilt; The exact restoration of the historical EXE/OBJ is not guaranteed. Historical hashes of deleted executables remain records, not saved runnable copies.

## Evidence binding

- Plan: `.omx/plans/gameview-build-prune-2026-10-08-01.json`, SHA-256 `2F0E381B68026EAF596D8F58C8AE4BEE6A2DD45CA2DCCF0E7C1B8C0980EDE56A`.
- Applied helper: `tools/prune_gameview_build_generated.ps1`, SHA-256 `B376B91302865938664C8A78540AE0D4E0E470E6F8D9A9BC914E96B3F8680307`.
- Result: `.omx/plans/gameview-build-prune-2026-10-08-01.json.result.json`, SHA-256 `9BE6A53084B5152E1504875954E4C23B004D8D13E63DDCCE0D7D8A4C25C88ADD`.
- Replacement: [bounded window-light build10/native06–07](visual-targets/GAMEVIEW_WINDOW_LIGHT_CHECK_2026_10_08_UA.md). Her EXE/source/kit/native PNG and reports are entrusted to cleansing and protected during it.

Helper has a fixed allowlist for this very operation, not a general automatic cleaner. Repeat Apply is not a new permission to other roots. Future cleaning requires a separate exact manifest and user notification, with the latest + one verified rollback and compact proof preserved.

Full engine, artistic appearance, FPS/120FPS, video, normal startup and all E01–E17 are left OPEN.
