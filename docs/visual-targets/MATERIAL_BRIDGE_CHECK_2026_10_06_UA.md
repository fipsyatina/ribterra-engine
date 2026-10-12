# Матеріальний міст — частковий крок двигуна, 06.10.2026

Статус: **локальний інженерний контроль, не приймання графіки**. Додано typed textures, окремий освітлений спрайтовий шлях і schema-2 завантаження матеріалів у production Renderer2D/GameArt. 13/13 вибраних CTest та native матеріальний прогін пройшли. **E01–E17 відкриті; Dynamic сцена, відповідність концептам і ≥120 FPS не доведені.** Звичайний GameView ще використовує BakedCompat; launcher не змінено.

## Що реалізовано

- Шість обов'язкових каналів: albedo `RGBA8_UNORM_SRGB`, normal/material `RGBA8_UNORM`, camera-depth `R16_UNORM`, emission `R8_UNORM`, surface mask `RGBA8_UNORM`, caster mask `R8_UNORM`. Формат/usage перевіряється на конкретному GPU; software відмовляється від цього шляху явно.
- Albedo декодується самим sRGB sampler до bilinear filtering; повторного shader decode немає. Data maps залишаються лінійними. `.rtex` переносить щільні raw bytes, R16 little-endian, без 8-бітного PNG-декодера.
- Oct-normal, AO, реконструйована позиція, ambient, sun, до чотирьох point/spot джерел та окрема emission надходять у RGBA16F світу. Composite і byte-authored UI лишаються окремими. Wetness/roughness/specular підготовлені в коді, але повний native wet-material контроль ще потрібний.
- Per-channel texel-center clamp із фактичними dimensions ізолює atlas rectangles. Валідні UV прив'язуються до цілих texels; допустимий float roundoff канонізується. Незмінний material descriptor містить UV/emission; caller-side зміна їхніх копій не змінює матеріал.
- Typed текстури не можуть потрапити в legacy draw чи використовувати legacy-decoded текстуру як data channel. Handles мають generation; stale texture/material відхиляються. Матеріал не встановлюється без доступного HDR шляху.
- Schema 2 перевіряє family/camera/revision, hash/bytes, канали/формати, узгоджені rectangles, source size/pivot/export scale та depth range перед GPU-завантаженням. Увесь candidate комітиться тільки після успіху. Schema 1 залишається BakedCompat; `ready()/contains()` — тільки legacy routing, `contains_lit()/lit_sprite()` — явний новий шлях.
- GPU ABI: `SurfaceLighting` 384 bytes / fragment b0 space3 та `SpriteSurface` 160 bytes / b1 space3; п'ять sampled channels. **Caster mask поки валідований і збережений, але не споживається шейдером тіней.** Повний program/render-graph schema із Core spec ще не завершено.

## Координати й обмеження

Спільний adapter — `include/ribterra/world_projection.hpp`: reference tile 58×29, half extents 29/14.5, `renderZ = legacyElevation × 2.4 / 29`. Camera-depth — dot(XYZ, normalize(1,1,1)); inverse affine basis однаковий за змістом у CPU й матеріальному шейдері. Source pixel відносно pivot переводиться через export scale, потім geometry scale й camera-relative anchor. Це не screen-space пляма освітлення.

CPU `world_projection` перевірив roof/façade/feet/terrain points, R16 range −96…96, zoom 0.62/1.30/3.00, source-pixel rounding, ≤1 px / ≤0.02 tile у цих контролях та representable offsets великих координат. **Це не GPU reprojection proof справжнього house/resident master.** Ширші depth ranges/scales потребують окремого precision audit; не поширювати CPU tolerances на будь-який descriptor.

Legacy compatibility adapter зберігає історичний порядок float-обчислень та множення zoom у тій самій affine basis. Reassociation у проміжному v2 змінювала координати менш ніж на 0.0005 px **у проаналізованій Legacy matrix/fixture coordinates**, не у всьому довільному finite діапазоні, та рідкісні RGB-біти. У v3 додано bit-exact CPU controls, зокрема для cached-origin translation; native matrix нижче підтверджує відновлення попередніх 15 PNG. Новий 3D/material adapter при цьому не підміняється legacy-проєкцією.

Після незалежного review розширено **тільки CPU test**: asymmetric camera pan і точна послідовність viewport-base → cached-origin subtraction → vertex append. Додаткові [збірка](../../artifacts/material-bridge-2026-10-06/build-a/projection-hardening-build.log) і [прямий прогін](../../artifacts/material-bridge-2026-10-06/build-a/projection-hardening-check.log) — exit0. Це окрема перевірка, не новий 13-test snapshot; production exe/shaders і наведені native captures не змінювалися.

Спрайтові source dimensions ≤4096, export scale 0.001…64, geometry scale (0…64], anchor/depth/reconstructed corners у межах ±8192. Перевіряються вісім affine corners. Radiance inputs ≤64, specular strength ≤0.99: верхня арифметична межа з п'ятьма джерелами, wet multiplier, ambient/emission — 65190.4, нижче finite RGBA16F 65504. Старий surface-kernel coordinate bound не звужено цим кроком.

Завантаження schema 2 **startup-only**, навіть перехід із нього на schema 1 після submit відхиляється. Hot reload/fence-safe retirement — відкрита вимога, не удаваний успіх. Усі нові карти поки base-level-only; semantic mip generation/coverage policy не реалізовані.

## Актуальні докази

[Snapshot 329 hash-verified runtime files](../../artifacts/material-bridge-2026-10-06/check-final-v3/snapshot.json), [збірка](../../artifacts/material-bridge-2026-10-06/build-a/build-final-v3.log). Output-isolated, **не hermetic**: Python/source/toolchain inputs лишаються live, після фінальної збірки до прогону не змінювалися. Ninja Release / MSVC; warnings alignment та раніше наявні view/UI warnings не оголошуються виправленими.

13/13 CTest, 12.85 с: `shader_contract`, `shader_negative`, `game_art_outline`, `presentation_ui`, `render_frame`, `frame_timing`, `world_projection`, `render_batch`, `minimap_cache`, `presentation_contract`, `surface_lighting_contract`, `hdr_color_contract`, `material_bridge_contract`. [Aggregate CTest log](../../artifacts/material-bridge-2026-10-06/check-final-v3/ctest-final.log), [LastTest.log](../../artifacts/material-bridge-2026-10-06/check-final-v3/runtime/Testing/Temporary/LastTest.log). Це не повний suite; material/HDR software tests доводять validation/rejection, не native shader output. 12 shader register/space/output/ABI mutations відхилено після валідного baseline.

Native MaterialBridgeProbe: **actual exit0, 22 немонтовані final-target PNG**, Windows/D3D12, RTX 3050 Laptop, driver 32.0.15.9282, drawable 640×480, build 0.26.3. [JSON зі samples/counts/negative cases](../../artifacts/material-bridge-2026-10-06/native-final-v3/material-bridge-report.json), [log](../../artifacts/material-bridge-2026-10-06/check-final-v3/native-final.log). Числові light/exposure inputs і validation bounds тут не всі серіалізовані; source constants прив'язані до hash фінального executable, JSON не є самодостатнім повторенням усіх входів.

| Контроль | Фактичний RGB |
|---|---|
| Encoded gray128, один decode/encode | 128 / 128 / 128 |
| Black/white bilinear midpoint | 188 / 188 / 188 |
| Gray128/white midpoint | 205 / 205 / 205 |
| Linear AO128 і emission mask128 | 188 / 188 / 188 |
| Straight alpha128 над чорним | 188 / 188 / 188 |
| +X/+Y normal під відповідним sun | 255 / 255 / 255 |
| −X/−Y normal під тим самим sun | 0 / 0 / 0 |
| Near/far R16 depth під одним point light | 220 → 113 |
| Emission off/on, незмінний albedo hash | 0 → 188 |
| UI поверх світу | 31 / 157 / 219 без grading |
| Right-edge albedo біля білого texel | 128, без bleed |
| Right-edge AO255 біля AO0 | 255, без bleed |
| Right-edge emission128 біля mask255 | 188, без bleed |

Кожний зразок має alpha255. Прямі negative controls: missing/wrong-format/stale bindings, fractional UV, legacy data binding та typed legacy draw. Шість невалідних manifest-valid schema-2 candidates збережено; старий catalog лишається встановленим. Fault positions 1/3/6 нового завантаження — absolute attempts 7/9/12 після шести baseline textures — приймають тільки exact injected error; після кожного старий catalog реально rendered/captured/decoded. Caller UV/emission mutation не впливає на збережений матеріал.

Окремі актуальні native HDRColorProbe (34 PNG), SurfaceLightingProbe (18 PNG), RenderFrameTests/text+buffer recovery та RenderBatchTests (1 PNG) — exit0. [HDR report](../../artifacts/material-bridge-2026-10-06/hdr-final-v3/hdr-color-report.json), [surface report](../../artifacts/material-bridge-2026-10-06/surface-final-v3/surface-lighting-report.json), [frame log](../../artifacts/material-bridge-2026-10-06/check-final-v3/render-frame-native.log), [batch log](../../artifacts/material-bridge-2026-10-06/check-final-v3/render-batch-native.log).

## GameView і візуальне приймання

У порівнянні з попереднім HDR checkpoint: 34/34 HDR PNG і 18/18 surface PNG byte-identical відповідним captures. Це перевірка цих контрольних сцен, не загального GameView чи продуктивності.

Свіжий Legacy GameView/RmlUi/D3D12 matrix завершився exit0, 15 PNG / 6 locale-resolution-scale cases. F1 hidden прибирає HUD commands/annotations; F1 restored **byte-identical відповідному staged-selected-stockpile кадру**, не іншому ordinary fixture. [Report](../../artifacts/material-bridge-2026-10-06/legacy-final-v3/report.json), [native log](../../artifacts/material-bridge-2026-10-06/check-final-v3/gameview-native.log).

Порівняння v3 із попереднім `artifacts/hdr-color-2026-10-06/legacy-02`: **15/15 same-name PNG byte-identical за SHA256**. Проміжний v2 із дрібними RGB-відмінностями збережено, але він не є фінальним proof. Свіжі літній/зимовий/F1-hidden actual кадри й target-0262 переглянуто: живий світ/HUD все ще не відповідає концепту повністю. Прості analytical panels доводять операції shader, не художній вигляд будинку/людини. Readback PNG не є моніторним scan-out чи відео; доступний native desktop-control surface у цій сесії не підтримується.

## Ідентифікація фінальної матеріальної збірки

| Input | SHA256 |
|---|---|
| Ribterra.exe | `9569dd1db478b99ee03fadece0569a68ae70b21a8f36b5830dbdac22ed565d4b` |
| MaterialBridgeProbe.exe | `4e6222a76bbc5eb0bbe9836b78c2cd4cffff55ded6e9291877bba8a3c22a51b8` |
| ReworkVisualProbe.exe | `fbd987800676342013ebe0c009b28ee547d9ddf41262ee11ff44d9d65af7e583` |
| Shader manifest | `d55f4e1327682af5a2a5d535ddee44f9aed833d4daf00e483fc4535d84fa7ce8` |
| Asset content | `e428def3f77bf6f1a5365dc21bda9d2c57f9962c47d0afe79935012f7a06121c` |

Проміжні build logs, snapshots, fixtures, native-a/native-edge збережено. Preliminary check-a мав stale source/manifest failure під час наступних змін shader; він не використовується як green proof. Збірку/тестові файли не видалено. Launcher досі вказує на preview-2026-10-05-r2, не на цей probe. Це не install/release/commit/push.

## Що далі, до продовження гри

1. [Додано прямий native GPU calibration P/N на редагованому geometry master](MATERIAL_GEOMETRY_CALIBRATION_CHECK_2026_10_06_UA.md): 72 interior samples. Це diagnostic cabin/slope, не concept-ready house/resident kit; regression compatibility adapter закрито наведеним вище matrix.
2. Мінімальний нейтральний house/store/workshop/tree/resident+body/cargo/terrain/water/bridge kit за Art spec із реальними normal/depth/emission/masks та production GameView receiver integration. Поточний `source_hash` лише декларує identity; повного master/export provenance тут ще немає.
3. Повний Core graph, незалежні джерела/occlusion, контактні й напрямлені тіні, sun/moon/time/weather, мокрі матеріали, вода/reflections, просторова атмосфера й сезонні ефекти.
4. Temporal concept matrix, effect-preserving quality settings і профілювання ≥120 FPS при 1920×1080 на RTX 3050. Цей capture-based контроль продуктивність не вимірює.

Ponytail/ultrawork розділили projection/material/probe/review; karpathy-guidelines/cpp-coding-standards застосовано до typed boundaries, локального scope та candidate RAII. Відкриті вимоги не скорочувалися. Пряма вимога engine-first має пріоритет над старим deferred-Dynamic порядком документів.

Технічні опори: [Direct3D sRGB conversion-before-filter](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#FormatModifiers), SDL [format/usage query](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat), [tight transfer layout](https://wiki.libsdl.org/SDL3/SDL_GPUTextureTransferInfo), [GPU upload timeline](https://wiki.libsdl.org/SDL3/SDL_UploadToGPUTexture). Повний поріг: [E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).
