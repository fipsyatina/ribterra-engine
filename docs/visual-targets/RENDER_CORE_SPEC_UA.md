# Ribterra — технічна специфікація Render Core для 0.26.1

**Новий контракт E10 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md) додає просторову far/morning/coastal густину в GroundLighting, WorldForward і Reflection source до alpha, не в Composite. Opt-in `WaterAtmosphereSlice`, SurfaceLighting384 prefix +Atmosphere640 =1024 bytes b0; без додаткових attachments або uniform slots. Повні engine gates лишаються OPEN; контракт не є доказом native/performance приймання.

**Новіший bounded follow-up 08.10:** [raw binary WaterCoverage / odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md), fresh build02/native05–06, має actual GPU tuples і fault36/retry54/restore72. Це не intermediate AA, усі recovery failures, normal startup або повне приймання. Нижчі checkpoint записи збережені як історія своїх source cohorts.

**Поточний bounded checkpoint 08.10:** [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) працює в спільному native renderer через opt-in GameView adapter. [Окремий контракт](WATER_REFLECTION_V1_CONTRACT_UA.md) уточнює новий кумулятивний граф: 5 MRT, окремий RGBA8 carrier і half-resolution HDR Reflection; старі графи та Contact V1 W=0/1/2 незмінні. Це технічний контрольний набір, не концепт-арт або приймання E09. E01–E17/normal startup/AA/input/video/presets/120 FPS OPEN.

**Актуальний partial authored checkpoint08.10:** [GameArt sky-triangle bridge](AUTHORED_SKY_BRIDGE_CHECK_2026_10_08_UA.md) перевіряє незалежні schema-2 локальні XYZ трикутники, явний world-to-camera adapter і атомарність кадру/registry. Це ще не authored GPU shadow або повне приймання Render Core. E01–E17/GameView/art/presets/120FPS OPEN.

**Актуальний partial checkpoint08.10:** [Sun/Moon SkyShadowSlice](SKY_SHADOW_CHECK_2026_10_08_UA.md) має source-specific D32 depth passes на opt-in production renderer, тільки для bounded fixed-camera opaque triangles. Це shadow-only geometry, не перехід гри на повний3D. Історична заборона depth target нижче не описує цей новий prerequisite; world HDR/UI і старі graph outputs збережені. E01–E17, authored art/GameView/full suite/presets/120FPS OPEN; engine-first порядок має пріоритет над старим scaffold/deferred планом.

**Історичне рішення 27.09 (не поточний стан bounded opt-in графів):** повний граф нижче описував майбутній режим Dynamic і scaffold API. Номер релізу не призначено; нормальний startup не переключено. Поточний порядок — [повний двигун перед грою](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md), а актуальні часткові докази наведено вище. [DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) та [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md) зберігаються як історичний план/алгоритм, не підтвердження готовності.
**Дата:** 27 вересня 2026.
**Статус:** **PROPOSED / UNMEASURED**. Це план реалізації; описаних passes, GPU layouts і нових ефектів у runtime ще немає.
**Ціль:** довести керований 2.5D renderer на чинному SDL_GPU/D3D12 шляху до малого нативного зрізу до масового виробництва ассетів.
**Головний план:** [Graphics Engine Plan V2](GRAPHICS_ENGINE_PLAN_V2_UA.md).
**Суміжний контракт:** [World FX](WORLD_FX_SPEC_UA.md).
**Первинні SDL-джерела:** посилання і межі доказу зібрані у [розділі 12 головного плану](GRAPHICS_ENGINE_PLAN_V2_UA.md#12-первинні-технічні-джерела).

## 1. Рішення й межа
Робоча архітектура — чинний 2.5D renderer із фіксованою ізометричною камерою, painter order для спрайтів і окремими bounded passes для матеріалів, локального світла та World FX.
Це рішення діє, доки нативний доказ не покаже конкретну непереборну межу.
Амбіційність концепт-арту сама по собі не дозволяє переписати рушій або перейти на повну 3D-сцену.
У майбутньому режимі Dynamic заплановані:
- динамічне світло на ground, фасадах і мешканцях за normal/depth material та окрема emission; контактні тіні для кожного мешканця й напрямлені authored silhouettes; local-light occlusion через OBB/capsule prism approximation;
- wetness response на дозволених ground receivers; плоска річка, bounded reflections, shoreline masks і World FX; world-space haze, який не замінює Fog of War;
- rain/snow particles і косметичні footprints/compressed-snow shading; один viewport/DPI/input контракт для світу й RmlUi; schema 2 для матеріалів із транзакційним upload і schema 1 adapter.
Не входять:
- depth target, perspective camera або повна 3D-геометрія; повний deferred PBR, GI, ray tracing, SSR або planar capture всього кадру; rigid-body, cloth чи fluid physics;
- фізична деформація або collision снігу; водоспади, повінь, кілька рівнів води або simulation feedback із renderer; автоматичне переписування рушія після невдалого окремого shader experiment.

## 2. Перевірена поточна база
Нижче — **історичний source snapshot початкового плану**, не поточний стан після engine-first кроків 05–07.10.2026. Актуальну production opt-in основу та відкриті межі описують [GroundForward](GROUND_FORWARD_CHECK_2026_10_07_UA.md) і [local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md); звичайний GameView ще BakedCompat. Таблицю збережено, щоб не переписувати provenance старого плану.
| Місце | Поточний факт | Наслідок для V2 |
|---|---|---|
| `include/ribterra/render_frame.hpp` | `Frame2D` має 2D vertex, UV, RGBA8 color, texture handle, painter commands і проходи Terrain/WorldOrdered/Atmosphere/Fog/Ui | Немає material, light, receiver, decal чи reflection контрактів |
| `src/render/renderer2d.cpp` | GPU path створює один `R8G8B8A8_UNORM` offscreen target | Потрібні нові attachments, pipelines і lifecycle |
| `src/render/renderer2d.cpp` | Дозволено два frames in flight; кожний frame має fence і upload buffers | Нові attachments також мають мати два незалежні набори |
| `shaders/sprite.vert.hlsl` | Є лише `logical_extent` uniform | Нові програми потребують власних точних binding contracts |
| `shaders/sprite.frag.hlsl` | Результат — sampled texture × vertex color | PNG сам не створить wetness, occlusion чи bounded reflection |
| `tests/shader_contract.py` | Manifest schema 1 очікує один sampler у fragment і один uniform buffer у vertex | Тест і manifest треба розширити разом із програмами |
| `src/client/game_art.cpp` | Loader приймає тільки `world.json` schema 1 і завантажує RGBA atlas page | Schema 2 має бути transactional; старий каталог проходить adapter |
| `include/ribterra/world_projection.hpp`, `src/client/view.cpp` | Спільна affine-проєкція використовує еталонний ромб 58×29; view усе ще має logical 1440×900 | 1440×900 не може лишатися прихованим DPI контрактом |
| `src/client/main.cpp` | Після `GameView::compose` frame знову отримує 1440×900, потім додається UI | Світ, UI та input мають перейти на один drawable-pixel viewport |
| `src/render/renderer2d.cpp` | Pointer event конвертується через window/pixel/logical scale | Після V2 конвертація window→drawable виконується рівно один раз |
Software backend корисний для command-generation тестів.
Він не доводить DXIL bindings, HDR targets, blend, mask leakage, timing або зображення нативного D3D12 renderer.

## 3. Нормативні простори координат
### 3.1. Одна world metric
Усі V2 world interfaces використовують `render_xyz` у tile-equivalent units:
- `render_x` і `render_y` — позиція в клітинах світу; `render_z = legacy_elevation × 2.4 / 29.0`; camera-relative координати обчислюються на CPU перед float32 upload;
- `screen_y` віднімає `29 × render_z × effective_zoom`; light, occluder, decal, reflection caster і receiver використовують ту саму metric; shader не вгадує висоту з painter order або alpha.
Поточні authored polygons footprint переводяться в OBB або capsule prism approximation під час frame preparation.
Оригінальний polygon лишається CPU validation/debug source; GPU не отримує довільний polygon solver.
### 3.2. Camera й zoom
Нормативний масштаб:
`effective_zoom = user_zoom × drawable_height / 900`.
`drawable_height` — physical drawable pixels поточного кадру, не window points і не старі logical 900.
Camera origin віднімається до upload, щоб великі world coordinates не псували float precision.
Контрольні zoom для dev.09:
- `0.62` — широкий огляд і stress для mask seams; `1.30` — основний геймплейний кадр; `3.00` — перевірка leakage, anchor drift і authored silhouette.
### 3.3. World depth без depth buffer
Painter order лишається авторитетним для спрайтів.
`world_meta.r` містить camera-relative depth metric для haze й діагностики, але не вмикає depth test.
Кліфи, дахи й міст перекривають world objects через стабільний порядок і receiver/occlusion masks.

## 4. Запропонований CPU frame contract
`Frame2D` лишається верхнім контейнером кадру, але отримує V2 world inputs.
Усі наведені типи — **PROPOSED interface names**, не наявні header declarations.
| Тип | Мінімальні поля | Власник і правило |
|---|---|---|
| `WorldFxFrame` | presentation time, epoch, rain/snow/haze, wind, temperature, snow cover, quality | Client projection із authoritative state; renderer не змінює simulation |
| `MaterialDesc` | material id, albedo, normal/material map, camera-depth map, emission map, receiver flags, alpha mode, color space, dry/wet response | GameArt schema 2; immutable протягом submitted frame |
| `LocalLight2D` | stable id, render XYZ anchor, radius, direction/cone, linear RGB/intensity, receiver mask, occluder range | GameView/renderer preparation; window light не є fullscreen glow |
| `Occluder2D` | stable id, OBB або capsule prism, base/top render Z, shadow class, reveal | Підготовлений із authored footprint; не physics body |
| `GroundDecal2D` | stable id, render XYZ anchor, axes/extent, material/decal id, opacity, receiver mask | Contact shadow або footprint; cosmetic only |
| `ReflectionCaster2D` | stable id, sprite/material, anchor/bounds, water plane, strength, reveal | Лише дозволені видимі caster-и; UI/particles не входять |
Frame preparation зберігає stable order.
Жоден effect input не отримує callback у SimCore.
Reveal filtering відбувається до GPU buffer upload.

## 5. Material і receiver contract
### 5.1. `MaterialDesc`
Кожен world draw посилається на material id, а не на випадковий набір texture handles.
Обов’язкові semantic bindings:
| Binding | Color space | Alpha | Призначення |
|---|---|---|---|
| albedo | sRGB decode явно до linear | straight coverage | нейтральний base color |
| normal/data | linear | linear data | authored normal XY, roughness/wet eligibility за затвердженим packing |
| emission | sRGB RGB → linear; mask linear | straight mask | emissive RGB/strength без другого night darkening |
| camera-depth | linear R16_UNORM + range | data | відновлення поверхні sprite за affine basis із Dynamic Lighting spec |
| mask | linear | linear data | receiver, water, shore, snow або decal masks |
| UI atlas | legacy byte path | legacy premultiplied | тільки UI pass, без world grade |
World targets і всі lighting calculations працюють у linear color.
Texture creation не покладається на неявний swapchain decode.
Albedo RGB декодується з sRGB рівно один раз.
Mask/normal/roughness значення ніколи не проходять sRGB transform.
World albedo source використовує straight alpha.
Перед linear alpha blend shader формує premultiplied output або pipeline явно використовує straight-alpha blend; один draw не може змішувати обидві конвенції.
Manifest фіксує convention для кожної програми.
UI зберігає чинні premultiplied bytes і відповідний blend path.
### 5.2. Ground data і world metadata
Чотири Ground G-buffer targets:
- ground_albedo RGBA8_UNORM_SRGB: RGB neutral albedo, A contact/AO visibility; shader outputs linear, target encodes, sampler decodes рівно один раз.
- surface_world RGBA16F: camera-relative render XYZ; семантика W залежить від explicit graph version, як нижче. Не трактувати contact tag як coverage.
- surface_material RGBA8_UNORM: RG oct normal, B roughness, A sun visibility.
- surface_flags RGBA8_UNORM: R receiver bitfield, G wetness, B shore01 для Water або snowCover01 для суші, A reveal.

Flags семплуються point, coverage/material — за відповідним program contract. Contact shadow пише лише ground_albedo.A; directional silhouette лише surface_material.A. Blend множить destination visibility, не змінюючи RGB/normal. Footprint змінює лише затверджені albedo/normal/roughness channels, не world position, class або reveal.

**Versioned Contact V1 (2026-10-07; partial GPU proof, не full engine).** [Поточний звіт](CONTACT_GPU_CHECK_2026_10_07_UA.md). GroundForwardSlice лишає SurfaceWorld.W=`0` clear/invalid і `1` valid, старі readback asserts не послаблюються. Окремий opt-in GroundContactSlice використовує W=`0` invalid, `1` ordinary valid, `2` eligible valid. World target opaque/без blending, point/clamp. Contact приймає рівноW2/reveal1; новий Lighting — тількиW1/2. Усі8local bits і SurfaceFlags RGBA незмінні. Typed Ordinary/Flat/Slope/Bridge транспортується через копію GroundMaterialGpu.flags[1] лише в новому graph; caller-side reserved fields та весь старий lane лишаються zero. Native paired controls доводять receiver/reveal/plane exclusions, але нового raw-W readback немає.

Цей bounded contact version **не підтримує Reflection/Water** і не зберігає antialiased coverage у W. Раніше описана майбутня coverage-семантика належить окремому full-engine graph, не цій версії. Перед full-engine Reflection потрібен окремий versioned coverage carrier з native edge proof та оновленим memory/resize/performance ledger; конкретний carrier ще не прийнятий. Заборонено `saturate(W)`, interpolation або reuse contact tag як coverage. Це не зняття вимоги Reflection і не full-engine acceptance.

ContactSurfaceGpu80: anchor/opacity, U/halfExtent, V/halfExtent, canonical texel-centre UV, pivot/planeTolerance/reserved0; offsets0/16/32/48/64. Prepared Draw112/zero padding, noncentral pivot, orthonormal tangents, extents≥1e-4, normalized plane band **[.02,.04]**, all4corners±band у±32, normal.z≥.5. Старий CPU prototype minimum1e-4 — історичний, не GPU tolerance. Це renderer policy, не terrain traversal/buildability. Reveal1, opacity(0,1], known facing, nonzero stable/owner та unique stable IDs; одинowner може мати кількаparts. Cap128provisional, overflowreject. Registry перевіряє alive generation, R8, integerrect/zero gutter, canonicalUV/facing до command acquisition; холодне release чекаєidle та неwrap-итьgeneration. Projection/scissor/GPUpass реалізовані, GameArt filtering/adapter й concept kit — ще ні.

Contact pass: fullscreen triangle, eight-corner bounded physical scissor зі спільної canonical projection. Reopen тільки ground_albedo LOAD/STORE; nearest World/Flags + linear-clamp R8 mask/LOD0. A-only write mask, alphaZERO/ONE_MINUS_SRC_ALPHA/ADD: newAO=oldAO*(1-mask*opacity), безactive-target reads або RGB зміни. Зберігаються7attachments/lighting384; graph5passes порожній /6populated. Registry≤256live/16MiBlogical R8,3..1024dimensions,≤1MiB/art; texel-centre endpoints уdeclaredrect≥3×3 зzeroouterringіnonzerointerior. Allocation/upload failurespublishnothing; handles-only framesstale-check доGPUwork. Native53checks×2,211CPUchecks таshader contracts — partial proof, не full Engine/FPS/art/motion acceptance.

world_meta RG16F створюється у GroundLighting: R haze depth, G reveal. World sprites потім пишуть depth/reveal фактичного видимого fragment. Для coverage output=float4(depth*coverage,reveal*coverage,0,coverage), blend ONE/ONE_MINUS_SRC_ALPHA. Native backend test має довести використання source alpha для RG16F; інакше promotion RGBA16F із новим memory ledger. World не sample-ить world_meta, поки пише її.

## 6. Explicit GPU layouts
SDL shader program отримує `StructuredBuffer`, а CPU buffer має збігатися з HLSL полями за offset і size.
Не посилатися на `std430`: це не контракт SDL/DXIL StructuredBuffer.

Production уточнення 07.10: pinned SDL 3.4.16 D3D12 створює RAW storage SRV, а pinned shadercross 3.0.0-preview.2 через SPIR-V round-trip lowering виробляє byte-addressed DXIL. Тому shader contract перевіряє або точний reflected struct, або саме відповідний RAW resource handle, `index << 6`, loads на 0/16/32/48 і byte-count → record-count `/64`. Наявності struct у початковому HLSL недостатньо. Native доказ і межі: [local visibility](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md).
### 6.1. `LightGpu` — 80 bytes
| Offset | HLSL поле | Значення |
|---:|---|---|
| 0 | `float4 anchor_radius` | camera-relative render XYZ, radius |
| 16 | `float4 direction_cone` | normalized direction XYZ, outer cosine |
| 32 | `float4 color_intensity` | linear RGB, intensity |
| 48 | `uint4 occluder_range_flags` | first occluder, count, receiver mask, stable id |
| 64 | `float4 height_falloff_reveal` | inner cosine, falloff exponent, reveal01, reserved=0 |
CPU declaration має `alignas(16)` і п’ять 16-byte members.
Обов’язкові compile-time checks: size 80; offsets 0/16/32/48/64.
HLSL declaration має ті самі п’ять members у тому самому порядку.
### 6.2. `OccluderGpu` — 64 bytes
| Offset | HLSL поле | Значення |
|---:|---|---|
| 0 | `float4 center_base` | center render XY, reserved Z=0, base height |
| 16 | `float4 axis_x_extent` | normalized XY axis X, half extent X / capsule half length, top height |
| 32 | `float4 axis_y_radius` | normalized XY axis Y для OBB, half extent Y, capsule radius |
| 48 | `uint4 shape_flags_id` | shape 1=OBB / 2=capsule, fully-revealed flag=1, stable id, receiver owner id |
CPU declaration має `alignas(16)`, size 64 й offsets 0/16/32/48.
Capsule використовує axis X + half length + radius; axis Y/extent Y лишаються валідними нулями.
OBB використовує обидві осі й extent-и; radius дорівнює нулю.

**Версійоване уточнення reserved word, 07.10.2026:** `shape_flags_id.w` у production GroundForward local-visibility extension є ненульовим owner id, не reserved zero. Лише відповідний ненульовий receiver owner виключає свою approximation із local visibility; чужі blockers не виключаються. Ground owner=0. `ForwardMetadataGpu` розширено з 16 до 32 bytes: reveal `float4` на offset0 та `uint4(mask, owner, 0, 0)` на offset16. `SpriteSurfaceGpu`160 / `LightGpu`80 / lighting384 не змінені. Legacy не споживає local occluder list/ranges.

OBB осі unit/orthogonal; capsule perpendicular виводиться з axis X, не з reserved axis Y. Min height, OBB half extents і capsule radius — `1e-4` у float32; capsule half length може бути нульовим. Export canonicalizes числа до float32 **до** validation/output; GameArt повторно перевіряє transformed dimensions транзакційно. При `reveal<1` blockers не отримують runtime IDs, не витрачають budget і не upload-яться. Поточний camera-relative GroundForward domain ±32 охоплює P, local-light anchors і весь prism footprint/Z; це не authoring/export domain ±8192 та не глобальне обмеження Legacy.
### 6.3. Caps і overflow
Початковий Medium cap — **256 global visible occluders** і **32 occluders per light**.
32 per light — обране тут попереднє число, **PROPOSED / UNMEASURED**; його треба передати головному плану при наступній редакції.
CPU culling order:
1. reveal і layer;
2. expanded viewport;
3. light influence intersection;
4. distance до light;
5. stable id як tie-break.
Global overflow не може відкидати потрібні blockers: консервативно вимкнути відповідний light contribution з diagnostic; acceptance провалений.
Per-light overflow у контрольній сцені є debug/acceptance failure.
Renderer не має права мовчки відкинути blocker і показати світло крізь будинок.

Реалізований bounded slice 07.10 має strict fail-closed rejection **усього некоректного кадру до GPU submission** при 257-му global/33-му per-light blocker або некоректному range, а не тихе truncation. Spatial culling/tiled light lists, залежні від preset caps і fallback окремого contribution ще не реалізовані й не виміряні. Ліміти 256/32 не доводять ≥120 FPS.

## 7. Майбутній graph: шість passes, вісім attachments
Цей graph уточнює попередню пропозицію із seven targets/half LocalLight. Вона була недостатня для матеріалів і dynamic-lit sprites та більше не використовується.

| Pass | Вхід і вихід |
|---|---|
| 1 Reflection | Half-resolution RGBA16F CLEAR/STORE; eligible caster forward-lit із normal/depth/emission, bounded billboard reflection. Повного кадру не копіює |
| 2 GroundGBuffer | Чотири MRT CLEAR/STORE з розділу5.2: terrain/water/bridge base, footprint, AO і sun silhouettes. Жоден target не семплується під час запису |
| 3 GroundLighting | Читає чотири G-buffer + reflection + CPU tile light/blocker lists. Пише full scene_linear RGBA16F і world_meta RG16F. Повний ground lighting зі sharp albedo/normal/specular; half LocalLight видалений |
| 4 WorldForward | LOAD/STORE scene_linear + world_meta MRT. Ground buffers read-only. Painter-ordered lit sprites реконструюють P/N й рахують власні local lights; не накладають ground-light texture на фасад. Emission/weather мають явні material families |
| 5 Composite | Читає scene/meta, робить output encode і фінальний secrecy FoW; пише final RGBA8_UNORM. UI й повторного night grade тут немає |
| 6 UI | LOAD/STORE final, чинний RmlUi blend і text; без world grade/туману |

Haze застосовується у GroundLighting/WorldForward із фактичної P до alpha blend. Частинки теж отримують haze у своєму fragment; Composite не додає його вдруге. world_meta служить діагностиці й final reveal. Всі opaque/alpha матеріали використовують той самий haze function.

Half reflection використовує повні drawable координати/NDC при half viewport. Не ділити camera zoom вдруге. Texture UV нормовані за розміром target; distortion pixel amplitude множиться на internal scale.

Full-resolution GroundLighting має CPU screen-tile lists, стартовий bin 32×32 px. Перевірити 4/8/12 lights per ground receiver для Low/Medium/High; не виконувати global48×32 ray tests у кожному pixel. Per-sprite lists обмежені окремо. Усі caps початкові, невиміряні.

## 8. Світло, emission і тіні
### 8.1. Порядок освітлення
Для terrain і world sprite: decode albedo → reconstruct P/N → albedo*(ambient*AO + sun diffuse*sun visibility + local diffuse*per-light visibility) + material specular + emission → haze → output/FoW. Точна формула і depth basis у Dynamic Lighting spec. Sun shadow не затемнює інше local light.
Немає другої night-grade операції після emission.
Вікно вночі лишається emissive, а його projected ground light згасає за radius/cone й occlusion.
Старий `fx.3` для переведених об’єктів прибирається, щоб не було подвійного glow.
### 8.2. Contact shadows
Кожен видимий unit, включно з directional resident sprite і carried cargo, має contact shadow decal.
Contact shadow прив’язується до ground anchor, масштабується обмежено за render Z і не отримує input hit target.
Вода, дах, UI й unrevealed surface не є contact receivers.
### 8.3. Directional silhouettes

**Versioned direct-sky prerequisite07.10:** SkyContactSlice має окремий MoonLightingGpu32
у ground b1/material-forward b3; lighting384 і сім targets незмінні. Moon direct term
не використовує sunvisibility/contactAO/localvisibility. Це ще НЕ directional silhouette
pass/carrier або time/weather implementation; деталі й докази — [Sky direct](SKY_DIRECT_CHECK_2026_10_07_UA.md).
Authored directional shadow silhouette є окремим art channel від visual sprite.
Вона обирається за facing/light class і проєктується на дозволений ground receiver.
Per-light occlusion для window cone є окремою OBB/capsule prism системою; silhouette texture її не замінює.
Обмеження:
- довільна самотінь вертикального фасаду не підтримується; тінь не перегинається фізично через довільний cliff; підтриманий slope має окремий receiver proof;
- на непідтриманому cliff тінь clip-иться або використовує задокументований fallback; leakage на невідоме, воду, дах або UI є failure.

## 9. Render targets і пам’ять
Вісім attachments на кожний із двох frame sets:

| Attachment | Format | Bytes/full pixel |
|---|---|---:|
| reflection 0.5×0.5 | RGBA16F | 2 |
| ground_albedo full | RGBA8_UNORM_SRGB | 4 |
| surface_world full | RGBA16F | 8 |
| surface_material full | RGBA8_UNORM | 4 |
| surface_flags full | RGBA8_UNORM | 4 |
| scene_linear full | RGBA16F | 8 |
| world_meta full | RG16F | 4 |
| final full | RGBA8_UNORM | 4 |

Разом38 bytes/full pixel/set; два sets:1920×1080=150.29MiB,2560×1440=267.19MiB. Початкові attachment caps176/304MiB; це розрахунок, не профіль GPU. Asset atlases, UI, buffers, upload і тимчасовий resize overlap рахуються окремо. Транзакційний resize одночасно тримає старий і новий комплекти; цей peak перевіряється явно. RG16F→RGBA16F metadata fallback додає4bytes/fullpixel/set й потребує перерахунку.

Немає depth target; R16 data texture спрайта не є depth attachment.

## 10. Capability query, resize і lifetime
Перед створенням кожного target renderer викликає format support query з фактичним format і usage flags.
Перевірка формату без `COLOR_TARGET | SAMPLER` usage не є доказом придатності.
Resize transaction:
1. отримати non-zero drawable size;
2. дочекатися fences обох старих frame sets;
3. перевірити formats/usages для нового розміру;
4. створити повний candidate набір із восьми attachments × два sets;
5. створити/перевірити size-dependent views і pipelines;
6. лише після повного успіху swap-нути active set;
7. звільнити старі ресурси після завершених fences.
При minimize/zero drawable renderer пропускає world submission і не створює 0×0 texture.
При partial allocation failure candidate повністю звільняється, active set лишається валідним, а error/diagnostic записується.
Такий fallback не дозволяє зарахувати dev stage як completed.
Failure під час fence wait, shader creation, format query, target creation, upload або submit має містити program/format/usage/size/frame-set context.
Silent disable обов’язкового pass заборонено.

## 11. Shader program contracts
Один універсальний лічильник sampler/uniform із поточного `shader()` недостатній.
Кожна program pair має окремий manifest record:
- entry point і stage; vertex inputs/outputs; color target count/formats;
- uniform buffers; samplers; read-only storage buffers;
- storage textures, якщо вони з’являться; blend/write masks; source/DXIL/reflection hashes;
- declared alpha і color-space convention.
Заплановані program families:
- ground material/MRT; water/reflection composite в Ground; reflection caster;
- local light; world sprite Dynamic2_5D/albedo+normal+depth/emission; contact/directional shadow;
- weather/decal; composite; UI legacy.
`tests/shader_contract.py` переходить на schema 2 і порівнює program-specific counts із SDL create info.
DXIL reflection має підтвердити точні `LightGpu`/`OccluderGpu` strides.
C++ static assertions підтверджують sizes/offsets незалежно від reflection JSON.

## 12. Art schema 2 і транзакційний upload
### 12.1. Schema 2
`assets/art/world.json` schema 2 описує pages, textures, color space, alpha convention, materials, sprite regions, pivots і normal/depth/emission/mask channels за material family. Для Dynamic2_5D normal/depth обов’язкові, missing data відхиляє каталог; schema1/BakedCompat не проходить dynamic proof.
Material не може послатися на page із несумісним semantic.
Atlas limits і rectangle validation з schema 1 зберігаються.
### 12.2. Schema 1 adapter
Schema 1 не видаляється в 0.26.1.
Adapter створює albedo-only `MaterialDesc` із:
- explicit sRGB albedo; straight alpha для world sprites; default neutral normal/data;
- zero emission; no wet/light/reflection receiver, доки роль не додана явно; чинні pivot/rect/texture ids без перенумерації.
Adapter забезпечує сумісне завантаження, але не видає старий asset за V2 material proof.
### 12.3. Transaction
Loader спочатку parse-ить і повністю валідовує candidate catalog.
Потім upload-ить усі candidate textures/material buffers у тимчасового owner-а.
Будь-яка помилка видаляє весь candidate без зміни active catalog.
Active catalog swap виконується лише після повного upload і validation.
Старий catalog звільняється після fences кадрів, які ще посилаються на нього.
Тестова ін’єкція failure має пройти на першій, середній і останній texture, а також на material buffer upload.
Після кожної відмови active handles лишаються валідними, а leaked resource count дорівнює нулю.

## 13. Viewport, DPI, input і RmlUi
Поточна фіксація 1440×900 є невідповідністю цільовим 720p/1080p/1440p кадрам.
V2 contract:
- `Frame2D.logical_size` дорівнює drawable width/height у physical pixels; world projection використовує той самий drawable viewport; `effective_zoom` множиться на `drawable_height / 900`;
- RmlUi context dimensions дорівнюють тим самим drawable pixels; SDL mouse coordinates конвертуються window→drawable рівно один раз; renderer, GameView і RmlUi не виконують повторного letterbox conversion;
- picking і scissor використовують drawable coordinates; user UI scale є окремим параметром від OS DPI й world zoom.
Actual rasterized font size після всіх scale operations не може бути меншою за 15 physical pixels на 720p.
Text layout, hit regions і visible glyph size перевіряються разом.
Збільшення UI scale не змінює world zoom або mouse world position.
Обов’язкові viewport cases:
- 1280×720, 100% OS scale; 1920×1080, 100% і 150%; 2560×1440, 100% і 150%;
- resize під час гри; minimize/restore; drag selection і minimap click після DPI transition.

## 14. Source integration map
Позначення **CURRENT** означає наявний файл; **PROPOSED** — рекомендована точка, яку ще треба створити.
| Path | Статус | Відповідальність V2 |
|---|---|---|
| `include/ribterra/render_frame.hpp` | CURRENT, change planned | Frame inputs, material/light/decal/reflection handles, drawable size |
| `include/ribterra/renderer2d.hpp` | CURRENT, change planned | Renderer options, capabilities, telemetry, viewport conversion |
| `src/render/renderer2d.cpp` | CURRENT, major change planned | resources, six passes, fences, shader programs, capture |
| `src/client/view.cpp` | CURRENT, change planned | render XYZ, receiver geometry, painter inputs, shadows |
| `src/client/main.cpp` | CURRENT, change planned | drawable sizing, one input conversion, UI/world frame order |
| `src/client/game_art.cpp` | CURRENT, change planned | schema 2 transaction і schema 1 adapter |
| `include/ribterra/render_materials.hpp` | PROPOSED | `MaterialDesc`, receiver flags, CPU/GPU ABI |
| `include/ribterra/world_fx.hpp` | PROPOSED | `WorldFxFrame`, decals, reflection casters, FX telemetry |
| `src/render/world_fx_renderer.cpp` | PROPOSED | water/reflection/haze/weather/footprint draw preparation |
| `shaders/*.hlsl` | CURRENT directory, new programs proposed | program-specific contracts із єдиним linear/alpha policy |
| `tests/render_frame_tests.cpp` | CURRENT, extend | validation, ordering, ABI, failure cases |
| `tests/shader_contract.py` | CURRENT, extend | schema 2 program reflection/bindings/hashes |
Матеріальні bindings задає `MaterialDesc`; довільний texture slot у view code не є новим material API.
World FX використовує ті самі attachments і contracts, а не створює паралельний renderer.

## 15. Етапи й залежності
Повний Dynamic відкладений користувачем. Виконувати [DL-01–08](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md), коли роботу буде продовжено. Ранні dev.01–09 зі старого графічного плану — технічні work packages, не наказ реалізувати Dynamic в поточній 0.26.1. Зараз реалізована лише заготовка API; 6-pass graph не додавався.

Першими будуть capability/resource/material contracts, потім малий lit kit і реконструкція форми, далі source/occlusion proof, performance, тільки після цього UI setting/persistence. Масові lit assets не виготовляються до приймання малої сцени.

## 16. Майбутній stop/go для Dynamic
Потрібні безперервні native D3D12 videos production executable, а не software probe:
- zoom `0.62`, `1.30`, `3.00`; day і night; clear/dry, rain/wet, snow і storm;
- camera pan над ground, slope, river, bank, bridge і revealed frontier; unit із contact/directional shadow і footprints; window emission/light з blocker до/після переміщення;
- resize або restore без stale resource; debug counters для light/occluder overflow, attachments і memory ledger.
Кожне відео/manifest фіксує commit, build, shader/asset hashes, GPU, driver, backend, drawable size, preset, seed/save hash, camera/zoom і diagnostic/authoritative source.
Software capture, generated image або isolated shader sample не закриває gate.
Dynamic proof проходить лише якщо:
- усі шість passes працюють у production graph; occluder overflow у контрольній сцені дорівнює нулю; receiver metadata не пошкоджена decals, а world_meta оновлена lit sprites;
- UI не grade-иться й input збігається після DPI conversion; mandatory effect не вимкнений fallback-ом; memory/resource numbers записані як виміряні, а не переписані з цього плану;
- native відео показує temporal stability на всіх трьох zoom.

## 17. Автоматизовані перевірки
| ID | Перевірка |
|---|---|
| `RC-ABI-01` | `LightGpu` size/offset 80 і 0/16/32/48/64 збігаються C++/DXIL |
| `RC-ABI-02` | `OccluderGpu` size/offset 64 і 0/16/32/48 збігаються C++/DXIL |
| `RC-SHD-01` | кожна program pair має точні sampler/uniform/storage counts і target formats |
| `RC-COL-01` | sRGB albedo decode один раз; linear mask не змінюється gamma path |
| `RC-ALP-01` | straight world і premult UI test patterns не мають dark/bright fringe |
| `RC-MRT-01` | Ground decal змінює scene color, але metadata bytes/halfs лишаються незмінні |
| `RC-PAS-01` | resource state audit не допускає sample+target одного subresource в одному pass |
| `RC-LGT-01` | ambient застосований до base до additive light/emission; Composite не затемнює emission вдруге |
| `RC-OCC-01` | OBB/capsule selection stable; 33-й blocker дає overflow failure у acceptance fixture |
| `RC-SHDW-01` | кожний unit має contact shadow; directional silhouette не стає light occluder shortcut |
| `RC-MEM-01` | attachment ledger відповідає drawable size, two-set count і queried formats |
| `RC-RSZ-01` | resize transaction не активує partial set і звільняє candidate після кожної injected failure |
| `RC-UPL-01` | schema 2 failure на first/middle/last upload лишає active catalog і zero leaks |
| `RC-DPI-01` | one window→drawable conversion дає однаковий world pick при 100%/150% DPI |
| `RC-UI-01` | 720p actual font ≥15 px; UI не потрапляє у world Composite |
| `RC-FOW-01` | hidden caster/light/particle не змінює revealed pixels понад AA boundary |
| `RC-CAP-01` | native capture містить правильний final target без UI/world double conversion |
Pixel/unit tests потрібні, але не заміняють video checks руху, shimmer, shadow leakage й DPI interaction.

## 18. Failure matrix
| Відмова | Runtime дія | Delivery статус |
|---|---|---|
| RGBA16F/RG16F format+usage unsupported | abort V2 initialization із точним diagnostic; окремо переглянути formats/architecture | FAIL, не completed |
| один із 16 attachments не створився | release candidate, keep valid old set якщо є | FAIL |
| shader manifest/binding mismatch | program не створюється | FAIL |
| per-light occluders >32 у acceptance scene | counter + diagnostic, blocker не відкидається мовчки | FAIL |
| global occluders >quality cap | stable cull + visible counter | допустимо лише поза acceptance fixture й після review |
| schema 2 asset/mask missing | rollback candidate catalog | FAIL відповідного dev |
| reflection/local-light allocation failure | safe render path може лишитися для діагностики | mandatory effect не прийнято |
| metadata overwrite decal-ом | disable bad program + diagnostic | FAIL |
| zero-size minimized window | skip submission, preserve resources | допустимо |
| device/submit/fence failure | stop frame, record SDL/backend context | FAIL; не рахувати успішним frame |
| software probe green, D3D12 video відсутнє | не робити runtime claim | UNVERIFIED |

## 19. Cross-platform і backend discipline
Public CPU contracts, asset schema, color/alpha semantics і shader manifest не мають залежати від Windows-only struct packing.
Усі GPU structs використовують fixed-width fields, explicit 16-byte members і compile-time offsets.
Поточний release proof для 0.26.1 — native SDL_GPU Direct3D 12 на Windows.
Це не дозволяє назвати software renderer доказом GPU correctness.
Інший native SDL backend потребує окремих compiled shaders, capability query, native video й performance evidence; відсутність такого доказу позначається unverified, а не «працює кросплатформно».

## 20. Невирішені рішення до коду
1. Підтвердити на target GPUs підтримку `RGBA16F` і `RG16F` саме з потрібними `COLOR_TARGET | SAMPLER` usage flags.
2. Перевірити, чи final `RGBA8_UNORM` + explicit linear→sRGB conversion відповідає фактичному swapchain format; не додати друге перетворення під час blit.
3. Довести packing/basis/depth precision із Dynamic Lighting spec на малому asset kit.
4. Виміряти full-resolution ground lighting і per-sprite forward lighting; culling/caps не вважати оптимізованими до профілю.
5. Перевірити попередній cap 32 occluders per light і global Medium 256 у `visual_slice_v2` та stress scene.
6. Узгодити точний behavior тіні на підтриманому slope й documented fallback на cliff до dev.05.
7. Визначити native timing method; CPU submit time не видавати за GPU time.
8. Зафіксувати ownership final UI composition між Renderer2D і RmlUi adapter, щоб не виник другий hidden offscreen target.
Жодне число budget/cap у цій специфікації ще не виміряне.
Реалізація майбутнього режиму починається з DL-01; поточна робота обмежена заготовкою, UI setting відкладений.
