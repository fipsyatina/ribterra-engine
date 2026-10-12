# Old package builds — bounded cleanup

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

08.10.2026. **Finished; actual Apply exit0, independent post-audit CLEAR.**
The user is notified before deleting all 16 roots under `artifacts`, exact
manifest104 leaves, size and permanent-loss/no-GitHub-archive caveat.

Removed 104 compiled leaves /1 506 551 808 logical bytes:
72 EXE [1 428 695 040 bytes) and 32 DLL (77 856 768 bytes). Roots:

```text
package-0.4.0-smoke       package-0.5.0-final
package-0.5.0-fixed       package-0.5.0-smoke
package-0.6.0-verified    package-0.6.0-warning-clean
package-0.7.0-final       package-0.8.0-final
package-0.9.0-final       package-0.10.0-final
package-0181-f7923b25     package-0182-c5d49729
package-0190-6409ac88     package-0191-2a0b8951
package-0200-e89bd797     package-0201-32b1e4af
```

These historical extracted packages ** are no longer runnable**. 72 EXE have 68
They had unique hashes and no protected byte-identical counterparts. Theirs
The exact binaries are lost; recompilation can restore functionality,
But it doesn't guarantee identical bytes. DLL copies have exact protected counterparts.
Historical validation prose is not proof of current runnability.

## Protection and Enforcement

- Fixed per-root relative leaf allowlists, not package wildcard/age-prune.
  Immediate path/boundary/reparse/regular-file/bytes/SHA before each
  literal leaf removal; No recursive removal, move or directory deletion.
- Dry-run default; Apply needs exact plan/helper SHA, fresh result,
  unchanged target/non-target/protected inventories, disjointness and
  fail-closed live-process/module audit.
- Independent preflight CLEAR; wrong-plan-SHA negative actual1 before
  deletion,104 targets exact/no negative result (operator transcript).
- Actual Apply exit0: success=true,104 removed, remaining[], errors[].
- Main and independent post-audit:104/104 absent, zero remaining compiled
  extensions in16 roots;1405 retained rows /244 071 260 bytes and3152 protected
  rows /1 816 826 662 bytes rehashed exact, no missing/extra/changed files.
- Protected full launch-fix-0.10.2, package-0.10.1-final rollback, both
  current dist launcher distributions, launchers/pointer/executable bindings,
  environment/atmosphere/water latest proofs and shared kits.
- There is no deletion code, original art, assets, saves, reports, captures,
  manifests, archives, or directories.1405 retained files contain 17 saves.

Observed volume free space94 219 141 120→95 724 896 256 bytes,
delta **1 505 755 136 bytes** (~1.51 GB). Samples exist in the transcript operator,
not immutable result; independent audit confirmed arithmetic, not sampling
provenance/exclusive causality. This is not a logical allocation.
Partial failure not transactional: helper stops further leaf deletes and
records applied/remaining/errors, but does not return the already deleted binaries.

## Evidence

| The file | SHA-256 |
|---|---|
| `tools/prune_legacy_package_copies.ps1`,23 924 bytes | `41F64935E47BF48572C73670ED5A70018FC813CBEDBA5866F46AA60C809BC30E` |
| `build/legacy-package-prune-plan01.json`,1 758 626 bytes | `388783D2806763E77B627AAA9A55CB5041DEDC5911A61355759741DFA47A4576` |
| `build/legacy-package-prune-result01.json`,1 758 683 bytes | `9E9D3AB64EABE2AFE506DAF9CD6C9DDB83CA291DDB6E31A0B240FE483448FFD6` |

Target inventory SHA `A6D084E7FB5D8EF4F41042846A4013962D806C995F08B34372A9009552B20D97`.
After this read-only operation `artifacts`:35 026 files /27 521 162 198
logical bytes. This is not the size after other future purges.
Helper historical fixed scope; Don't apply the old plan to the new packages.
