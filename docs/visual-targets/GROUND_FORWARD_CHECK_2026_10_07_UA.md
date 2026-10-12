# Production GroundForwardSlice — перевірка 07.10.2026

Статус: **обмежений інженерний крок реалізовано й перевірено; повний двигун НЕ ПРИЙНЯТО**. У production Renderer2D додано opt-in `GroundGBuffer → GroundLighting → WorldForward → Composite → UI`. Фінальні 16/16 selected CTest, native D3D12 матриця й незалежний review пройшли. **Усі E01–E17 відкриті; Dynamic GameView, відповідність концептам і ≥120 FPS у 1920×1080 не доведені.** Генератор, механіки, масовий art catalog і подальший HUD чекають повного engine-first порога.

## Що реально змінилося

- [Renderer2D](../../src/render/renderer2d.cpp) отримав окремий internal `WorldGraph::GroundForwardSlice`, strict routing і сім attachments для кожного з двох frame sets. Це не user-facing preset і не ввімкнення `LightingMode::Dynamic`. Звичайний GameView/launcher лишено BakedCompat.
- [Frame contract](../../include/ribterra/render_frame.hpp): ground material index — one-based, explicit typed sRGB opaque albedo, index-aligned geometry XYZ/N і white tint. Ground приймає лише opaque Terrain; WorldForward — лише schema-2 material draws у painter order. Mixed/legacy/Atmosphere/Fog compatibility roles, відсутня геометрія, cutout ground, NaN та недопустимі metadata відхиляються. Суміжне batching враховує ground material index; A/B/A не сортується.
- `GroundMaterialGpu` — 48 bytes; `ForwardMetadataGpu` — 16 bytes. GPU `SpriteSurfaceGpu` 160 bytes і lighting 384/80-byte ABI не змінено. Reveal — окреме CPU-поле draw та окремий uniform, не переозначення reserved SpriteSurface components.
- Нові [GBuffer](../../shaders/ground_gbuffer.frag.hlsl), [GroundLighting](../../shaders/ground_lighting.frag.hlsl) і [WorldForward](../../shaders/material_forward.frag.hlsl) programs. Forward wrapper перевикористовує незмінений production material shader, не його діагностичну копію. GroundLighting підтримує bounded ambient/sun та до чотирьох unoccluded point/spot lights; native controls нижче перевіряють один local source. Тіні/occlusion цим не реалізовано.
- Чотири exact format/usage queries (`COLOR_TARGET | SAMPLER`), окремі MRT/ABI/binding/blend contracts, recursive include SHA256. CMake відстежує весь bounded shader source tree, включно з новими nested leaf files. Angle/macro include forms відхиляються fail-closed; canonical quoted relative includes мають повний recursive ledger.

| Attachment | Format | Зміст |
|---|---|---|
| ground_albedo | RGBA8_UNORM_SRGB | Albedo, A = AO/contact visibility |
| surface_world | RGBA16F | Camera-relative XYZ, W = validity |
| surface_material | RGBA8_UNORM | Oct normal RG, roughness B, sun visibility A |
| surface_flags | RGBA8_UNORM | Receiver byte, wetness, shore/snow, reveal |
| scene_linear | RGBA16F | Linear ground + straight-alpha forward objects |
| world_meta | RG16F | Coverage-weighted depth/reveal |
| final | RGBA8_UNORM | SDR Composite + окремий незмінний UI |

GBuffer source albedo hardware-decodes sRGB, sRGB MRT hardware-encodes його для storage, Lighting sample знову hardware-decodes storage. Ручного подвійного decode немає. Pass закривається до семплювання його targets. GroundLighting fullscreen UV прив'язані до **всього physical drawable**, зі scissor world rectangle: letterbox не розтягує GBuffer. UI не проходить через exposure чи world lighting.

## Фінальний native доказ

[GroundForwardProbe](../../tools/ground_forward_probe.cpp), [фінальний JSON](../../artifacts/ground-forward-2026-10-07/native-final-05/ground-forward-report.json), [actual exit0 log](../../artifacts/ground-forward-2026-10-07/check-final-04/native-final.log). Windows / D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver 32.0.15.9282; actual initial drawable **1280×720**, build label 0.26.3. Це не 1080p performance прогін.

Використано [свіжий kit export](../../artifacts/ground-forward-2026-10-07/master-export-a/manifest.json) із [редагованого master](../../art-source/engine-calibration/material-master.json), без змін його геометрії чи production material shader. Cabin і slope — прості діагностичні поверхні, **не concept-ready art kit**.

### Сонце, вікно, UI та два frame slots

Послідовність `A0 → B1 → B0 → A1`, slots `0/1/0/1`. A: sun +Z, emission off. B: sun уздовж master slope normal, emission on. A0/A1 та B1/B0 PNG побайтно однакові **між різними slots**, а повторне використання кожного slot змінює світловий стан. Усі чотири submissions мають п'ять passes у потрібному порядку, один batched ground draw і один material draw.

Схил лише 7.956° від +Z; його N = `[-0.06189845,-0.12379691,0.99037516]`. Для основного normal oracle ground dry/roughness=0: sharp specular contribution у цих напрямах не перекриває diffuse contrast. Albedo однакове. Flat/slope N-dot `1 / 0.990375` у A та навпаки у B. RGB slope A `[149,173,156]`, B `[150,174,157]`; flat A `[150,174,157]`, B `[149,173,156]`. Assertions вимагають і reversal, і позитивної causal зміни **кожного** receiver. Це контроль нормалей, не приймання wetness/specular.

Окрема пара за незмінного sun: window `[8,10,11] → [221,138,70]`, delta RGB `[213,128,59]`; nonemissive facade `[8,10,11]` не змінюється. Emission світиться, але **вікно ще не є пов'язаним джерелом для сусідніх поверхонь**. UI reference `[37,211,149,255]` точний у всіх контрольних станах.

### Світові координати та point/spot

Однакові geometry/albedo, sun intensity 0, ambient .025. Point light переноситься між центроїдами **source XYZ**, на Z+1, radius 2.5, color/intensity `[1,.6,.2,5]`. Spot лишається над схилом; змінюється тільки direction down/up, cone cosines .6/.9.

| Контроль | RGB схилу | RGB рівної землі |
|---|---|---|
| Point над схилом | 127,117,62 | 5,7,5 |
| Point над рівною землею | 5,7,5 | 128,118,62 |
| Spot спрямований на схил | 127,117,62 | 5,7,5 |
| Spot спрямований від схилу | 5,7,5 | 5,7,5 |

Вагова RGB brightness зміна: slope +108.72, flat +109.65, spot +108.72; поріг кожного causal delta — 16. Point assertions перевіряють і within-frame reversal, і direct cross-capture deltas. Ground поза spot radius та UI лишаються точними. Це реальний production GroundLighting response на P, **не occlusion/shadow чи вся система джерел**.

### Precision і metadata blending

Окремий native readback production GroundGBuffer shader перевірив чотири constant cases із ±31.99/±32 та affine interior pixel. Tight little-endian [RGBA16F raw buffer](../../artifacts/ground-forward-2026-10-07/native-final-05/ground-position-rgba16f.bin) — 1024 bytes. Max component error **0.00562477** за порога .0078125; max Euclidean **0.00810665** за порога .02. Тому цей graph fail-closed обмежує кожну camera-relative P component та reconstructed forward corners до **±32**. Це не універсальна точність будь-якого zoom/visible range; Legacy bounds не звужено. П'ять interior controls не доводять точність усіх поверхонь.

Окремий D3D12 output-merger test того самого material-forward shader перевірив дві alpha coverages 64/255 і 153/255 на RG16F metadata: expected depth 38.3069496, actual 38.3125, error .00555038 < .03125; expected reveal .200313732, actual .200317383 < .002 error. Scene alpha straight, metadata coverage-premultiplied з `ONE / ONE_MINUS_SRC_ALPHA`. Native proof підтверджує source alpha blend навіть у двокомпонентному target; це не повний Composite/FoW proof. Контекст: [D3D12 per-target blend fields](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_render_target_blend_desc).

Обидва independent controls мають ті самі backend/device/driver, що production probe; assertion не дозволяє непомітно перевірити інший adapter.

### Resize, lifetime і presentation

Два sets, по сім targets. Allocate RAII candidate → complete old slot fences → atomic swap. Повторний resize `1120×630 → 800×600 → 1280×720`: actual drawable/status/bytes збігаються, generation росте `1→2→3→4`, IDs `1/2→3/4→5/6→7/8`, candidate_live=0. Strict 4:3 control не може бути пропущено: обидві ground samples збігаються, UI точний, upper/lower bands — `[6,9,14,255]`, обчислені незалежним sRGB decode → linear exposure .72 → encode oracle.

Faults на creation attempts **15/21/28** — перший, середній і останній target candidate після 14 active creations. Вимагаються точні injected-error prefix і номер attempt. Обидва active IDs/dimensions/generation збережені, candidate_live=0; **обидва old-slot PNG byte-exact після rollback**. Наступний resize installs fresh sets; їхні кадри теж byte-equal. Це перевірка owned GPU textures, не доказ відсутності всіх driver allocations/leaks.

Immediate/Mailbox/VSync усі advertised/applied і реально відрендерили п'яти-pass кадр з exact reference pixels. Це не вимірювання display intervals, tearing чи плавності.

Base attachment ledger: 36 bytes/full-pixel/set; **66,355,200 bytes** для двох sets у цьому 1280×720 control, **149,299,200 bytes** розрахунково у 1920×1080. Resize old+candidate подвоює base ledger. Це не виміряна VRAM: atlases, buffers, legacy resources і майбутній Reflection сюди не входять. GPU execution unavailable; capture/readback overhead не використано як FPS або GPU time.

## Збірка, тести та регресії

[Release build exit0](../../artifacts/ground-forward-2026-10-07/build-full-12.log); C4324 alignment warnings присутні, warning-free результат не заявляється. [Snapshot](../../artifacts/ground-forward-2026-10-07/check-final-04/snapshot.json) має 355 hashed runtime files; [додатковий diagnostic copy](../../artifacts/ground-forward-2026-10-07/check-final-04/diagnostic-copy.json) — три файли.

Окремо [target Ribterra також зібрано, exit0](../../artifacts/ground-forward-2026-10-07/build-game-13.log). Це локальна source build, не встановлений preview/release і не ввімкнення нового graph у GameView. `Play Ribterra Rework.cmd` як і раніше вказує на збережений `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2`; його не перемикали на probe або проміжний двигун.

[Фінальні 16/16 CTest, 11.99 s](../../artifacts/ground-forward-2026-10-07/check-final-04/ctest-final.log): shader_contract, shader_negative, material_calibration_shader_contract, material_master_export, ground_forward_contract, game_art_outline, presentation_ui, render_frame, frame_timing, world_projection, render_batch, minimap_cache, presentation_contract, surface_lighting_contract, hdr_color_contract, material_bridge_contract. Це selected suite, не повний simulation/network suite. Software contracts не підміняють native докази.

[LastTest.log](../../artifacts/ground-forward-2026-10-07/check-final-04/runtime/Testing/Temporary/LastTest.log) зберігає negative результат: **20** wrong register/space/output/ABI mutations rejected, **3** direct + **1** two-level include mutations detected, **2** angle/macro include forms rejected. Manifest містить 18 stages / 9 programs та exact current source/dependency hashes. Застарілий ранній shader manifest не використано як final proof.

Окремі актуальні GPU-regressions пройшли з runtime `check-final-02`: [RenderFrame](../../artifacts/ground-forward-2026-10-07/check-final-02/render-frame-gpu.log), [batching](../../artifacts/ground-forward-2026-10-07/check-final-02/render-batch-gpu.log), [surface lights](../../artifacts/ground-forward-2026-10-07/check-final-02/surface-native.log), [HDR](../../artifacts/ground-forward-2026-10-07/check-final-02/hdr-native.log), [material bridge](../../artifacts/ground-forward-2026-10-07/check-final-02/material-native.log). [Hash verifier](../../artifacts/ground-forward-2026-10-07/check-final-04/after-run-hashes.json) підтверджує, що всі шість цих compatibility executables, включно з ReworkVisualProbe, побайтно однакові з final runtime; змінився тільки новий GroundForwardProbe.

[Свіжий GameView quick matrix](../../artifacts/ground-forward-2026-10-07/check-final-02/gameview-native.log): 6 cases, 15 captures. [Повний PNG ledger](../../artifacts/ground-forward-2026-10-07/legacy-comparison.json) — **15/15 byte-identical** проти retained material-bridge legacy-final-v3, включно з F1 hide/restore. Це regression старого BakedCompat шляху, не нове приймання HUD/V01–V03.

[After-run verifier](../../tools/verify_ground_forward_snapshot.ps1) підтвердив **355 runtime + 26 external command + 3 diagnostic files** без hash changes. Output isolation не є hermetic source snapshot: transitive repository inputs не заморожено; live test inputs на фінальному прогоні не змінювали.

[Reuse verifier](../../tools/verify_ground_forward_reuse.ps1) повторив probe в зайнятий output-root: [очікуваний exit1](../../artifacts/ground-forward-2026-10-07/check-final-04/reuse-final/refusal.log), [всі 37 файлів незмінні before/after](../../artifacts/ground-forward-2026-10-07/check-final-04/reuse-final/reuse-report.json). Фінальний набір має **34 PNG + 2 JSON + 1 raw buffer**. Попередні failures/snapshots/logs/fixtures збережено, нічого не видалено.

## Перегляд і межа художнього результату

Переглянуто немонтовані final-target кадри [point над схилом](../../artifacts/ground-forward-2026-10-07/native-final-05/point-above-slope.png), [point над flat](../../artifacts/ground-forward-2026-10-07/native-final-05/point-above-flat.png), [spot](../../artifacts/ground-forward-2026-10-07/native-final-05/spot-aimed-at-slope.png), [emission](../../artifacts/ground-forward-2026-10-07/native-final-05/emission-on-same-sun.png), [4:3](../../artifacts/ground-forward-2026-10-07/native-final-05/resize-800x600.png). Assertion-only final hardening зберіг [34/34 PNG byte equality](../../artifacts/ground-forward-2026-10-07/native-04-05-comparison.json) із попереднім переглянутим набором.

Окремо переглянуто [actual GameView](../../artifacts/ground-forward-2026-10-07/legacy-native/uk-85-1280x720-staged-selected-stockpile.png) і [target-0262](images/target-0262.png): основи UI, матеріали землі/води, щільність і глибина сцени, масштаб та причинне світло ще не відтворюють концепт повністю. Тестові ромби та cabin-блок не підміняють потрібні дерева, мешканців, будівлі, воду й повний art kit. Native readback PNG не є monitor scan-out або відео. Native desktop-control API у цій сесії недоступний; дозволені користувачем видимі SDL/D3D12 probe windows використовувалися, але керування робочим столом не заявляється.

Незалежний read-only source/evidence review: **APPROVE лише цього bounded slice, 0 відкритих findings**. Перераховано hashes/кількість файлів, causal deltas, raw precision, alpha oracle, resize IDs/rollback, presentation modes та Legacy equivalence. Це не художнє приймання, не все E02 і не завершення загальної цілі.

## Ідентифікація та наступна межа

| Input | SHA256 |
|---|---|
| GroundForwardProbe.exe | c91b5622694409e8cd2fd650102adf114ee328a6c9365d05e2243f270c5b41b9 |
| Production shader manifest | 904ab229eb79c49b78fbf10d12bfc9ef30dfefcd82ca57b955f3c857f0afbf4e |
| Unchanged material shader | 853120dad680adae861d3f0354c1efd4e8056778771cd28acdf0c10253ea1f09 |
| Editable master | cca93e76374baeeb1189cb2e1ff351b17b43c1566b20d53e3c7606b7ddd8e531 |
| Kit manifest / content ledger | 8b800ef5822d09c7eb2660ad89994b580d7dd99dbcc2cff07b56a7a6376e1f4e / 4be653ee31809ae1bcd73019e6a4c05c529f367d57429e4084526d4ea34eee33 |

Далі лишається повний graph із Reflection/FoW, causal source linkage та blockers/тіні, time/sun/moon/weather, material/water/atmosphere FX, concept-faithful minimum kit, Dynamic GameView, actual quality presets й підтвердження ≥120 FPS/плавності у 1080p. Receiver/shore/reveal metadata тут збережено, але downstream effect/secrecy ще не прийняті. Не продовжувати gameplay чи перемикати launcher до [повного E01–E17 приймання](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).

Ponytail/ultrawork розділили renderer, shaders, native proof та незалежний review; karpathy-guidelines/cpp-coding-standards вплинули на additive opt-in scope, typed ABI, strict validation і RAII/fence lifetime. Вимоги користувача не скорочено. Commit/push/install/release та новий elevated profiler запуск не виконувалися.
