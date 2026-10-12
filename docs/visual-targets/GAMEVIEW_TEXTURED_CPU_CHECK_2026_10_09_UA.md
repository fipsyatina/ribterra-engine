# Textured material: прийнятий CPU checkpoint, 09.10.2026

Прийнято конкретний CPU-пайплайн нейтральних RGB-текстур та авторських UV. Parent actual72433 і незалежний retained-data reviewer63511 завершилися exit0. Це не приймання GPU, художнього вигляду, звичайного запуску, FullFX або FPS.

Producer: `.unlazy/engine-completion-2026-10-08/neutral-texture-boundary-candidate/tools/export_material_master.py`, 118526 B, SHA256 `820F5AACD2BBF8CC5CC95984250DCF95AD8357EB3EF9D8AFE42A981A83752699`. Live exporterEB6 не замінювали.

Незалежний тест: у тому самому candidate, `tests/gameview_textured_material_export_tests.py`, 118508 B, SHA256 `7DDDB89676DA31B7CDA25697A6F62E6304F751CBB0CB1FA2E01698789907DDB5`. Він використовує точні раціональні UV-площини, виведені з XYZ. Producer доводить однозначний texel через outward intervals і застосовує exact Fraction fallback на неоднозначних межах. Новий алгоритм вибору RGB не змінює геометричне покриття, depth/winner або решту п’яти каналів.

ActualCPU05: `build/textured-material-export-tests-2026-10-09-attempt05/textured-material-export-test-report.json`, 2966217 B, SHA256 `A4721B653A87DE0858E72865DD69101F9B20E989D476376F4BB32002B6CB6C59`.

- 92 actual CLI: 29 exit0, 63 очікувані exit2; complete=true, errors=[].
- 73 protected inputs незмінні; три старі baseline inventories збережено точно.
- 92 повні OS child lifetime RSS counters; жодного unavailable. Найбільший цілий дочірній процес — 348221440 B. Це не вимір компонента 80 MiB.
- Власний CPU root: 1103 файли / 79784220 B. 29 retained roots: сумарно 252183998 B; обмеження 128 MiB застосовується до кожного root окремо.
- Actual cases перевірили 3/12/24 MiB source-role межі, shared sources, 8/9 assets, 64/65 та128/129 origins, dimensions і стару сумісність. Component case64 MiB retained channels +16 MiB transient прийнято;96 MiB відхилено.

Незалежний reader: `.unlazy/engine-completion-2026-10-08/neutral-texture-cpu-review-candidate/tests/review_textured_material_cpu_evidence.py`, 45020 B, SHA256 `984CB70B25426175836AD75954779E66E335155AC1DB89FEBA1CB5F66D61FA54`.

Його actual receipt: `build/textured-material-cpu-review-2026-10-09-attempt01/receipt.json`, 2635371 B, SHA256 `BF34DDFB046748A8416F0C4AD175C6109FCAC243A95D26A55CBA7EA7CDE7BB47`. Accepted=true, complete=true, errors=[]. Заново перевірено 26 успішних textured exports /38 assets: 6238196 canvas pixels, 6233320 covered samples, усі шість каналів. Це незалежний запуск і перевірка pinned7DDD oracle, а не третій незалежно написаний алгоритм. Reader не запускав producer, fixtures або нові exports; його власний RSS не виміряно.

RGB-only та UV-only змінили4922 та19596 pixels відповідно, з точно незмінними alpha й іншими п’ятьма каналами. Nextafter cases нижче/на/вище внутрішньої межі та constant-axis scaling перевірено без tolerance snapping. Повторні шість каналів побайтно однакові. Старі calibration/flat/painted nonreport outputs також повторено точно.

Root додатково перехешував77 report/source/protected rows. Підсумкова direct receipt: `.unlazy/engine-completion-2026-10-08/textured-material-cpu-accepted-2026-10-09.json`, 1154 B, SHA256 `F6CEE16A21AA2F6A0087E65B8EF342CA170DBF5E4C3A18C95270865E708616CB`.

Невдалі CPU01–04, усі SOURCE-попередники, точні preimages та aliases збережено. Файлів не видаляли. Automatic gate certification не заявляється.

Цей checkpoint дозволяє окрему генерацію текстур House і подальшу інтеграцію нового opt-in profile. Actual генерація та export двох House drafts уже виконані; їхня окрема незалежна перевірка ще триває. Production8masters, структурна деталізація будинку, emitters/contacts, три референси, фізичні desktop input/video, FullFX, packaging і ≥120 FPS залишаються відкритими. Повна ціль активна.
