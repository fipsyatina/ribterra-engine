# Contact GPU V1 — частковий крок двигуна

07.10.2026. **Contact-only перевірено; повний двигун НЕ ПРИЙНЯТО.** Усі E01–E17, відповідність концептам і ≥120 FPS у 1920×1080 залишаються відкритими. GameView лишається BakedCompat, Dynamic недоступний; launcher, HUD, генератор, механіки та каталог арту не змінювалися.

## Що реально реалізовано

Окремий `WorldGraph::GroundContactSlice`: GroundGBuffer → Contact → GroundLighting → WorldForward → Composite → UI. Порожній список пропускає Contact і зберігає п'ять проходів. Використовуються ті самі сім attachments і lighting384; новий target не додавався.

Contact змінює тільки ground_albedo.A: `newAO=oldAO*(1-mask*opacity)`. Target LOAD/STORE, A-only write mask, alpha blend ZERO/ONE_MINUS_SRC_ALPHA/ADD; активне albedo не семплується. World/Flags — nearest; окрема R8-маска — linear clamp, explicit LOD0. Сонячний/local внески та UI не затемнюються. Emission незалежний за shader contract, але нового окремого native emission fixture у цьому кроці немає.

GroundForward зберіг старі W0/1 і reserved-zero правила. Новий Contact graph використовує discrete W0 invalid / W1 ordinary / W2 eligible та typed Ordinary/Flat/Slope/Bridge; усі вісім local receiver bits незмінні. Contact вимагає W2/reveal1 й допустиму відстань до площини. Ordinary, partial reveal та поверхня іншої висоти перевірені парними негативними/позитивними controls. Це не автоматичне розпізнавання water/roof/cliff у GameArt: їхній майбутній adapter повинен задавати Ordinary.

Спільна canonical projection: origin + zoom × project_render_xyz(P). Ground vertices перевіряються з допуском 1/1024 logical pixel. Вісім extruded corners користуються тією самою float-функцією; фізичний layout, extrema, padding ±1px, floor/ceil і clipping обчислюються double. Plane tolerance [.02,.04] узгоджено з bounded RGBA16F error; CPU prototype з меншим minimum лишається історичним, не новим GPU контрактом. Bounds ±32, normal.z≥.5, cap128 без truncation — provisional renderer limits, не FPS або terrain gameplay правила.

Typed immutable registry володіє exact validated R8 bytes: dimensions3..1024, ≤1 MiB/art, ≤256 live arts і ≤16 MiB logical R8 bytes. Integer subrect≥3×3 має zero outer one-texel ring усередині rect та nonzero interior. UV задає first/last texel centres; GPU draw мусить точно збігатися з registry UV/facing. Publication лише після успішного upload; allocation/pre-submit upload/idle-wait faults не змінюють живий sentinel. Cold release чекає GPU idle, змінює generation; wrap назавжди retires slot. Render не reentrant, caller-serialized; всі inert handles перевіряються до command acquisition, resolved GPU pointers не виходять із render.

## Поточні докази

Основна capsule: `build/contact-targeted-2026-10-07-attempt03/`.

- Fresh direct MSVC C++20 `/O2 /MT /W4 /WX /permissive-`, три current renderer/contact/probe TU, тільки SDL3/SDL3_ttf imports. Не використовували старі Ribterra objects/libs, CMake або повне asset staging. Окремі ABI assertions контролюють GPU80/160/48 та CPU wrappers; C4324 локально вимкнено лише для двох intentionally padded CPU wrappers, не wire structs/глобальних flags.
- `cpu-tests.log`: **211 CPU assertions PASS**. Це прямий standalone test, не CTest.
- `shader-contract.log`: compiled DXIL interfaces, bindings, ABI і manifest PASS. `shader-negative-fixed-01.log`: **34 register/space/output/ABI mutations**, 10 direct +1 two-level include mutations та інші semantic/numerical negatives відхилено.
- Початковий `shader-negative.log` — **FAIL**, не перейменований у PASS: fixture31 лишав undefined identifier після rename. `negative31-diagnostic.log` зберігає причину. Виправлений, compilable negative script окремо збережено в `source/shader_negative-fixed-01.py`; це єдиний intentional delta від початкової script snapshot, не зміна production DXIL.
- `native-01` і `native-02`: **53/53 native checks у кожному**, actual D3D12, RTX3050Laptop, driver32.0.15.9282; GPU execution time=null. По35 output files; **31/31 PNG між повторними запусками byte-exact** (`native-repeat-comparison.json`).
- Ambient sample168/156/135 →125/116/99; 128 repeated-value1 masks лишають168/156/135, 128 distinct1..128 masks дають24/22/17. Обидва matched frames мають128 actual draws/binds/casters. Це pixel discriminator, не лише counters.
- Paired Ordinary/Flat, reveal.5/1, lower/upper plane, slope/bridge, shifted anchor та discriminator off-centre pivot PASS. Atlas outside255/outside0 у двох slots дає byte-exact pixels із реальним interior darkening. Sun-only/local-only/UI, both frame slots, minimize/no-submit, letterbox/resize A-B-A, stale/mixed handles, cap129 і registry256/16MiB boundary PASS.
- `renderer-regression-01/report.json`: fresh direct RenderFrameTests `/W4 /WX`; software і `--gpu` exit0. Це supplementary selected suite, **не новий full CTest/full game run**. Всі49 staged shader inputs незмінні.
- `prior-input-hashes-after-repair.json`: старі Local/Ground EXE і shader manifest **3/3 hash-identical**. `reuse-native-01.json`: expected exit1, всі35 existing files незмінні. Нові helper-и fail-if-exists; pre/post compiler headroom≥384MiB і root logical budget≤128MiB. Логічні bytes з hardlinks не дорівнюють allocated disk bytes або VRAM.
- Independent source/oracle/evidence review: **APPROVE лише partial Contact-only milestone**. Невдалі compiler attempts01/02, перша negative run та проміжні sources збережені. Наявних файлів не видаляли/не restage-или.

Native probe SHA256: `7E2D8609F0298F94CE52A71E351C16D07D9F766954A1EBE5D3874A1BE5DD9357`.

## Що цим не доведено

Немає нового raw W readback, desktop scan-out/video, motion acceptance, artist/concept-ready house/tree/resident/cargo kit, GameArt loader/adapter integration чи representative performance. Це новий shader/registry route із діагностичними масками, не фінальний вигляд гри. Повний CTest, shipping GameView, full graphics presets і 120 FPS тут не приймаються. Sun/moon та directional silhouettes, reflections/coverage carrier, atmosphere/fog/water/weather та інші engine gates лишаються обов'язковими.

Наступна engine робота: independent sun/moon та source-specific sky visibility, authored small calibration kit, потім невиконані ефекти й full GameView/presets/performance gate. До ігрової переробки V01–V03 не переходити, доки повний engine checklist не прийнято.
