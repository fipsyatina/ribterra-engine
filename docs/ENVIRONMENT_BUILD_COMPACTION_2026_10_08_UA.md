# Environment build01 — Compact storage of failed set

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **Finished; actual Apply exit0.** The replacement environment build02
passed a strict build and two bounded native GPU runs. Before removal, the user
was given the exact root, the manifest of all 21 OBJ files, the size,
the reason, and the recovery limitations. No GitHub archive was created.

Removed only 21 named `.obj` leaf in
`build/gameview-environment-2026-10-08-attempt01/obj`, 60 070 592 logical bytes.
Not deleted directories, code, resources, shaders, logs, reports or saves.
Object copies can be recompiled; The byte-exact OBJ is not guaranteed.
The historical failed-build report no longer has these compiled inputs on the drive.

## Protection and verification

- Fixed allowlist; Each leaf resolved and checked on the boundary, reparse,
  file type, exact bytes and SHA right before literal-path deletion.
- Default dry-run; Apply needs exact plan/helper SHA, fresh result and
  completely unchanged target/non-target/protected inventories. Live refs
  are checked fail-closed. The recursive removal/moving is missing.
- Wrong-plan-SHA negative: actual exit1 before deletion; All 21 targets
  Exact, negative result is not created.
- Independent preflight **CLEAR**: target allowlist/hash aggregate exact,
  no reparse/path escape/live references/protected overlap.
- Actual Apply exit0: success=true, removed21, remaining[], errors[].
- Main post-Apply audit reconfirmed 2 228 of the saved rows: zero changed;
  All 21 reported targets are missing.
- Independent post-Apply audit **CLEAR**: exact plan/helper/result, applied
  rows, 21 absent targets,490 non-target and 1738 protected rows again wild.
- Failed01 non-target evidence: 490 files / 2 826 540 bytes exact.
  The nine protected inventories include environment02/native01/native02,
  atmosphere03/native02/native03, water02 rollback, shared control kit,
  Both launcher/pointer and current dist executable targets.

Observed volume free space: 94 157 840 384 → 94 216 986 624 bytes,
delta **59 146 240 bytes** (~59.15 MB). This is an observed increase.
not the logical deleted bytes and not the total disk size/VRAM.
Samples and actual negative exit recorded in operator/tool transcript, not in
immutable result sidecar; independent audit confirmed the arithmetic, not
The origin of these samples is the exclusive causality free-space delta.
Removing leaf copies is not transactional: partial failure stops the following
deletes and records applied/remaining/errors, but does not recover already deleted OBJ.

## Evidence binding

| The file | SHA-256 |
|---|---|
| `tools/prune_environment_build_copies.ps1` | `691CD5187957E47BCBE24FA962758DBB87FD41F8DB2E1A7501C93E7E73B473AB` |
| `build/environment-attempt01-prune-plan01.json` | `FD7A9D0352DC4FD98C77430B002D1857D35841BEDB7D09BCF511CB82AE8E8D7D` |
| `build/environment-attempt01-prune-result01.json` | `D84B04E15494A5FC89C7A94361D9A2BB98986BA5C7E16AA239C9CA2A2BA2510F` |

Plan 791 332 bytes; result 791 417 bytes; target inventory SHA
`755F5B085A6EFEDCB22C27D55B328AC650331A154EA95A99C659DE34EA60F121`.
This helper has a historic fixed scope; Don't use other roots.
Current [Environment GPU checkpoint](visual-targets/ENVIRONMENT_CHECK_2026_10_08_UA.md)
It's still a fake. Full E06/E01–E17/120 FPS/concept acceptance OPEN.
