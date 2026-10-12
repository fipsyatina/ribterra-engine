# Напрямлені тіні Sun/Moon — частковий engine checkpoint

08.10.2026 (Europe/Warsaw). **BOUNDED PREREQUISITE VERIFIED / ПОВНИЙ ДВИГУН НЕ ПРИЙНЯТО.**

У production renderer додано явний opt-in `SkyShadowSlice`: незалежні карти глибини сонця й місяця, які затінюють тільки внесок відповідного джерела на справжньому ground P та reconstructed P typed sprite. Це не новий вигляд гри: звичайний GameView лишається BakedCompat, Dynamic unavailable, launcher незмінний. **E01–E17, відповідність концептам і ≥120 FPS у 1920×1080 залишаються OPEN.** До gameplay/HUD/генератора/каталогу не переходили.

## Що реалізовано і перевірено

- Frame-owned непрозорі трикутники XYZ, source mask 1/2/3; не тінь, намальована в PNG, і не повторне використання contact AO або local OBB/capsule.
- Дві D32_FLOAT DEPTH_STENCIL_TARGET|SAMPLER карти 1024² для кожного з двох frame slots. Combined format support перевіряється; fallback не видається за ввімкнений ефект.
- SunShadow → MoonShadow → попередній ground/forward/composite/UI graph. Ground і typed sprite семплюють кожну карту за власною позицією, а не ground-behind-sprite.
- Nearest depth, двосторонні трикутники, nearest 3×3 PCF, явне visible=1 поза coverage/tap. Базис спільний для raster/sample, обчислюється з нормалізованого напрямку.
- Перевірка всіх raw rows, включно з reveal=0: finite/nondegenerate XYZ у включному camera-relative ±32 cube, маски та reveal [0,1], raw≤4096, active≤1024/source. Reveal=0 не rasterizes; будь-яке reveal>0 є повністю непрозорим caster, **не частковим alpha/fade**.
- Увімкнені джерела цього prerequisite потребують elevation≥20°. Це обмеження, не готова політика сходу/заходу чи низького місяця.
- Чотири depth targets створюються транзакційно; fault на candidate №3 не публікує напівнабір, повторна спроба успішна. Frame fences перед повторним використанням; RAII lifetime.
- Матеріальний ABI: новий явний `SkyMoonLighting192` у b3 = Moon32 + Shadow160. Ground має окремі Moon32/b1 і Shadow160/b2. Не використовуються reserved legacy поля; Surface384/Sprite160/Metadata32 незмінні. Не більше чотирьох uniform buffers/stage — фактична межа pinned SDL 3.4.16.

16 MiB — номінальний payload чотирьох D32-карт, **не** вимірювання driver allocation, peak VRAM чи продуктивності.

## Фактичні докази

Єдина фінальна capsule: `build/sky-shadow-2026-10-07-attempt09/`. Назва збережена з початку роботи; фінальні перевірки виконано 08.10 за місцевим часом. Fresh standalone MSVC C++20/O2/MT/W4/WX/permissive/UTF-8 snapshot build; не повний CMake/CTest або game package. Helper і verifier завершилися з actual exit0.

| Перевірка | Фактичний результат |
|---|---|
| SkyShadow CPU / Moon CPU / Contact CPU | 110 / 51 / 211 assertions PASS |
| 17 shader programs / 68 DXIL+reflection artifacts | Строгі bindings, ABI, include closure і hashes PASS |
| Shader negatives | Відхилено 48 register/space/output/ABI mutations, 20 direct + 1 transitive include-hash mutations, 2 noncanonical includes, owner/capsule-axis/stride, 2 numerical, 3 moon-independence і 4 compiled shadow-semantic mutations |
| Native SkyShadow | 12 іменованих груп PASS ×2; по 27 PNG; D3D12, NVIDIA RTX 3050 Laptop GPU, driver 32.0.15.9282, software=false |
| Перехресні frame slots | Sun: open0/shadow1/shadow0/open1; Moon: open0/mask1-open1/mask2-shadow0/mask2-shadow1. Реальні slot IDs asserted; обидва позитивні джерела на обох slots |
| Repeat / попередній SkyDirect / Contact | 27/27 / 26/26 / 31/31 PNG byte-exact |
| Попередні shaders | 56/56 старих DXIL/reflection files byte-exact |
| Selected RenderFrameTests | Software і actual D3D12 PASS; це не full suite |
| Provenance | 7 EXE, 68 shader artifacts + manifest hash-anchored на build completion; 104 frozen source/root-lineage entries незмінні на момент final verifier; 7 protected старих artifacts незмінні |

Native матриця доводить source-specific ground/material causality, маски 1/2/3, протилежні напрями, рух caster і зміну напряму того самого сонця, reverse winding, nearest blocker перед typed receiver z=3 проти blocker за ним, тінь на tilted slope, нульовий caster compatibility та збереження ambient/contact/local/emission/UI за disabled direct. Є 10 pre-command invalid-payload cases й capacity boundary 1024 active + 3072 hidden. Final independent review: **APPROVE тільки bounded fixed-camera prerequisite**.

EXE SHA-256: `DC250FA0FA021EFE14381A7EA6C7D50BCF34E9A9F7B49396369F9992C9E27828`.
Shader manifest SHA-256: `14075DE70EE02BE9A65A41B9F7ADD2AE6BA13EBA3F227FD48C09B02C8D4173D1`.
Точні commands/exit provenance: `verification-command-01.json`; comparisons, source lineage та protected hashes — окремі JSON у capsule. Ledger зафіксував 54,198,638 logical bytes до додавання command record; final root залишається нижче 128 MiB. Це не GPU VRAM ledger.

## Збережені невдачі — не переписані на PASS

- Attempt01: ADL ambiguity у probe; attempt02: 1-ULP CPU threshold fixture; attempt03: shader-negative source-independence fixture. Виправлення fixture не послабило production horizon або shader semantics.
- Attempt03/04 native: heap corruption C0000374. Pinned SDL з debug=false не відхилив п'ять fragment uniform buffers до D3D12 fixed-array запису. Новий ABI об'єднує Moon32+Shadow160 у явний b3; renderer/shader preflight відхиляє count>4. Старі небезпечні EXE не запускати повторно.
- Attempt05: W4/WX відхилив локальне ім'я, що приховувало member. Attempt06: build пройшов, але review виявив same-slot, а не cross-slot repeat, і неповний shader/verifier hash anchor; це не native acceptance.
- Attempt07: native відхилив nearest-depth fixture без обов'язкового ground draw. Збережено 18 PNG; report counter19 включав перервану 19-ту спробу capture. Додано справжній ground control поза caster footprint, renderer validator не змінено.
- Attempt08: native/CPU/shaders/regression пройшли, але verifier actual exit1 через хибний expected PNG count31. Фактичний count27 звірено за всіма capture calls та файлами. Attempt09 повторив увесь прогін з count27 і exact 12-name inventory; жодних вигаданих додаткових кадрів.

Усі roots, logs і artifacts збережені. Файли не видаляли; не було install/cleanup, privileged profiler, commit/push/release або зміни launcher.

## Візуальна межа та наступна робота

Переглянуто unmodified native PNG з фінальної capsule: source-direction relocation і tilted-slope shadow; додатково в attempt08 — nearest low/high blocker. Переглянуто також `target-0262.png`. Diagnostic ромби/typed 1×1 fixtures **не подібні до concept art і не є художнім результатом**. PNG — GPU readback, не desktop scanout, керування ноутбуком, відео руху чи FPS evidence.

Ще OPEN: authored world-triangle asset adapter, всі реальні building/tree/resident/cargo категорії й silhouette quality, camera-following/snapped maps/cascades і pan/zoom stability, self-shadow/bias policy, partial/alpha coverage, low-horizon/time/weather, вода/reflections/FoW/атмосфера/FX, art kit, GameView, presets, full current suite та ≥120 FPS/smoothness.

Наступний engine-only крок уже почато окремою cohort: optional authored `sky_shadow_triangles` у diagnostic schema-2 exporter і typed GameArt world→camera adapter. Його нові зміни **ще не прийнято**; frozen 104-entry checkpoint вище не є тестом цих пізніших змін. Мінімальний diagnostic kit не є новим каталогом чи готовою копією макета. Повний engine-first gate лишається відкритим, гра чекає.
