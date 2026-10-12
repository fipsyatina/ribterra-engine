# Compact storage fails Atmosphere build02

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. The user requires not to accumulate old full versions.
After the failure of build03/native02–03 PASS the previous build02 was replaced.
Before deleting the exact 27-file plan, root, 112.94 MB, cause,
stored data and loss of runnable old copy; GitHub upload failed.

- Scope: only 22 named OBJ files, `GameViewWaterProbe.exe/.lib/.exp`,
  `SDL3.dll`, and `SDL3_ttf.dll` under `build/gameview-atmosphere-2026-10-08-attempt02`.
- Plan `build/atmosphere-attempt02-prune-plan01.json`, SHA256
  `C50DE4DA5A7B991924049035965ED9339082C24617883E72E924AC2DE9D0C5C5`.
- Frozen helper `tools/prune_atmosphere_build_copies.ps1`, SHA256
  `CF710722C1AE835AC92540560F4C845BB1FE917F6F0D7A3CAD4FE7117EAF4ECE`.
- An independent pre-apply audit has confirmed the exact allowlist, no live refs/reparse,
  unchanged target/protected inventory and immediate hash check before each
  literal leaf deletion. The recursive delete/move is missing.
- Apply the actual exit0; result `build/atmosphere-attempt02-prune-result01.json`.
  27 compiled leaves, 112 939 136 logical bytes; free space
  94 598 234 112 → 94 710 472 704 bytes, observed delta **112 238 592 bytes**.
  Free-space delta is not guaranteed to be of equal logical size.

An independent post-apply audit from the drive confirmed 27 absent targets, exact 494
retained files and all seven protected inventories; `remaining_targets=[]`,
`verification_errors=[]`. Result SHA256
`E1E58F08280AEDA85DB1630CE544DA7E479AE040C3F0FA96948AE2113AAE646B`.

Protected 494 non-target files by failed set (3 770 050 bytes), latest03,
All three Atmosphere native outputs, verified Water build02 rollback,
control kit and actuators launchers/targets. Source snapshots, textures, shaders,
logs, reports and saves were not deleted. The catalogs were not deleted.

Restoring the old EXE requires recompilation; a byte-identical EXE is not
guaranteed. The old failed-build report remains historical, but it is no longer
self-verifying for all of its compiled input paths. Current proof03 and the
rollback are fully preserved. This helper/plan is not a cleaner for new roots.
