# Компактне зберігання GameView engine-перевірок — 08.10.2026

**Виконано лише повідомлене очищення 102 compiled-копій.** Код, арт, збереження гравця, звіти, PNG та чинні запускні файли не видалялися. Це не приймання повного двигуна.

На пряму вимогу не накопичувати старі версії після успішної заміни прибрано зайві OBJ/EXE/DLL/LIB/EXP зі старих `build/gameview-engine-2026-10-08-attempt06`–`attempt09`. Повний список абсолютних шляхів, причина й відновлення були повідомлені **до** Apply: [cleanup notice](GAMEVIEW_BUILD_CLEANUP_NOTICE_2026_10_08_UA.md).

| Root | Видалено файлів | Логічних байтів |
| --- | ---: | ---: |
| attempt06 | 21 OBJ | 59 793 017 |
| attempt07 | 27 compiled files | 113 414 047 |
| attempt08 | 27 compiled files | 113 424 631 |
| attempt09 | 27 compiled files | 113 427 269 |
| Разом | **102** | **400 058 964** |

## Фактичний результат

- Plan створено фактичним exit0; valid Apply завершився фактичним exit0.
- `complete=true`, `exit_code=0`, `removed_file_count=102`, `remaining_file_count=0`, точний `remaining_files: []`.
- Усі **3859 protected files** повторно перевірені після очищення: bytes/mtime/SHA-256 незмінні; фактично всі102 цілі відсутні.
- `protected_changed=false`, `protected_error=null`, `error=null`.
- Вільне місце перед/після: 95 676 100 608 → 96 076 316 672 байтів.
- Спостережуваний приріст — **400 216 064 байтів / 400,22 MB**. Це спостережувана зміна диска, не обіцянка тотожності logical bytes і фізичного місця чи відсутності паралельної активності Windows.

Wrong-SHA negative завершився фактичним exit1 до видалення:102/102candidate bytes/mtime/hash лишилися незмінними, result-файл не створено. Лише потім застосовано правильний незмінний SHA.

## Що залишилося

Повністю збережені GameView05 rollback і GameView10 latest, native01–07, sky-authored05/10 і authored-native01–06, увесь kit check05 (export-a/export-b/negative evidence),96 live source bindings, launchers/pointer та фактичні launcher EXE. Нецільові snapshots/logs/reports/assets/shaders/generated/compiler-temp старих roots теж збережені. Каталоги не видалялися.

Операція є остаточним локальним видаленням compiled-копій, не кошиком і не GitHub-архівом. Збережені джерела/залежності дозволяють перебудувати відповідну програму; побайтове відновлення історичних EXE/OBJ не гарантується. Історичні hashes видалених executable залишаються записами, а не збереженими runnable copies.

## Прив'язка доказів

- Plan: `.omx/plans/gameview-build-prune-2026-10-08-01.json`, SHA-256 `2F0E381B68026EAF596D8F58C8AE4BEE6A2DD45CA2DCCF0E7C1B8C0980EDE56A`.
- Applied helper: `tools/prune_gameview_build_generated.ps1`, SHA-256 `B376B91302865938664C8A78540AE0D4E0E470E6F8D9A9BC914E96B3F8680307`.
- Result: `.omx/plans/gameview-build-prune-2026-10-08-01.json.result.json`, SHA-256 `9BE6A53084B5152E1504875954E4C23B004D8D13E63DDCCE0D7D8A4C25C88ADD`.
- Заміна: [bounded window-light build10/native06–07](visual-targets/GAMEVIEW_WINDOW_LIGHT_CHECK_2026_10_08_UA.md). Її EXE/source/kit/native PNG і звіти звірені до очищення та захищені під час нього.

Helper має фіксований allowlist саме цієї операції, не є загальним автоматичним cleaner. Повторний Apply не є новим дозволом на інші roots. Майбутнє очищення потребує окремого exact manifest і повідомлення користувачеві, зі збереженням latest + одного перевіреного rollback та компактного доказу.

Повний двигун, художній вигляд, FPS/120FPS, відео, normal startup та всі E01–E17 лишаються OPEN.
