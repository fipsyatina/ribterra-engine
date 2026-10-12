# Старі package-збірки — обмежене очищення

08.10.2026. **Завершено; actual Apply exit0, independent post-audit CLEAR.**
Користувачеві до видалення повідомлено всі16 roots під `artifacts`, exact
manifest104 leaves, розмір і permanent-loss/no-GitHub-archive caveat.

Видалено104 compiled leaves /1 506 551 808 logical bytes:
72 EXE (1 428 695 040 bytes) і32 DLL (77 856 768 bytes). Roots:

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

Ці історичні extracted packages **більше не runnable**. 72 EXE мають68
унікальних hashes і не мали protected byte-identical counterparts. Їхні
точні binaries втрачені; recompilation може відновити функціональність,
але не гарантує identical bytes. DLL copies мають exact protected counterparts.
Historical validation prose не є доказом поточної runnability.

## Захист і виконання

- Fixed per-root relative leaf allowlists, не package wildcard/age-prune.
  Immediate path/boundary/reparse/regular-file/bytes/SHA перед кожним
  literal leaf removal; no recursive removal, move або directory deletion.
- Dry-run default; Apply потребує exact plan/helper SHA, fresh result,
  unchanged target/non-target/protected inventories, disjointness і
  fail-closed live-process/module audit.
- Independent preflight CLEAR; wrong-plan-SHA negative actual1 before
  deletion,104 targets exact/no negative result (operator transcript).
- Actual Apply exit0: success=true,104 removed, remaining[], errors[].
- Main й незалежний post-audit:104/104 absent, zero remaining compiled
  extensions у16 roots;1405 retained rows /244 071 260 bytes та3152 protected
  rows /1 816 826 662 bytes rehashed exact, no missing/extra/changed files.
- Protected full launch-fix-0.10.2, package-0.10.1-final rollback, обидві
  current dist launcher distributions, launchers/pointer/executable bindings,
  environment/atmosphere/water latest proofs та shared kit.
- Немає deletion коду, original art, assets, saves, reports, captures,
  manifests, archives чи каталогів.1405 retained files містять17 saves.

Observed volume free space94 219 141 120→95 724 896 256 bytes,
delta **1 505 755 136 bytes** (~1.51 GB). Samples існують в operator transcript,
не immutable result; незалежний audit підтвердив arithmetic, не sampling
provenance/exclusive causality. Це не logical allocation = physical free claim.
Partial failure не transactional: helper зупиняє подальші leaf deletes і
записує applied/remaining/errors, але не повертає вже видалені binaries.

## Докази

| Файл | SHA-256 |
|---|---|
| `tools/prune_legacy_package_copies.ps1`,23 924 bytes | `41F64935E47BF48572C73670ED5A70018FC813CBEDBA5866F46AA60C809BC30E` |
| `build/legacy-package-prune-plan01.json`,1 758 626 bytes | `388783D2806763E77B627AAA9A55CB5041DEDC5911A61355759741DFA47A4576` |
| `build/legacy-package-prune-result01.json`,1 758 683 bytes | `9E9D3AB64EABE2AFE506DAF9CD6C9DDB83CA291DDB6E31A0B240FE483448FFD6` |

Target inventory SHA `A6D084E7FB5D8EF4F41042846A4013962D806C995F08B34372A9009552B20D97`.
Після цієї операції read-only `artifacts`:35 026 файлів /27 521 162 198
logical bytes. Це не розмір після інших майбутніх очищень.
Helper historical fixed scope; не застосовувати старий план до нових пакетів.
