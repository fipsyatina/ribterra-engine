# Прозорість у typed HDR-шляху — 08.10.2026

**Проміжний нативний поріг пройдено:** `GameArt::lit_sprite` підтримує прозорість через наявний vertex alpha. Покриття, випромінювання та відбиття у воді окремо перевірені в Direct3D12; незалежний source та actual-evidence review — **CLEAR**. Повний рушій, погодні FX, normal startup і відповідність трьом референсам залишаються **OPEN / NOT_ACCEPTED**.

До API додано trailing `float opacity = 1.f`. Усі входи перевіряються до зміни кадру; пошук asset залишається обов'язковим навіть за нульової прозорості. Alpha округлюється до найближчого значення `round(255 * opacity) / 255`; максимальна похибка цього подання — 1/510. Нульовий квантований alpha не додає vertices, indices, materials або commands. За замовчуванням payload і PNG точно дорівнюють explicit opacity 1. RGB лишається білим; зміна не вводить tint, нові uniforms, bindings чи shader ABI. Оpaque ground/surface вимоги збережені.

Ця властивість керує лише спрайтом і його наявним reflection path. Окремі authored lights, shadow blockers та contacts мають власні контракти; загальне згасання GameView або погодна система ще не під'єднані.

## Actual execution та прив'язка файлів

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; попередній dirty source збережено.
- Fresh build `build/gameview-opacity-2026-10-08-attempt01`: C++20 `/O2 /MT /W4 /WX`, 21 production translation units і новий probe; 160 source bindings, 120 actual zero-exit commands, 23 shader programs / 46 stages. Root: 530 файлів / 118 618 058 bytes.
- Два actual subprocess runs, exit 0: `build/gameview-opacity-native-2026-10-08-attempt01` і `attempt02`. Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080, eight-pass `WaterAtmosphereSlice`.
- Кожен run: 19 PNG, 3 qualification masks, raw WaterCoverage RGBA8, JSON, один current-schema world save та два runner evidence files; 27 файлів, приблизно 20.1 MB.
- Parent verifier actual exit 0: `opacity current evidence checks passed; sources=160, bound_files=601, zero_commands=120, repeat_outputs=24`.
- Усі 24 derived outputs точно збігаються між actual runs. Єдиний іменований виняток — `opacity-world-current.save` із випадковим World ID; обидва saves окремо hash-bound до своїх executions. Wrapper timings не видаються за deterministic outputs.
- Reviewer незалежно перевірив усі 160 current/snapshot bindings, runtime origins/destinations, 25 probe outputs/run, 120 zero-exit sidecars і 24 derived comparisons. Додатково переглянуто coverage half/one, emission half і reflection half/one PNG. Mismatches: 0.

Команда перевірки поточної source cohort:

```powershell
C:\Python314\python.exe tests/gameview_opacity_evidence_tests.py build/gameview-opacity-2026-10-08-attempt01 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-opacity-native-2026-10-08-attempt01 build/gameview-opacity-native-2026-10-08-attempt02
```

Після наступних змін bound source ця cohort стає історичною. Попередні cumulative-contact і SessionShown cohorts уже історичні та збережені як окремі докази своїх версій source.

## Причинні контроли

Три групи мають absent, zero API, реально submitted zero-alpha geometry, half alpha 128, explicit one alpha 255 і default captures. Reflection-група малює тільки відбиття, без звичайного спрайта. Fog і water distortion вимкнені, exposure 1, environment явне та нерухоме. House fixture не додає окремі local emitters/casters, які могли б підмінити спрайтовий результат.

| Група | Qualified pixels | Qualified components | Strict intermediate components | Half changed від zero / one |
|---|---:|---:|---:|---:|
| Coverage | 17 344 | 49 954 | 49 954 | 17 395 / 17 402 |
| Emission | 1 067 | 3 196 | 3 196 | 1 067 / 1 067 |
| Reflection | 1 116 | 2 069 | 2 069 | 9 634 / 11 204 |

Числовий oracle використовує inverse-sRGB інтервали фінального SDR з квантованими endpoints; це **inferred linear evidence, без raw HDR readback**. Допуск FP16 явно задано: relative 3/1024, absolute 1/65536; endpoint byte delta ≥8, saturated channels виключені. В усіх трьох групах maximum interval gap 0. Підстановка zero або one замість half відхиляється; oracle невакуумний. Qualification masks для reflection додатково обмежені raw water carrier.

У кожній групі zero API та submitted zero-alpha geometry дають PNG, точно рівний absent; default PNG/frame точно рівний explicit one. Material/emission/receiver/reveal metadata незмінні, окрім alpha. Три CPU controls включають quantized zero 0.001 і alpha 128/129/255. Усі 10 API та 15 renderer invalid controls відхилені до submit зі збереженням кадру й без створення нових targets. Opaque ground alpha rejection перевірено окремо.

Known UI marker RGBA `[31,189,78,255]` і raw WaterCoverage залишаються byte-exact; невалідних carrier tuples немає. Це не перевірка RmlUi HUD. Carrier має 1 865 bridge-deck, 70 657 water і 302 496 land pixels; bridge records authored, фактичне будівництво не виконувалося.

## Межі приймання

Parent переглянув actual coverage-half і reflection-one PNG: вони показують нейтральну діагностичну сцену. Її вигляд не є прийманням референсного арту. Перевірено одну категорію house; storm/rain/snow/fire/smoke particles, погодна authority, save/wire weather, general fade, normal startup, physical input/video/DPI та два фізичні ПК потребують подальшої роботи.

FPS і GPU execution time мають `null`. 174 182 400 номінальних attachment bytes для двох frame sets не є peak VRAM або підтвердженням E17 ≥120 FPS. Native goal залишається ACTIVE; наведені результати — direct execution evidence, без автоматичної gate-check certification. Видалень у цій перевірці не було.
