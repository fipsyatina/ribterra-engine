# Геометрія → матеріальні карти → прямий GPU-контроль, 06.10.2026

Статус: **частковий інженерний крок двигуна, не приймання графіки чи реліз**. Детермінований exporter із редагованої трикутної геометрії та native D3D12-контроль пройшли. 15/15 вибраних CTest, 72 прямі P/N-контролі й 180 перевірок production-кольору. **Усі E01–E17 відкриті; GameView лишається BakedCompat, відповідність концептам і ≥120 FPS не доведені.**

## Що додано

- [Редагований master](../../art-source/engine-calibration/material-master.json): діагностичний об'єм із двома фасадами, двосхилим дахом, похилою поверхнею та маскою вікна. Це проста контрольна геометрія, **не будинок із концепту та не готовий нейтральний art kit**. Нормалі беруться з winding трикутників, XYZ — із barycentric interpolation; вони не вгадуються зі старих PNG.
- [Offline exporter](../../tools/export_material_master.py), Python standard library: фіксована ортографічна basis 58×29, один sample на центр texel, frontmost camera-depth, integer-snapped bounds/pivot. Шість узгоджених `.rtex` каналів і schema-2 off/on descriptors. Albedo у двох emission-станах побайтно однакове. Master, exporter, камера та settings мають окремі SHA256.
- Межі authored XYZ та реконструйованих восьми source/depth corners — ±8192 при anchor=0, geometry scale=1. Export scale 0.001…64 і depth endpoints канонізуються до float32 до raster/encoding/JSON; endpoints охоплюють геометрію й не можуть згорнутися в один float. Source extent ≤2048; довільні runtime anchors/scales і широкі precision ranges тут не приймаються.
- [Native probe](../../tools/material_calibration_probe.cpp) з окремими `RGBA16F` position/normal targets. [Diagnostic shader](../../shaders/material_calibration.frag.hlsl) включає незмінені production material functions; production shader і program manifest не підміняються. CTest перевіряє два MRT outputs, `SpriteSurface` 160-byte ABI, b0/b1 та registers/spaces. Reflection декларує п'ять sampler slots; оптимізований diagnostic DXIL реально використовує перші три.
- GPU-ресурси probe мають окремого власника, закриті passes, fence waits і RAII retirement. Невдале submit не намагається повторно скасувати вже consumed command buffer. Output-root не може існувати заздалегідь; попередні результати не замінюються й не видаляються.

Exporter не виконує semantic mip generation, atlas packing, multisample AA, model animation чи повну art-production валідацію. Caster mask експортується й приймається GameArt, але shader тіней у цьому кроці її не використовує.

## Прямий числовий доказ геометрії

[Фінальний native JSON](../../artifacts/material-master-2026-10-06/native-final/material-calibration-report.json), [actual exit0 log](../../artifacts/material-master-2026-10-06/check-final-v2/native-final.log). Windows / D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU, driver 32.0.15.9282, **фактичний drawable 1280×720**, build label 0.26.3. Це не 1080p performance measurement.

12 безпечних внутрішніх контролів × zoom 0.62/1.30/3.00 × origin/panned-elevated = **72 samples**. Panned case: camera XYZ [10.25, −6.125, 0], anchor [10.375, −6.5, legacy-elevation-4 converted Z], geometry scale 1.15. У JSON записано actual physical pixel center, camera-relative anchor, expected/GPU camera-relative XYZ та нормаль. CPU oracle перетинає camera ray із реальною площиною master, незалежно перевіряє winding, canonical C++ projection та barycentric coverage. Колір освітлення **не використовується як доказ правильності P**.

| Величина | Максимум у цих контролях | Поріг |
|---|---:|---:|
| XYZ, max absolute component error | 0.003792286 тайла | 0.02 |
| Кут GPU normal проти geometry normal | 0.3189811° | 1.5° |
| Похибка довжини GPU normal | 0.000480533 | 0.001 |
| Reprojection у native drawable | 0.312011719 px | 1 px |

Збережено **12 tight little-endian RGBA16F raw buffers**. Саме вони є числовим readback. Шість decoded-normal PNG — лише візуалізація нормалей; вони не є raw float proof. Перевірка обмежена 72 внутрішніми samples, не всім силуетом, усіма DPI/масштабами чи довільним material descriptor.

## Production освітлення і перегляд

Той самий geometry kit реально завантажено через GameArt та намальовано production Renderer2D: **15 немонтованих final-target PNG / 180 samples**. Три zoom × sun / point +X / point −X / emission off / emission on. RGB/alpha поріг — 4 byte values. Sun нормалізовано; point positions/colors, ambient, exposure й emission state записані в JSON. Specular strength явно 0: мокрі матеріали/відблиски цим oracle не перевірялися.

Переглянуто фінальні кадри: [sun](../../artifacts/material-master-2026-10-06/native-final/production-sun-300.png), [лампа +X](../../artifacts/material-master-2026-10-06/native-final/production-point-x-positive-300.png), [лампа −X](../../artifacts/material-master-2026-10-06/native-final/production-point-x-negative-300.png), [emission on](../../artifacts/material-master-2026-10-06/native-final/production-emission-on-300.png), [decoded normal preview](../../artifacts/material-master-2026-10-06/native-final/origin-300-decoded-normal-preview.png). Видно зміну освітленої сторони без зміни albedo і окреме світіння вікна. **Вікно ще не є джерелом, що освітлює сусідні об'єкти; shadows/occlusion не доведені.**

Ці прості контрольні поверхні навмисно не повторюють деталізацію, матеріали й палітру [target-0262](images/target-0262.png). Вони не можуть зараховуватися як V03 art acceptance. Native readback PNG — не screenshot моніторного scan-out, не відео руху й не доказ керування робочим столом; native desktop-control API у цій сесії недоступний.

## Повторюваність, тести й збереження

[Release build, exit0](../../artifacts/material-master-2026-10-06/build-final.log). Є alignment warning C4324; warning-free чи full suite результат не заявляється.

[Snapshot](../../artifacts/material-master-2026-10-06/check-final-v2/snapshot.json): 330 runtime files. Діагностичні shader/JSON/contract додатково скопійовано у `runtime/calibration-shaders`; [3 matching SHA256](../../artifacts/material-master-2026-10-06/check-final-v2/diagnostic-copy.log). [Після прогонів](../../artifacts/material-master-2026-10-06/check-final-v2/after-run-hashes.log) усі 330 файлів і 26 external command files лишилися hash-identical. Output isolation **не є hermetic source snapshot**: Python tests, exporter/master і transitive inputs лишаються live; на фінальному прогоні їх не змінювали.

[CTest 15/15, 14.59 с](../../artifacts/material-master-2026-10-06/check-final-v2/ctest-final-v2.log), [LastTest.log](../../artifacts/material-master-2026-10-06/check-final-v2/runtime/Testing/Temporary/LastTest.log): `shader_contract`, `shader_negative`, `material_calibration_shader_contract`, `material_master_export`, `game_art_outline`, `presentation_ui`, `render_frame`, `frame_timing`, `world_projection`, `render_batch`, `minimap_cache`, `presentation_contract`, `surface_lighting_contract`, `hdr_color_contract`, `material_bridge_contract`. Software контрактні tests не є native lighting proof.

[Фінальний exporter report](../../artifacts/material-master-2026-10-06/export-tests-n/test-report.json): два експорти по 10 файлів побайтно однакові; **14 exact negative cases** повертають exit2 із потрібною причиною, не залишають partial output і зберігають existing-output sentinel. Окремий regression показує raw-double ePPP snapped extent 832×644 проти runtime-float32 831×644; exporter використовує другий. Зареєстрований CTest повторив exporter перевірки у власній новій retained directory.

Незалежний read-only source/evidence review — **APPROVE для цього bounded slice**: повторно обчислено snapshot/kit/shader hashes, кількість і розміри файлів, усі геометричні maxima та production-color deltas (максимум 1 за порога 4). [CTest-generated exporter report](../../artifacts/material-master-2026-10-06/check-final-v2/runtime/material-master-test-artifacts/material-master-8mm6icy0/test-report.json) теж має PASS / 10 deterministic files / 14 negative cases. Це не художнє приймання чи повний suite.

Повторний native запуск у вже зайнятий output-root повернув [очікуваний exit1](../../artifacts/material-master-2026-10-06/check-final-v2/reuse-final/refusal.log); [повний before/after SHA256 ledger усіх 34 незмінених файлів](../../artifacts/material-master-2026-10-06/check-final-v2/reuse-final/reuse-report.json) збережено окремим [reuse check](../../tools/verify_material_calibration_reuse.ps1). Початковий one-line hash summary теж збережено, але сам по собі він не є цим повним ledger. Проміжні failed/preliminary snapshots, logs і fixtures збережено. Початковий `ctest` не знайшовся у PATH; `ctest-final.log` не є green proof — наведений `ctest-final-v2.log` створений фактичним запуском абсолютного executable.

## Ідентифікація

| Input | SHA256 |
|---|---|
| MaterialCalibrationProbe.exe | `734adfd9f950633e1ac50bf3ce3248be86580bd2fe598ca8cebc9ee7c30d384e` |
| Editable master | `cca93e76374baeeb1189cb2e1ff351b17b43c1566b20d53e3c7606b7ddd8e531` |
| Exporter | `e3b8e5898b4ffea08886dc6b1f659aa9ebab5c31759a46807e7b20cadb766911` |
| Exported kit manifest file | `8b800ef5822d09c7eb2660ad89994b580d7dd99dbcc2cff07b56a7a6376e1f4e` |
| Exported kit content ledger | `4be653ee31809ae1bcd73019e6a4c05c529f367d57429e4084526d4ea34eee33` |
| Diagnostic DXIL | `ef122e74c51e05c67e05d0e346c7c0b8a8acd5acc6bc6c695f6f4873b0b36a22` |
| Production material HLSL | `853120dad680adae861d3f0354c1efd4e8056778771cd28acdf0c10253ea1f09` |
| Production shader manifest | `d55f4e1327682af5a2a5d535ddee44f9aed833d4daf00e483fc4535d84fa7ce8` |
| Ribterra.exe | `9569dd1db478b99ee03fadece0569a68ae70b21a8f36b5830dbdac22ed565d4b` |

Production executable/material shader/manifest не змінені від [material-bridge v3](MATERIAL_BRIDGE_CHECK_2026_10_06_UA.md); його 15-image legacy regression не видається за новий запуск у цьому кроці. Launcher не перемкнуто. Commit/push/install/release та новий elevated profiler запуск не виконувалися. Жоден файл не видалено.

## Що лишається до гри

Наступна залежність — справжній production GroundGBuffer → GroundLighting → WorldForward із реальними похилими receiver, довговічними surface metadata, двома frame sets та збереженням окремого Composite/UI. Цей probe MRT **не є реалізацією цього graph**. Reflection, FoW secrecy, blockers/тіні, sun/moon/time/weather, вода/атмосфера, мокрі матеріали, мінімальний concept-faithful art kit, Dynamic GameView, temporal captures і 1080p ≥120 FPS лишаються відкритими.

Збережено [конкретний наступний implementation plan](../../.omx/plans/2026-10-06-ground-forward-slice-ua.md) з ownership, pass/attachment contract, lifetime, memory ledger й native A/B/A/B доказом. Це план, не готовий graph.

Ponytail/ultrawork допомогли розділити exporter, native proof і незалежний review; karpathy-guidelines/cpp-coding-standards — тримати additive scope, typed boundaries та ресурсний lifetime. Вимоги користувача не скорочувалися. Повний поріг: [E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md); генератор, нові механіки й подальший HUD до його приймання не продовжуються.
