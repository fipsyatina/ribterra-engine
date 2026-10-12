# Storage of collection and verification results

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

User requirement from 2026-10-08: not to store all previous versions in `artifacts`. Outdated generated copies should be cleaned; archiving on GitHub is not automatic.

[Clearly limited clearance 08.10](ARTIFACT_CLEANUP_2026_10_08_UA.md): deleted 3 684 old program copies, 82,79 GB logical data; the observed increase in free space 78,09 GB. The collection, art and current launch kits are protected.

[Additional compact storage authored checks](AUTHORED_BUILD_COMPACTION_2026_10_08_UA.md): removed 56 outdated compiled copies, 228,21 MB logical data; the additional observed increase 227,87 MB. All 4303 protected files are unchanged.

[Complete storage GameView checks](GAMEVIEW_BUILD_COMPACTION_2026_10_08_UA.md): removed102 reported compiled-copies06–09,400,06 MB of logical data; The observed increase is 400,22 MB. All 3859 protected files are unchanged,05rollback/10latest and compact evidence is preserved.

## The Working Rule

[Old atmosphere build02](ATMOSPHERE_BUILD_COMPACTION_2026_10_08_UA.md):
After verified replacement and exact path notification removed only 27 compiled
copies /112.94 MB logical; observed free-space delta112.24 MB. Current03,
Water rollback02, source/assets/native results and launcher targets are saved.

[Environment build01 cleanup](ENVIRONMENT_BUILD_COMPACTION_2026_10_08_UA.md):
After verified build02/native01–02 replacement, removed21 reported OBJ /
60.07 MB logical; observed free delta59.15 MB. All 2228 retained/protected
rows exact; code/resources/current runtime/native evidence/launchers are saved.

[Old 16 package-collections](LEGACY_PACKAGE_COMPACTION_2026_10_08_UA.md):
after exact user notification, removed104 old EXE/DLL files /1.507 GB logical;
observed volume free delta1.506 GB.1405 retained and3152 protected rows exact;
These historical extracted packages are no longer runnable, current dist/rollback
packages and original art/saves/captures are not attached. GitHub archive is not created.

- It is also a rule for the following agents in the root [AGENTS.md](../AGENTS.md), rather than just a recommendation for a single cleaning. After verified replacement, remove the reported obsolete full copies until the next versions are accumulated; not create full runtime for each test attempt.
- For the new version, the current launch-ready package and one previously verified version for the boot up remain. The packet pointed to by any current launcher shall not be removed without an agreed replacement of that launcher.
- After a successful replacement of the collection do not leave additional full copies of its programs in each test directory. To store compact reports, hashes, you need controls PNG and a replicable verification code. A historical record does not mean that its executable is preserved forever.
- For current isolated engine checks, the limits of 128 MiB logical data on the root and at least 384 MiB free space remain. Before creating a next root, check the need for the previous one. After the replacement is made, clean out the outdated copies.
- A failed attempt remains to be diagnosed and correction confirmed. Then enough of the necessary compact counter evidence; It's not going to store its object files, DLL and export them indefinitely.
- Do not delete by the general pattern `artifacts/*`, `build/*` or `*runtime*`. The source code, the originals of the art, the concepts, the player's preservation and the actual dependencies of the current verification  are separate protected data.

The repeated read-only dimension 08.10.2026: `artifacts` contains 35 130 files, ** 29 027 714 006 logical bytes (29,03 GB / 27,03 GiB) **; The reparse-transitions have not been detected. This is the current size after the previous cleaning, not the new cleaning. In this iteration, no files are deleted and nothing is published on GitHub.

## Before each cleanup

1. Measure specific directories, identify current launcher and verification dependencies, separate originals from generated copies.
2. Create an accurate dry-run manifest with absolute paths, sizes, and SHA-256. Verify the belonging to the clear allowlist and the absence of reparse/symlink- transitions.
3. Notify the user of the specific path/manifest, extent, cause, and possibility of recovery ** to ** deletion. This rule also applies to files and temporary files. The current clearance of old versions does not allow for the hidden deletion of other data.
4. Remove only reported targets after a repeat. Record the actual result, check the protected files, and report the actual location.

## Limited clearance of historical test runtime

`tools/prune_artifact_runtime_copies.ps1` works only with an explicit list of historical test cohorts 2026-10-05–07. By default, it **only** creates a dry-run manifest. `-Apply` needs SHA-256 to be notified of the manifest; checks every file before and immediately before it's deleted. The script deletes only copies `.exe/.dll/.lib/.pdb/.obj/.ilk/.exp`, does not delete directories, PNG, texture assets, source snapshots, reports, or save files. This is not a universal automatic cleaner for future versions.

Cleansed executable copies cannot be reused for historical scrolling. A new limited collection/verification is required for a new application. The compactly preserved reports remain historical, not current, confirmation of engine readiness.

## Limited cleaning authored compiled copy

`tools/prune_authored_build_copies.ps1` has a separate apparent allowlist: 14 `.exe/.dll/.obj` in each of the four `build/sky-authored-2026-10-08-attempt06`–`attempt09`. It's not a universal cleaner. By default, only a new dry-run manifest is created; Apply needs its notified SHA-256 and unchanged helper. The protection covers all non-targeted files of these roots, fully latest attempt10 and rollback attempt05, GameView engine build/native roots and current launcher targets. The setting of the track is not dependent on the order of the recordings. After the plan04 is executed, the same compiled-targets are no longer present; A new cleanup of a different scope requires a separate plan and a separate message.

## Limited clearance GameView compiled-copy

`tools/prune_gameview_build_generated.ps1` has a separate fixed allowlist:21OBJ v06 and 27compiled files in each07–09. The unknown OBJ doesn't become a target through the common globus. Apply requires fresh build10/source/kit/native bindings, exact Plan/helper SHA and a message of all 102 paths. After Apply re-confident the protected inventory; Non-zero remaining or change protected gives failure. Plan01 is executed; His targets are gone. New roots are not removed by this helper automatically.
