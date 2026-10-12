# AnalyticAtmosphere V1 — просторовий туман у спільному рендері

08.10.2026. Це контракт нового `WaterAtmosphereSlice`, не приймання E10,
всього двигуна, нормального старту гри, концепт-арту чи 120 FPS.

## Заміна алгоритму, а не скорочення візуальної вимоги

Для цього графа аналітична густина **замінює** запропоновані у World FX §6
soft-quad bands / `HazeBandCache`. Залишаються обов'язковими далека імла,
ранковий низький туман, туман біля відкритої води, окремі кольори, реакція на
час/погоду, рух і масштаб камери, коректні reveal/alpha та незмінний HUD.
Старі cache/quad/asset-missing критерії не можна позначати виконаними за цим
аналітичним доказом. Нових текстур, fullscreen overlay або temporal cache немає.

Складові застосовуються до лінійного HDR RGB кожного видимого внеску один раз:

- GroundLighting: після освітлення, **до** домішування Reflection.
- Reflection: за P/N оригінального джерела, після lighting/emission,
  **до** alpha, fade та premultiply. Саме джерело, не глибина води.
- WorldForward: після lighting/emission, до alpha blend. У новому варіанті
  binary `reveal=0` відсікається до освітлення, не публікує scene/meta.
- Composite і UI не змінені. Уже затуманений Reflection не туманується знову.

## Простір і час

`view_far = -(P_camera.x + P_camera.y)`; near/far — у camera-relative tiles.
Панорамування закономірно змінює depth term. Абсолютні XY відновлюються через
camera origin **лише** для noise та відстані до води; ці складові не прив'язані
до екрана. Висота — canonical render Z, не screen Y.

`far = smoothstep(near, far, view_far)`.
`height = exp(-max(P.z-baseZ,0)*heightFalloff)`.
`morning = morningDensity*morning01*height`.
`coastal = coastalDensity*waterProximity*height`.
Відстань до води — точна зовнішня відстань до AABB; proximity плавно спадає
до нуля на shoreRadius. Шум низькоконтрастний, world-anchored, дрейфує лише за
явним shown clock і напрямом вітру. Зупинка цього clock не додає random/wall time.

Колір — зважена суміш **трьох окремих linear RGB**: far/morning/coastal.
Opacity = `min(maxOpacity,1-exp(-opticalStrength*sumWeights*noise))`, максимум
0.20; opacity множиться на reveal, змінює тільки RGB, не alpha. Туман не є
самостійною емісійною картою; нічний колір і густина потребують спільного
візуального приймання з нічним освітленням.

`EngineAtmosphereSettings` має один `time_hour` [0,24) і `weather_haze01` [0,1].
Adapter сам обчислює morning01 через smoothstep 4→6 і 6→9, daylight через
5→9 та 16→21, optical/densities/colors через weather/daylight. Не можна
незалежно подати суперечливі hour і morning. Це **діагностичні presentation
inputs**, не готові host-authoritative погода, доба, forecast чи save/network.

## Revealed-water carrier і транзакційність

GameView пропускає unknown cells і джерела. AABB формуються тільки з фактичних
revealed `Terrain::Water` у контрольному вікні, межі tile center ±0.5. Рядки
об'єднуються тільки за однаковими X-межами та безпосередньо сусіднім Y:
жоден прямокутник не перекриває land/unknown/hole. Максимум 32; потрібна 33-тя
область відхиляє candidate до публікації frame/camera/picks. Disabled atmosphere
не будує області, count=0 і tail=0, не накладає active-capacity на старий світ.

Renderer перевіряє finite/ranges, ABI version, enabled, unit/zero wind,
ordered world bounds і zero inactive tail **до GPU acquisition**. Публічний
`Frame2D` сам по собі не доводить походження областей із revealed water;
для GameView це окремо звіряється з ClientSession. Loop динамічний до actual
count; 32 — provisional carrier capacity, не виміряний performance budget.

## ABI і пам'ять

| Поле AtmosphereGpu | Offset | Зміст |
|---|---:|---|
| origin_time | 0 | absolute camera XY, shown seconds, morning01 |
| depth | 16 | camera-relative near/far, optical, maxOpacity |
| density | 32 | morning/coastal, baseZ, heightFalloff |
| far_color_noise | 48 | far linear RGB, noise amplitude |
| morning_color | 64 | morning linear RGB, reserved zero |
| coastal_color | 80 | coastal linear RGB, reserved zero |
| wind_shore | 96 | wind XY, tiles/sec, shoreRadius |
| counts_version | 112 | patchCount, ABI1, enabled0/1, zero |
| water_aabbs[32] | 128 | absolute minXY/maxXY, stride16 |

AtmosphereGpu = **640 bytes**, alignment16. Новий b0 wrapper = **1024 bytes**:
старий SurfaceLighting384 prefix + Atmosphere640; patches offset512.
Старі графи мають незмінний b0=384. b1–b3, sampler/storage/MRT slots незмінні;
не додається п'ятий UBO. Три нові програми: `atmosphere_ground_lighting`,
`atmosphere_material_forward`, `atmosphere_water_reflection`.

Залишаються дев'ять world attachments/two frame sets, **84WH bytes**, тобто
174 182 400 nominal bytes для 1080p, плюс окремі наявні sky targets. Uniform
upload: 1024 bytes на GroundLighting й кожен lit forward/reflection draw;
це не нова attachment VRAM і не вимір загальної/пікової driver allocation.

## Що має довести нативний прогін

Реальний D3D12/RTX 3050, GameView/ClientSession, old graph vs disabled PNG/raw
carrier, morning/noon/night, clear/haze, paused/drift, UI exact, unknown source
non-submission, hidden surface emission, збережені Reflection/deck/shore, pan і
zoom0.62/1.30/3.00. Окремі справді спроєктовані terrain panels із однаковими
матеріалами доводять причинні near/far, low/high і coastal-distance gates.
32-патчова сцена має виконатися; 33-патчова — зберегти попередній rendered frame.
Некоректні inputs мають відхилятися до GPU, наступний valid frame — бути точним.

PNG — final GPU output, **не raw HDR**. Raw називаються тільки завантажені
WaterCoverage bytes. Окремий hidden-surface test не тестує зникнення вже
складеного local light/caster; actual GameView unknown-source check тестує
non-submission цілого прихованого джерела. Без окремого доказу не приймати
відео/temporal smoothness, reprojection tolerances, DPI/input, combined weather,
повну нічну/погодну authority, E10/E15–E17 або концепт-faithful графіку.
