# Environment V1 — обмежена нативна перевірка

08.10.2026. **BOUNDED NATIVE PASS; E06 і E01–E17 OPEN.**
[Контракт](ENVIRONMENT_V1_CONTRACT_UA.md). Діагностичний спільний вхід
hour/cloud/haze керує Sun, Moon, ambient і вже перевіреною atmosphere у
справжньому GameView/ClientSession. Це не normal startup, authoritative
календар/погода, художнє приймання або доказ ≥120 FPS.

## Збірка та повтор

- Fresh strict C++20 `/W4 /WX` build
  `build/gameview-environment-2026-10-08-attempt02`: actual exit 0;
  22 TUs, 152 source bindings, 120 команд із zero exit sidecars.
  23 shader programs / 46 stages / 51-source closure / 93 artifacts.
  Root: 522 файли, 116 549 751 bytes (111.15 MiB).
- Незалежний read-only аудит звірив усі 152 current/snapshot bindings,
  command exits і 93 shader artifacts. Усі 93 shader artifacts byte-identical
  до atmosphere build03; SurfaceLighting384 і Moon32 ABI не змінено.
- Два запуски того самого EXE: native `attempt01` і `attempt02`;
  actual runner exit 0 і actual subprocess exit 0 в кожному.
  D3D12 / NVIDIA GeForce RTX 3050 Laptop GPU / driver `32.0.15.9282`,
  1920×1080. Обидва root ledgers перевірили незмінність п'яти bound inputs.
- Кожний native root: 47 файлів; 42 final GPU PNG, raw WaterCoverage,
  JSON результату, поточний test save, execution log і ledger.
  Root sizes 16 950 501 / 16 950 502 bytes. Runtime повторно не копіювався.
- **44/44 derived outputs byte-exact**: 42 PNG + raw carrier + JSON.
  Test save містить secure world ID; часові записи execution ledger також
  відрізняються. Їх не видавати за byte-exact повтор.
- Незалежний native аудит повторно декодував усі causal pixel/shadow/horizon
  metrics, raw tuples і весь 80×40 UI quad; повторно звірив output closures
  обох запусків і current 152-source cohort. Architectural status **CLEAR**
  тільки для цього bounded diagnostic checkpoint.

Історична назва EXE `GameViewWaterProbe.exe` не визначає тест: ізольований
builder отримав `-ProbeSource tools/gameview_environment_probe.cpp`.
Build report execution=false — це правильно; native виконання окремо
фіксують unsigned ledgers з actual argv/часом/exit/input/output hashes.
Вони не є самозасвідченим GPU timer або вимірюванням показаного FPS.

## Причинні перевірки

| Перевірка | Результат |
|---|---|
| Однаковий paused input / resolved manual equivalent | PNG exact до noon baseline |
| Hour0/6/9/12/15/18/21/3 | GPU captures; normalized directions і незмінні emission/local metadata |
| CPU 96 quarter-hour samples + wrap nextafter(24,0)→0 | Source exclusivity; continuity tolerance 1e-5, не byte-exact wrap |
| Cloud0→1, same noon/haze | Sun і ambient слабшають; 375 018 changed pixels |
| Haze0→1, same hour/cloud | Lighting384/Moon32 exact; 363 808 changed pixels через atmosphere |
| Sun9/15, caster-on/off у кожній годині | 8 452 / 8 896 darkened land pixels; centroid relocation 31.1242 px |
| Moon21/3, caster-on/off у кожній годині | 12 633 / 9 244 darkened land pixels; centroid relocation 19.4011 px |
| Same-hour horizon source-on/off | Hour6/18 exact; поруч позитивний sky contribution |
| Sun-off / Moon-off | 366 089 / 364 665 changed pixels у незалежному pixel audit; іншого sky source немає |
| Authored local lights off, emission unchanged | 46 379 changed pixels |
| Ambient-only on/off | 375 018 changed pixels |
| Isolated emission-only on/off | 1 001 warm pixels: ΔR>ΔG>ΔB, ΔR>3 |
| 12 invalid inputs | Rejected before published frame/picks/camera; following valid PNG exact |
| UI / alpha / raw carrier | UI exact; alpha255; carrier exact у всіх кадрах |

Horizon same-hour land SDR RGB mean deltas: hour5.9→0.65213,
6→0, 6.1→2.23082, 17.9→1.83490, 18→0, 18.1→0.30704.
Пари тримають ambient/local/emission незмінними. Це causal sky on/off,
не твердження про monotone total energy або exact shadow topology handoff.

Warm-hue fixture має black non-emission lighting і emission scale .125,
щоб SDR clamp не інвертував поканальну різницю. Не видавати цей результат
за hue measurement насиченого повного night кадру.
Це zero non-emission **world** radiance: окремий UI quad не чорний,
але exact у всіх 42 PNG.

Raw carrier: 1920×1080, 8 294 400 packed bytes, pitch7680; clear1 698 582,
land302 496, water70 657, bridge deck1 865, shore-water42 832, invalid0.
Це binary coverage, не intermediate AA. Invalid checks доводять
published frame/picks/camera і following GPU result, не всі internal caches.

## Ідентичність доказів

| Файл | SHA-256 |
|---|---|
| Build02 EXE, 42 646 528 bytes | `BA605FAAD7D06AE445A17380A9AB11FF619C7574722295FB83B18155903A22C6` |
| Build02 report, 310 354 bytes | `8B7BE788C81F4A90A9C42B035D8B16B140ECF0D2BBEA637D7D7E26AEB7A8C311` |
| Shader manifest | `213DA2A019D152134D5BD75088C3FED794BD879CE34851B825FCC16B14B0CBA8` |
| Native01/02 result | `F510E4682BBE20050B952981A150352D6D89A5C4DD692E766DD4C64B1C9CE3C4` |
| Native01/02 raw | `56BD855A4941255641A7F503FAE75C4826F0C6FEF479422548A3A209DF632310` |
| Native01 execution | `29AC43B4EEAD1E27DBBE5A7B0AFCD501D22DBE6379C2D166C0351BD465E66AA3` |
| Native02 execution | `0DDE1D0CB62520A826D76D194021ADD591C6EE9B34C68A8ED8C2734D7BB6970B` |

Visible SDL/D3D12 windows запускалися; final GPU PNG noon/night/hour18
переглянуті. Це не desktop scanout, mouse/DPI/input proof чи немонтоване
відео. Neutral technical house/terrain не збігаються з кінцевим концепт-артом.
Нових shaders/attachments, gameplay/network/save-schema, launchers або
current-build pointer не змінено. Normal integration, всі художні категорії,
рух/відео, authoritative weather, повний current CTest і ≥120 FPS залишаються
відкритими. У результаті `fps`/`gpu_ms` null; **весь двигун не прийнятий**.
174 182 400 bytes attachment telemetry — nominal target allocation,
не peak/total VRAM. Inherited water-probe/unversioned build label — механізм
ізольованої збірки, не standalone Environment або release acceptance.

Build01 мав actual exit1 (C2039: помилкові поля DeviceInfo у probe), без
native execution. Виправлено лише probe fields; ця невдала спроба не є
поточним результатом. Її compact source/shader/log evidence зберігається.
Після повідомленого exact manifest й незалежного preflight [видалено тільки
21 failed01 OBJ](../ENVIRONMENT_BUILD_COMPACTION_2026_10_08_UA.md), не поточну
збірку або native докази. Старий failed report тепер історичний без цих OBJ.
