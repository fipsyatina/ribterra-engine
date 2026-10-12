# Environment build01 — компактне зберігання невдалої збірки

08.10.2026. **Завершено; actual Apply exit0.** Заміна environment build02
пройшла strict build і два bounded native GPU прогони. Перед видаленням
користувачеві повідомлено точний root, manifest усіх 21 OBJ, розмір,
причину та обмеження відновлення. GitHub archive не створювався.

Видалені тільки 21 named `.obj` leaf у
`build/gameview-environment-2026-10-08-attempt01/obj`, 60 070 592 logical bytes.
Не видалено каталогів, коду, ресурсів, shaders, логів, звітів або saves.
Object copies можна повторно скомпілювати; byte-exact старі OBJ не гарантовані.
Історичний failed-build report більше не має цих compiled inputs на диску.

## Захист та перевірка

- Fixed allowlist; кожний leaf resolved і перевірений на boundary, reparse,
  file type, exact bytes і SHA безпосередньо перед literal-path deletion.
- Default dry-run; Apply потребує exact plan/helper SHA, fresh result і
  повністю незмінних target/non-target/protected inventories. Live refs
  перевіряються fail-closed. Recursive removal/moving відсутні.
- Wrong-plan-SHA negative: actual exit1 before deletion; усі 21 targets
  exact, negative result не створено.
- Independent preflight **CLEAR**: target allowlist/hash aggregate exact,
  no reparse/path escape/live references/protected overlap.
- Actual Apply exit0: success=true, removed21, remaining[], errors[].
- Main post-Apply audit повторно звірив 2 228 збережених rows: zero changed;
  усі 21 повідомлені targets відсутні.
- Independent post-Apply audit **CLEAR**: exact plan/helper/result, applied
  rows, 21 absent targets,490 non-target і1738 protected rows знову звірені.
- Failed01 non-target evidence: 490 файлів / 2 826 540 bytes exact.
  Дев'ять protected inventories включають environment02/native01/native02,
  atmosphere03/native02/native03, water02 rollback, shared control kit,
  обидва launcher/pointer і поточні dist executable targets.

Observed volume free space: 94 157 840 384 → 94 216 986 624 bytes,
delta **59 146 240 bytes** (~59.15 MB). Це спостережуваний приріст,
не тотожність logical deleted bytes і не загальне вимірювання диска/VRAM.
Samples і actual negative exit записані в operator/tool transcript, не в
immutable result sidecar; незалежний audit підтвердив arithmetic, не
походження цих samples або exclusive causality free-space delta.
Видалення leaf copies не transactional: partial failure зупиняє наступні
видалення й записує applied/remaining/errors, але не відновлює вже видалені OBJ.

## Прив'язка доказів

| Файл | SHA-256 |
|---|---|
| `tools/prune_environment_build_copies.ps1` | `691CD5187957E47BCBE24FA962758DBB87FD41F8DB2E1A7501C93E7E73B473AB` |
| `build/environment-attempt01-prune-plan01.json` | `FD7A9D0352DC4FD98C77430B002D1857D35841BEDB7D09BCF511CB82AE8E8D7D` |
| `build/environment-attempt01-prune-result01.json` | `D84B04E15494A5FC89C7A94361D9A2BB98986BA5C7E16AA239C9CA2A2BA2510F` |

Plan 791 332 bytes; result 791 417 bytes; target inventory SHA
`755F5B085A6EFEDCB22C27D55B328AC650331A154EA95A99C659DE34EA60F121`.
Цей helper має історичний fixed scope; не використовувати для інших roots.
Поточний [Environment GPU checkpoint](visual-targets/ENVIRONMENT_CHECK_2026_10_08_UA.md)
залишається відтворюваним. Full E06/E01–E17/120 FPS/concept acceptance OPEN.
