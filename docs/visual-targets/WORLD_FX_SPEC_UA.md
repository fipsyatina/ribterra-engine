# Ribterra — контракт World FX для 0.26.1

**Bounded Environment checkpoint08.10:** [Environment V1](ENVIRONMENT_CHECK_2026_10_08_UA.md) спільно проєктує diagnostic hour/cloud/haze у Sun/Moon/ambient/atmosphere. Fresh build02 та native01–02 actual exit0,44/44 derived outputs exact; causal cloud/haze/source-specific shadows та незмінні local/emission перевірені. Normal startup, authoritative WeatherFxFrame, precipitation/wind/video/performance й усі full E01–E17 лишаються OPEN.

**Bounded native E10 checkpoint 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_CHECK_2026_10_08_UA.md) пройшов build03/native02–03 на RTX 3050/D3D12; 29/29 derived outputs exact, causal depth/height/coast/time/weather та unchanged raw carrier. Це діагностичний шар, не full-world/authority/video/performance або повний E10.

**Уточнення алгоритму E10 08.10:** [AnalyticAtmosphere V1](ANALYTIC_ATMOSPHERE_V1_CONTRACT_UA.md) для нового `WaterAtmosphereSlice` замінює запропоновані у §6 fog quads/HazeBandCache аналітичною world-space густиною з окремими far/morning/coastal кольорами. Це не скасовує візуальні вимоги й не приймає старі cache/quad checks або повний E10. b0=1024 тільки в трьох нових варіантах, старий384 і attachment-memory незмінні. Нативний доказ оформлюється окремо після фактичного прогону.

**Новіший bounded follow-up 08.10:** [binary GPU carrier та odd resize](WATER_COVERAGE_RESIZE_CHECK_2026_10_08_UA.md) перевірено fresh build02/native05–06. Raw binary packing прийнято лише в цій контрольній сцені; intermediate AA, довільні recovery failures і всі повні пороги відкриті. Нижчий checkpoint описує попередню source cohort.

**Поточний стан 08.10:** [Water / Reflection V1](WATER_REFLECTION_CHECK_2026_10_08_UA.md) має bounded native checkpoint у спільному production renderer, не лише overlay. [Його окремий контракт](WATER_REFLECTION_V1_CONTRACT_UA.md) уточнює carrier, класи суша/вода/настил, world-anchored flow, берегову піну й half-resolution Reflection. Нормальний startup, raw coverage/AA, ввід/відео, весь World FX, концепт-арт і 120 FPS ще не прийняті.

**Історична межа обсягу 27.09:** scaffold/deferred рішення нижче не скасовує нову пряму вимогу [спочатку повний двигун](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md). Гра й генератор чекають E01–E17. Нижчі work packages і опис старого runtime лишаються специфікацією/історією, а не поточним доказом готовності.

**Статус:** технічна специфікація для `0.26.1-dev.06…dev.09`; усе, позначене **PROPOSED**, ще треба реалізувати й виміряти.  
**Мета:** довести воду, віддзеркалення, серпанок, погоду й сліди до одного переконливого 2.5D-зображення, не підміняючи симуляцію декоративними ефектами.  
**Пов'язані документи:** [план наступних оновлень](NEXT_UPDATES_DETAILED_UA.md), [контракт візуальної перевірки](AGENT_VISUAL_CONTRACT_UA.md), [ядро рендера](RENDER_CORE_SPEC_UA.md).

Цей документ не обіцяє фізичну воду, повноцінне 3D-освітлення або деформацію снігу. Ціль — контрольована 2.5D-система, яка витримує рух камери, zoom, паузу, 2×, reconnect і Fog of War. Якщо агент намагається закрити цю фазу новими спрайтами поверх старої плоскої води, повноекранною сірою пеленою або випадковим снігом поза diagnostic fixture чи майбутнім авторитетним `WeatherView`, роботу не приймати.

## 1. Перевірений стан і межа змін

### 1.1. Що є зараз

- `src/client/view.cpp` проєктує світ у фіксовану ізометрію: ширина клітини `58 × zoom`, висота `29 × zoom`; висота рельєфу віднімається від екранного `y` у логічних пікселях.
- Вода в presentation snapshot має єдину висоту `z = 0`. Чинний ефект — до 96 анімованих `fx.4`/`fx.5`-оверлеїв і до 12 берегових `fx.2`; це блиск і піна, а не водний матеріал.
- `render::Frame2D` має проходи `Terrain`, `WorldOrdered`, `Atmosphere`, `Fog`, `Ui`. GPU-шлях використовує один texture × vertex-color fragment shader; surface metadata, material-specific water pipeline і reflection target відсутні.
- `UnitMotion` інтерполює показану позицію мешканця з авторитетних samples. `UnitAnimation` має вісім напрямків і чотири кадри ходи; кісток, стоп та IK немає.
- Зимові варіанти тайлів/будівель є сезонним оформленням. Вони не доводять поточні опади. `WeatherView` і `snow_cover_permille` зараз існують лише у погодній специфікації; у runtime header/snapshot їх ще немає.
- Fog of War визначає розкриття клітин. Атмосферний серпанок не має права відкривати приховане.

Ці твердження — **CURRENT / VERIFIED IN SOURCE**. Усі нові структури, шейдери, caps і шляхи нижче — **PROPOSED**, доки не з'являться код, тести та native evidence.

### 1.2. Жорстка межа 0.26.1

Робимо:

- один рівень води на Surface;
- world-anchored UV, normal map, берегову піну й контрольований блиск;
- обмежені спрайтові віддзеркалення для видимих об'єктів;
- локальні смуги серпанку, дощ, сніг і пориви: у dev.06–09 від явно позначеного diagnostic input, після dev.14/18 — лише за авторитетним `WeatherView`;
- косметичні L/R-сліди від фактично показаного руху;
- quality presets, fault diagnostics, culling і native video checks.

Не робимо:

- SSR, planar capture усього кадру, ray tracing;
- симуляцію хвиль, течії, рівня або замерзання води;
- водоспади, каскади чи кілька висот води;
- мокру фізику, ковзання, collision/path зміни;
- деформацію terrain mesh або збереження слідів у save;
- ручні дороги, дорожні колії чи сліди возів;
- фортеці, армії, облоги, паливо або утеплення лише тому, що вони намальовані в концепті.

## 2. Системи координат і час

### 2.1. Координати

У контрактах нижче використовуються чотири простори:

| Простір | Одиниця | Призначення |
|---|---|---|
| `renderXY` | tile unit, `float2` | якір матеріалу, частинки, сліди, culling; одна клітина = `1×1` |
| `renderZ` | tile-height unit, `float` | `legacyElevation / 29`; вода завжди `0` |
| `drawable_px` | фізичний drawable pixel | quad, streak, scissor, native capture; не window points і не старі logical 1440×900 |
| `uv` | нормалізовані координати текстури | albedo/normal/foam/mask samples |

Чинна проєкція, яку новий FX-код мусить викликати через один render-core helper, а не дублювати по файлах:

```text
effective_zoom = user_zoom * drawable_height / 900
tile_w = 58 * effective_zoom
tile_h = 29 * effective_zoom

screen_x = origin_x + (renderX - renderY - cam_x + cam_y) * tile_w / 2
screen_y_at_z0 = origin_y + (renderX + renderY - cam_x - cam_y) * tile_h / 2
screen_y = screen_y_at_z0 - 29 * renderZ * effective_zoom
```

`origin_x/origin_y` належать drawable camera/viewport state рендерного ядра. World FX отримує готовий `RenderToDrawable`, а не читає camera globals із `GameView`. Camera origin віднімається на CPU до float32 upload. Усі radius, prism height, reflection proximity, lateral footprint offset і culling distance задаються в render tile units. Змішувати `legacyElevation`, drawable pixels і `renderZ` в одному spatial test заборонено.

### 2.2. Єдиний presentation clock

**PROPOSED interface:** render core один раз на кадр передає `presentation_time_s`, `shown_simulation_tick`, `control_epoch`, `paused`, `speed`. Вода, віддзеркалення, haze, weather particles і footprints не заводять окремі wall-clock генератори.

- При pause `shown_simulation_tick` і всі погодні частинки завмирають. Камеру можна рухати: перераховується лише проєкція.
- При 2× час FX іде вдвічі швидше разом із показаною симуляцією; кількість активних частинок не подвоюється.
- Якщо tick пішов назад, стрибнув більше ніж на 2 секунди симуляції, змінився `control_epoch`, session/layer або клієнт входить після reconnect, transient pools haze/particles/footprints очищаються. Не програвати пропущену бурю «в прискоренні».
- Глобальна фаза води є pure-функцією `shown_simulation_tick` і versioned material constants. Teleport/reset локального emitter не перезапускає воду; reconnect просто семплує фазу нового показаного tick без накопичення backlog.
- Save/load і reconnect можуть скинути haze, particles та footprints. Це дозволена косметична різниця і не може потрапляти в save, checksum, snapshot або replay authority.

## 3. Сумісний render-core контракт

Назви цього розділу узгоджені з `RENDER_CORE_SPEC_UA.md` як межа між world data та renderer, але залишаються **PROPOSED** до появи header-ів та implementation.

```cpp
struct SurfaceInstance {
    MaterialId material_id;
    std::uint32_t receiver_flags; // Water, Snow, Wetness, Footprint, Reflection
    std::uint8_t wetness_u8;
    std::uint8_t reveal_u8;
};

struct Occluder2D {
    WorldPoint world_anchor;
    AuthoredPrismShape shape; // OBB або capsule; CPU converts to bounded convex vertices
    float base_render_z;
    float top_render_z;
    ShadowClass shadow_class;
    std::uint8_t reveal_u8;
};

struct WeatherFxFrame {
    double presentation_time_s;
    std::uint64_t epoch;
    float rain01;
    float snow01;
    float haze01; // artistic atmosphere, never secrecy FoW
    Vec2 wind_world;
    float temperature_c;
    float accumulation01;
    RenderQuality quality;
};
```

`Occluder2D` не відкриває довільний polygon API для GPU. Початковий authored polygon може лишитися CPU validation/debug source, але frame preparation апроксимує його валідованою OBB або capsule prism у `renderXY/renderZ`. Невалідна, самоперетинна або надмірна форма відхиляється loader-ом, а не обрізається шейдером навмання.

`WeatherFxFrame` є presentation projection, а не новим джерелом правди. Два дозволені джерела існують у різні dev-фази:

1. **dev.06–09 diagnostic only:** versioned `DiagnosticWeatherFxInput` усередині `visual_slice_v2`, значення якого записані в capture manifest. Він не доступний звичайному world, save, network або HUD і не називається прогнозом.
2. **після dev.14, shipping integration у dev.18:** майбутній авторитетний `WeatherView` із host snapshot через `ClientSession`. На перевіреному зараз source цього runtime type ще немає; назви полів нижче походять із **PROPOSED** weather spec, а не з реалізованого API.

Фінальний adapter будує frame з `WeatherView.current`, `snow_cover_permille` і єдиного interpolated clock:

```text
rain01 = precipitation == Rain ? precipitation_permille / 1000 : 0
snow01 = precipitation == Snow ? precipitation_permille / 1000 : 0
haze01 = kind == Storm ? clamp(0.20 + 0.55 * precipitation01, 0, 0.75)
         : kind == Cloudy ? 0.12 : 0
temperature_c = temperature_deci_c / 10
accumulation01 = snow_cover_permille / 1000
```

`wind_world` отримується з восьми запланованих `WindDirection` і масштабується `wind_permille / 1000`. Один global climate означає однакові числа для всіх гравців. Quality preset може змінювати тільки щільність і caps. Звичайна гра не має права fallback-нути з відсутнього/unknown `WeatherView` на client RNG: precipitation дорівнює нулю, показується diagnostic status, а delivery gate лишається failed.

### 3.1. Майбутній render graph
Канонічний graph має шість passes і вісім attachments, визначених у Core: Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite → UI. GroundGBuffer містить albedo/world/material/flags; GroundLighting читає їх і reflection, пише scene/meta; WorldForward пише scene/meta, не читаючи meta; Composite робить output/FoW. Haze застосовується один раз до alpha blending у GroundLighting/WorldForward.

Water/reflection composite виконується у GroundLighting лише для water receivers. Bridge/bank мають власні flags, тому відбиття там не з’являється. Footprints у GroundGBuffer до WorldForward; не змінюють position/reveal/receiver flags. Metadata packing лише з Core: surface_flags B = shore для Water або snow cover для суші; world_meta = depth/reveal, не water coverage.

Старий half LocalLight target прибраний: повне освітлення використовує sharp material/normal. Reflection лишається half-resolution; жодного окремого water-mask attachment. Усі shader reads/writes відокремлені.

## 4. Водний матеріал

### 4.1. Геометрія і receiver mask

- Кожна розкрита Surface-клітина `Terrain::Water` у Ground pass пише water bit у `surface_flags` на `renderZ=0`. Нерозкрита не подається до effect preparation.
- Сусідні water cells можуть батчитися у chunk mesh, але межа authored water/shore data збігається з receiver triangles, без прямокутних швів.
- GroundGBuffer спершу фіксує water/bank/bridge receivers. GroundLighting семплує reflection тільки для water; bank/deck flags відкидають відбиття. Під мостом вода видима лише у відкритих отворах.
- Terrain elevation, picking, pathfinding і collision не змінюються. Vertex displacement води дорівнює нулю.

### 4.2. Стабільні UV і візуальна течія

UV обчислюються з `renderXY`, тому малюнок не «приклеюється» до екрана:

```text
uv_base = renderXY * 0.18
t = shown_simulation_tick / 20.0
flow = valley_flow_world * (0.018 + 0.022 * wind01)
uv0 = uv_base + flow * t
uv1 = rotate_37deg(uv_base * 1.73) - flow * 0.61 * t
```

`valley_flow_world` для 0.26.1 — нормалізований presentation vector з versioned map/generator visual metadata. Якщо metadata ще немає, використовується один явно заданий vector demo profile; не виводити напрямок із порядку контейнера, frame RNG або pathfinding. Це декоративна течія, вона не переносить об'єкти.

Water fragment shader (**PROPOSED**) читає:

- базову water albedo/gradient texture;
- tileable two-channel normal map двічі (`uv0`, `uv1`);
- authored water/shore atlas data та material flags;
- frame constants: `t`, `wind01`, tint, fixed camera light direction.

```text
n0 = unpack_normal(sample(normal, uv0))
n1 = unpack_normal(sample(normal, uv1))
n  = normalize(float3(n0.xy + 0.55*n1.xy, 1.35))
fresnel = pow(1 - saturate(n.z), 3)
sparkle = pow(saturate(dot(n, half_vector)), 32)
color = lerp(deep_tint, shallow_tint, shore01)
color += sparkle * (0.04 + 0.10*wind01)
alpha = 1
```

Це fake 2.5D lighting. Немає scene refraction, depth reconstruction або SSR. Амплітуда normal distortion мусить бути малою: берег не повинен «плисти» більше ніж приблизно десята частина висоти тайла на 100% zoom.

### 4.3. Берегова піна

`shore01` будується детерміновано з чотирьох orthogonal water/non-water сусідів і, за потреби, чотирьох diagonal для згладження. Піна з'являється лише на water-side смузі:

```text
foam = smoothstep(0.62, 0.92, shore01 + normal_noise*0.12)
foam *= 0.35 + 0.35*wind01 + 0.30*max(rain01, snow01)
foam *= reveal01
```

Вона не проходить поверх bridge deck, не малюється в unknown і не перетворює кожну внутрішню межу chunk на берег. Старі `fx.4/fx.5` можуть бути тимчасовим fallback, але не мають дублюватися поверх нового material у прийнятому кадрі.

## 5. Обмежені спрайтові віддзеркалення

### 5.1. Що відбивається

У dev.06 кандидатами є видимі будівлі, дерева, bridge supports і великі статичні props із `Reflectable2D`. На Medium/High можна додати мешканців та тварин, якщо caps витримані. Частинки, UI, Fog of War, selection rings, progress bars, smoke і самі reflections не відбиваються.

Кандидат приймається лише якщо:

- `reveal_u8 > 0`, Surface layer, sprite/atlas handle валідний;
- його ground anchor не далі ніж 2.5 `renderXY` tile units від видимої water receiver;
- його projected bounds перетинають drawable viewport + 32 reference px, масштабовані через `drawable_height/900`;
- base/top height не перевищують затверджений 2.5D діапазон; величезна декорація не може залити озеро.

### 5.2. Обмежене віддзеркалення billboard

Для кожного джерела беремо `A = world_anchor` у `renderXY`, `H = max(0, top_render_z - base_render_z)`, `B = project(A.x, A.y, renderZ=0)`. `B.y` — локальна лінія води для цього спрайта. Кожну вершину source quad у drawable space відбиваємо:

```text
x_ref = x_source
y_ref = 2 * B.y - y_source
```

Це наближене billboard віддзеркалення, не точна проєкція 3D форми. Використовується та сама atlas region, вертикально перевернута shader flag/UV; dynamic depth map сама не дає правильного forward warp одним quad. Quad далі зсувається вниз на `0.06 * projected_height`, щоб основа не лишала світлого шва. Alpha:

```text
distance01 = saturate(abs(y_ref - B.y) / max(1, projected_height))
alpha = source_alpha * reveal01 * (1 - distance01)^1.7 * reflection_strength
reflection_strength = clamp(0.16 + 0.18*(1-wind01), 0.12, 0.34)
```

Water normals зсувають reflection sample у межах `±1.5` reference px при `drawable_height=900`, масштабованих разом із viewport; більший зсув робить віддзеркалення желеподібним. Це subdraw у `Reflection` pass до Ground/World. Він не є screen capture і не має права відбити вже готовий framebuffer. Reflection target може містити pixels поза майбутньою водою; вони ніколи не потрапляють у `scene_linear`, бо лише GroundLighting water branch семплує його за water/shore inputs.

### 5.3. Clipping, banks, bridges, Fog of War

Reflection pixel потрапляє у `scene_linear` лише за authored water receiver, reveal, reflection bit і nonzero antialiased coverage у **carrier явно заданого full-engine graph version**. Старий roadmap припускав coverage уSurfaceWorld.W; для Contact V1 це заборонено: W — discrete0/1/2, point/opaque, не coverage. [Contact GPU partial proof](CONTACT_GPU_CHECK_2026_10_07_UA.md) уже реалізований, але **Reflection/Water не підтримує**. Full-engine coverage carrier лишаєтьсяOPEN; доReflection потрібні explicitABI/memory/resize amendment іnative edge proof. ВимогаReflection не скасована. Ground bank/bridge deck перезаписує water output; supports/foreground sprites — пізніше. Local OBB/capsule prisms не замінюють authored берег/receiver geometry.

Нульова терпимість до витоку: жодного reflected pixel у unknown, на суші, поверх deck або поверх foreground bank. Якщо точний clip не працює, reflections вимикаються з diagnostic fault; це не release pass.

### 5.4. Cache і culling

`ReflectionCandidateCache` ключується stable object id + atlas generation + terrain/surface/reveal revision. Статичний кандидат перевіряє proximity/bounds не рідше ніж раз на 8 rendered frames і негайно при camera, zoom, reveal, layer або terrain revision. Dynamic candidate — кожен frame. Cache очищається на epoch/session reset; він не серіалізується. Перевищення cap відкидає найдальші від центра viewport кандидати стабільним tie-break за id.

## 6. Серпанок і глибина сцени

Серпанок — локальні world-anchored шари, а не сірий fullscreen rectangle.

### 6.1. Depth bands

World depth визначається `d = renderX + renderY`; `screen_y_at_z0` є його проєкцією. Видимий діапазон ділиться на `haze_bands`. Кожна смуга має 2–4 детерміновані soft quads із render-space anchors і low-contrast noise. Anchor seed:

```text
hash(world_seed, band_index, floor(shown_simulation_tick / 1200), fx_algorithm_version)
```

Положення не перекидається кожен кадр. Quads дрейфують уздовж `wind_world` на 0.02–0.06 cell/s і wrap у межах своєї смуги. Типова прозорість 0.03–0.12; у Storm дозволено до 0.20, але чорні/сірі непрозорі штори заборонені.

### 6.2. Відмінність від secrecy fog

- Haze може зменшити локальний контраст, але силует розкритої будівлі лишається читабельним.
- Haze не створюється для unrevealed anchors і множиться на receiver/source `reveal01`.
- Fog of War малюється після atmosphere й остаточно маскує unknown.
- Жодна haze texture, reflection або particle не бере колір/форму з прихованого тайла.

`HazeBandCache` живе 60 presentation ticks після виходу смуги за expanded viewport, щоб camera pan не викликав миготіння. Epoch/layer/algorithm reset очищає cache. TTL не впливає на simulation.

## 7. Дощ, сніг і вітер

### 7.1. Diagnostic input і майбутній авторитетний input

У dev.06–09 рендер отримує лише `DiagnosticWeatherFxInput` контрольної сцени з тими самими нормалізованими полями `WeatherFxFrame`. Це дозволяє перевірити shader/pool/pause/quality до появи погоди, але не доводить simulation, forecast, save, network або reconnect authority.

Після dev.14/18 рендер отримує запланований `WeatherView`, де:

- `precipitation_permille` і `wind_permille` — цілі `0…1000`, не м/с;
- `temperature_deci_c` — десяті °C;
- `PrecipitationKind` визначає Rain/Snow; renderer не вгадує вид опадів за сезоном;
- `snow_cover_permille` — глобальний покрив долини, а не кількість particle sprites;
- `evaluated_simulation_tick` задає, якому authoritative tick відповідає sample.

Позитивна температура + Rain ніколи не показує сніг. Winter material без current Snow не породжує snowfall. Forecast не запускає ефект до фактичної boundary. У shipping build diagnostic provider не реєструється і не може підмінити unknown network weather.

### 7.2. Напрямок вітру

Вісім напрямків мапляться в unit world vector. Для drawable screen:

```text
wind_screen = normalize(project_vector(wind_world, z=0))
wind01 = wind_permille / 1000
```

Rain streak має горизонтальний drift `80 * wind_screen.x * wind01` reference px/s та вертикальну швидкість 520–760 reference px/s. Snow flake: 55–120 reference px/s вниз, 18–65 reference px/s lateral drift, sin wobble зі stable slot phase. Усі reference pixels задані для drawable height 900 і множаться на `drawable_height/900`. Числа **PROPOSED**, налаштовуються native video, але одиниці й залежність фіксовані.

### 7.3. Pool, spawn і TTL

Particle slot seed = hash(`control_epoch`, weather period start, slot id, algorithm version). Це presentation RNG; він не входить у simulation hash.

```text
target_rain = round(cap_rain * rain01)
target_snow = round(cap_snow * snow01)
spawn_budget_per_frame = min(missing, ceil(cap * 0.08))
```

- Rain TTL: 0.45–0.85 s; kill нижче viewport + 32 px.
- Snow TTL: 3–7 s; kill нижче ground receiver або viewport + 48 px.
- Wind gust pixel/ribbon TTL: 0.8–1.8 s; вони дозволені лише при `wind01 >= 0.55` і не заміняють Rain/Snow.
- Spawn area — expanded drawable viewport. Rain є screen-oriented, snow має world anchor для parallax; обидва проходять reveal filtering до GPU upload і receiver clip у production graph.
- Камера не телепортує старий snow: world-anchored flakes повторно проєктуються. Screen rain можна пересіяти після camera jump > 1 tile.
- Pause не старить TTL. 2× старить TTL і рух удвічі швидше. Reconnect reset не replay-ить backlog.

Particle draw не змінює accumulation. У dev.06–09 cover/melt є полем diagnostic fixture; після dev.18 вони приходять лише з authoritative simulation `snow_cover_permille`.

## 8. Сліди на снігу

### 8.1. Джерело руху

Слід випускає `FootprintEmitter` із фактично показаної `resident_position()` / `UnitMotion::sample(animation_tick)`, а не з майбутнього path, packet arrival або tile command. Чотири кадри ходи не дають точного контакту стопи, тому footfall є чесною 2.5D-апроксимацією за відстанню.

```cpp
struct FootprintEmitter { // PROPOSED, cosmetic only
    WorldPoint previous_displayed;
    float accumulated_distance;
    FootSide next_side; // Left / Right
    std::uint64_t epoch;
    bool initialized;
};
```

На кожному кадрі:

1. Обчислити `delta` між попередньою і поточною displayed `renderXY` position.
2. Якщо layer змінився, distance > discontinuity bound або clock reset — скинути emitter без stamp.
3. Додати Euclidean distance у render tile units. Кожні `stride = 0.34` для дорослого (`0.28` для young visual scale) інтерполювати точку всередині поточного segment.
4. Нормалізувати напрямок `dir`; lateral `perp = (-dir.y, dir.x)`. Stamp anchor = point `± 0.085 cell * perp`, почергово L/R.
5. Orientation = `atan2(dir.y, dir.x)` або найближчий з восьми atlas directions. Не брати випадковий кут.

Якщо один кадр містить велику, але допустиму інтерпольовану відстань, цикл може поставити кілька stamp із max 4 на emitter/frame. Більше означає discontinuity і reset, щоб телепорт не намалював пряму доріжку.

### 8.2. Receiver eligibility

Stamp створюється лише коли:

```text
Surface layer
AND receiver_flags contains Snow & Footprint
AND accumulation01 >= 0.20
AND revealed
AND not Water
AND not bridge deck / building footprint / exposed rock
```

Stamp використовує наявний `ground_height()` тільки для проєкції. Він не змінює height field, mesh, collision, path cost або movement bonus. Ручної Road немає; road receiver/колії не додавати.

### 8.3. Життя, засипання і танення

`FootprintStamp` зберігає anchor, side, orientation, birth shown tick, cover at birth і opacity seed у локальному ring buffer.

```text
age_s = (shown_tick - birth_tick) / 20
age_fade = 1 - smoothstep(35, 90, age_s)
burial = saturate((current_cover - cover_at_birth) / 0.18)
melt = current_cover < cover_at_birth ?
       saturate((cover_at_birth - current_cover) / 0.22) : 0
opacity = base * age_fade * (1 - 0.85*burial) * (1 - melt) * reveal01
```

Пороги **PROPOSED** і перевіряються у dev.08. Rain при температурі >0 не видаляє stamps окремою локальною формулою: fixture/authoritative adapter подає нове accumulation, а renderer реагує на його зменшення. Після dev.18 це значення походить лише з host cover. Stamp з opacity < 0.02 або age > 90 s видаляється. Camera culling не видаляє його до TTL, але ring cap може витіснити найстаріший.

Footprints не серіалізуються, не реплікуються й не хешуються. Після load/reconnect вони можуть зникнути. UI або release notes не мають називати їх збереженим станом снігу.

## 9. Quality presets і budgets

Це **PROPOSED, UNMEASURED** caps. Low перевіряється на 1280×720, Medium — на 1920×1080; High наведено як окрему початкову пропозицію. Вони є вихідною точкою dev.06–09, а не твердженням про FPS. Caps масштабують кількість ефектів, але не `WeatherFxFrame`, reveal, water geometry чи snow cover.

| Ресурс | Low | Medium | High |
|---|---:|---:|---:|
| reflection target scale | 0.50 | 0.50 | 0.50 |
| local lights | 24 | 48 | 72 |
| global shadow occluders | 128 | 256 | 384 |
| occluders per accepted light | 16 | 32 | 48 |
| reflection casters | 8 | 16 | 32 |
| haze bands | 3 | 6 | 8 |
| haze quads | 24 | 48 | 72 |
| weather particles разом | 1000 | 2000 | 3000 |
| footprint ring capacity | 2048 | 4096 | 6144 |

Medium — архітектурний baseline. Rain і Snow взаємовиключні за одним authoritative/diagnostic sample, тому ділять спільний weather particle pool. Resolution scaling для particle caps дозволений лише після замірів; hidden multiplier за FPS заборонений, бо робить capture неповторюваним. Stable culling order: visibility → expanded viewport → distance to viewport center → stable id/slot.

Global occluder cap не дозволяє мовчки викинути blocker, який реально впливає на вже прийняте світло. Підготовка спочатку відсікає lights за reveal/viewport, потім збирає всі їхні blockers. Якщо accepted light потребує понад per-light cap або сукупність перевищує global cap, counter/diagnostic є обов'язковим, а контрольна сцена й release acceptance провалені. Агент має зменшити overlap, уточнити authored prisms або обґрунтовано змінити budget і повторити proof; кадр із протіканням світла не стає pass через stable cull.

Обов'язкові telemetry counters у debug overlay/log: submitted/drawn/culled water chunks, reflection casters, haze quads, rain, snow, footprints, lights і occluders; усі вісім attachment dimensions/formats; active quality preset; overflow/reset reason. Telemetry не показується гравцю у release UI.

## 10. Fault policy

| Відмова | Дозволений runtime fallback | Статус delivery |
|---|---|---|
| water shader/normal missing | flat/current water + diagnostic | **FAIL**, не приймати dev.06 |
| `surface_flags`/shore metadata invalid | reflections off + diagnostic | **FAIL**, не приймати dev.06 |
| reflection clip leak | reflections off | **FAIL**, поки нема native proof |
| haze asset missing | haze off | **FAIL** dev.07 |
| weather particle asset/pipeline missing | particles off, diagnostic/authoritative frame лишається доступним для debug | **FAIL** dev.07 |
| footprint atlas/pool failure | footprints off | **FAIL** dev.08 |
| non-blocking particle/reflection/decal overflow | stable cull до cap, counter increments | дозволено лише поза обов'язковою контрольної сценою й без втрати rubric |
| blocker/required-category overflow | diagnostic; кадр може продовжити без crash | **FAIL**, silent drop не приймається |

Fallback потрібен, щоб гра не падала. Він не є способом видати фічу за готову. Silent disable заборонений. Software test backend може перевірити command generation, але не доводить D3D12 shader result.

## 11. Файли й межі володіння

### 11.1. Наявні точки (**CURRENT**)

- `src/client/view.cpp` — projection, terrain/water overlays, displayed residents, atmosphere;
- `include/ribterra/render_frame.hpp`, `include/ribterra/renderer2d.hpp`, `src/render/renderer2d.cpp` — чинний Frame2D і D3D12/software paths;
- `shaders/sprite.vert.hlsl`, `shaders/sprite.frag.hlsl` — чинний texture × color;
- `include/ribterra/unit_motion.hpp`, `include/ribterra/unit_animation.hpp` — displayed movement і 4-frame walk;
- `include/ribterra/weather.hpp` — **PROPOSED weather plan path**, авторитетні типи мають з'явитися через weather task, не через FX shortcut.

### 11.2. Рекомендоване розділення (**PROPOSED**, узгодити з render core до створення)

- `include/ribterra/world_fx.hpp` — POD frame inputs, quality, counters, reset reasons;
- `src/client/world_fx.cpp` — diagnostic/future-WeatherView adapters, pools/caches/culling, без simulation mutations;
- `include/ribterra/render_materials.hpp` — `SurfaceInstance`, receiver flags і material IDs, якщо саме цей path затвердить render core;
- `src/render/world_fx_renderer.cpp` — water/reflection/haze/particle GPU passes;
- `shaders/water.vert.hlsl`, `shaders/water.frag.hlsl`, `shaders/reflection.frag.hlsl`, `shaders/world_fx.frag.hlsl`;
- `assets/art/fx/water_normal.*`, `water_foam.*`, `rain.*`, `snow.*`, `footprint_left.*`, `footprint_right.*`;
- `tests/world_fx_tests.cpp` — pure projection/pool/reset/culling contracts;
- `tools/world_fx_visual_probe.*` — за потреби isolated probe, але він не замінює native game capture.

Не редагувати generated shader binaries вручну; build/package має компілювати/копіювати їх тим самим шляхом, що sprite shaders. Канонічні назви проходів і attachments належать `RENDER_CORE_SPEC_UA.md`. World FX використовує їх без aliases і не створює паралельний renderer.

## 12. Порядок реалізації dev.06–dev.09

### `0.26.1-dev.06` — water proof

Видима зміна: world-anchored water normals, shoreline foam, bounded reflections, правильні banks/bridge/FoW clips. Стабільними лишаються terrain height, picking, water level, paths і gameplay.

Потрібні докази:

- native D3D12 відео 20–30 s: повільний pan + zoom над берегом і мостом;
- друге відео з рухомим reflectable unit на Medium/High;
- кадри same seed/view при pause і після resume;
- automated mask/projection/culling tests та counters без cap overflow у контрольній сцені.

### `0.26.1-dev.07` — haze і diagnostic weather prototype

Видима зміна: depth-banded haze, Rain/Snow/Wind з versioned `DiagnosticWeatherFxInput` лише у `visual_slice_v2`; Storm не стає сірою завісою. Стабільними лишаються reveal і water proof. Forecast, work modifier та runtime weather тут ще не заявляються; host-authoritative wiring належить dev.13–14/18.

Потрібні докази:

- native відео Clear → Rain, Snow/Storm, pause → resume, 1× → 2×;
- diagnostic epoch reset, еквівалентний майбутньому reconnect: без backlog burst і без зміни виду опадів;
- positive-temperature Rain case без жодної snow particle;
- той самий diagnostic frame на двох клієнтах: однакові нормалізовані weather values, дозволена різна particle density preset; це не network-authority proof.

### `0.26.1-dev.08` — displayed-motion footprints

Видима зміна: чергування L/R, правильний напрям, fade/burial/melt на snow receivers. Стабільними лишаються movement, speed, collision і save schema. До dev.14 cover у fixture є diagnostic input; після dev.18 та сама формула читає authoritative cover.

Потрібні докази:

- native відео одного мешканця по снігу з поворотом на 90°;
- перехід snow → rock/bridge/water edge без stamps на забороненій surface;
- pause/2×, teleport/discontinuity і load/reconnect reset;
- programmed tests stride interpolation, max 4 stamps/frame, L/R, cap eviction, receiver mask.

### `0.26.1-dev.09` — combined slice

Видима зміна: вода, reflections, haze, diagnostic weather і footprints працюють одночасно у контрольній сцені до масового виробництва art assets. Стабільними лишаються UI readability, Fog of War і bridge occlusion. Це G1 render proof, не доказ майбутнього authoritative weather state.

Потрібні докази:

- 60–90 s native video same seed/view на Low/Medium/High;
- GPU timing/counters і frame-time capture; конкретна test machine/driver/resolution;
- storm біля води з bridge, будівлями, resident movement і camera pan;
- failure-injection runs для missing shader/asset із явним diagnostic;
- side-by-side із попереднім native кадром. Цільовий AI-макет можна показати лише як reference, не як proof.

## 13. Перевірки й acceptance IDs

### 13.1. Автоматизовані контракти

| ID | Перевірка |
|---|---|
| `WFX-WAT-01` | camera pan/zoom не змінює world UV фази в тій самій world point |
| `WFX-WAT-02` | GroundLighting water branch/`surface_flags` не лишають reflection на land, unknown, bridge deck |
| `WFX-REF-01` | quad mirror виконує `y'=2B.y-y`, `x'=x` для fixed iso camera |
| `WFX-REF-02` | stable culling дає той самий список при тому самому frame input |
| `WFX-REF-03` | source або receiver reveal 0 дає нуль reflected pixels/commands |
| `WFX-HAZ-01` | haze anchors стабільні між frames і reset при epoch |
| `WFX-HAZ-02` | unknown tiles не породжують quads; secrecy fog лишається останнім |
| `WFX-WEA-01` | dev.07 diagnostic input, а після dev.18 `WeatherView`, точно проєктуються в ті самі `WeatherFxFrame` units |
| `WFX-WEA-02` | pause не рухає/не старить particles; 2× масштабує presentation delta |
| `WFX-WEA-03` | tick rollback/jump/reconnect очищає pool без backlog spawn |
| `WFX-FPT-01` | stamps виникають за displayed distance, чергують L/R і мають correct lateral sign |
| `WFX-FPT-02` | receiver/reveal/cover gates блокують water, rock, deck, unknown |
| `WFX-FPT-03` | footprint pool не входить у save/network/checksum і reset без mutation world |
| `WFX-BUD-01` | кожен preset дотримується hard caps і stable eviction |

Pixel tests допустимі для masks і projection probe. Вони не заміняють native відео, бо не показують temporal shimmer, particle cadence, camera anchoring і bridge occlusion у русі.

### 13.2. Візуальна рубрика

Прийнято лише якщо спостережувано виконано все:

- вода читається як одна поверхня, а не набір однакових наклейок; UV не їдуть за камерою;
- берегова піна й reflection clip не заходять на сушу/міст/unknown навіть на zoom extremes;
- reflected sprite залишається слабшим за source і розпадається вниз, без дзеркала UI/туману;
- haze додає глибину між планами, але не закриває завдання, selection і HUD;
- Rain/Snow відповідають зафіксованому diagnostic frame у dev.07–09, а після dev.18 — current authoritative state; pause/2×/epoch reset виглядають логічно;
- сліди мають зрозумілу L/R ходу, не утворюють лінію при teleport і зникають через покрив/вік;
- Medium baseline не перевищує свої caps; будь-який cull видимий у counter, а не прихований;
- жодна з цих фіч не змінює terrain, рух, path, cover simulation, save або network authority.

## 14. Stop conditions

Фазу не можна назвати завершеною, якщо:

- є лише згенерований target або isolated shader probe без native game video;
- water/reflection працюють тільки в одному статичному кадрі;
- weather у dev.06–09 береться не з versioned diagnostic fixture, а після dev.18 — не з host `WeatherView`;
- gray overlay видається за haze;
- footprint stamps ставляться за command path, а не displayed motion;
- fallback тихо вимикає required effect;
- масове виробництво нових будівель/персонажів почалося до combined dev.09 proof;
- performance numbers не містять machine, backend, resolution, preset і capture method.

Після dev.09 дозволено масштабувати бібліотеку art assets. До цього моменту головний ризик не «бракує красивих спрайтів», а те, що вода, атмосфера, маски й час не утворюють стабільну систему.
