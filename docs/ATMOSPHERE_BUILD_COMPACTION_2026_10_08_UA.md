# Компактне зберігання невдалої Atmosphere build02

08.10.2026. Користувач вимагає не накопичувати старі повні версії.
Після build03/native02–03 PASS невдала попередня build02 була замінена.
До видалення повідомлено exact 27-file plan, root, 112.94 MB, причину,
збережені дані й втрату runnable старої копії; GitHub upload не виконувався.

- Scope: тільки 22 named OBJ і `GameViewWaterProbe.exe/.lib/.exp`,
  `SDL3.dll`, `SDL3_ttf.dll` під `build/gameview-atmosphere-2026-10-08-attempt02`.
- Plan `build/atmosphere-attempt02-prune-plan01.json`, SHA256
  `C50DE4DA5A7B991924049035965ED9339082C24617883E72E924AC2DE9D0C5C5`.
- Frozen helper `tools/prune_atmosphere_build_copies.ps1`, SHA256
  `CF710722C1AE835AC92540560F4C845BB1FE917F6F0D7A3CAD4FE7117EAF4ECE`.
- Незалежний pre-apply аудит підтвердив exact allowlist, no live refs/reparse,
  unchanged target/protected inventory і immediate hash check перед кожним
  literal leaf deletion. Recursive delete/move відсутні.
- Apply фактичний exit0; result `build/atmosphere-attempt02-prune-result01.json`.
  27 compiled leaves, 112 939 136 logical bytes; free space
  94 598 234 112 → 94 710 472 704 bytes, observed delta **112 238 592 bytes**.
  Free-space delta не гарантується рівним logical size.

Незалежний post-Apply аудит з диска підтвердив 27 absent targets, exact 494
retained files і всі сім protected inventories; `remaining_targets=[]`,
`verification_errors=[]`. Result SHA256
`E1E58F08280AEDA85DB1630CE544DA7E479AE040C3F0FA96948AE2113AAE646B`.

Захищено 494 non-target files невдалої збірки (3 770 050 bytes), latest03,
усі три Atmosphere native outputs, verified Water build02 rollback,
control kit і чинні launchers/targets. Source snapshots, textures, shaders,
logs, reports і saves не видалялись. Каталоги не видалялись.

Відновлення старого EXE потребує recompilation; byte-identical EXE не
гарантується. Старий failed build report лишився історичним, але тепер
не self-verifying за всіма своїми compiled input paths. Current proof03 і
rollback повністю збережені. Цей helper/plan не є cleaner для нових roots.
