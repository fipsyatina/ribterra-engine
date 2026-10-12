# Normal GameView: registered materials — 08.10.2026

**Статус:** strict Build03, native01–02, parent та незалежний supplemental read-only verifier завершилися з actual exit0; **незалежний actual evidence review CLEAR**. Bounded checkpoint прийнято для його exact frozen source snapshot. Він покриває два діагностичні registered variants у звичайному `GameView::compose`; full main startup, production neutral art, reference fidelity та весь рушій залишаються OPEN. Формальні gate checkboxes не змінено.

Авторитетне дерево: `C:\Users\fipst\Desktop\ribterra`. Leaf1.25 додає cold `load_material_collection` і спільне normal draw/query resolution; leaf1.27 перевіряє actual mixed composition/menu/HUD; leaf1.28 виправляє original registry; leaf1.33 зберігає повні SDK bindings у компактному lossless format; leaf1.34 додає read-only calibration schema after-tree labels. Кожен результат стосується конкретного source snapshot і bound inputs.

## Фактичне виконання

| Перевірка | Evidence |
|---|---|
| [Build03 report](C:/Users/fipst/Desktop/ribterra/build/gameview-material-2026-10-08-attempt03/build-report.json) | Actual exit0; 181 source bindings, 24 TUs = 23 production + probe, 122 actual argv commands із zero exits; 557 files / 134 141 097 bytes, менше128MiB |
| [Native01](C:/Users/fipst/Desktop/ribterra/build/gameview-material-native-2026-10-08-attempt01/material-report.json) / [native02](C:/Users/fipst/Desktop/ribterra/build/gameview-material-native-2026-10-08-attempt02/material-report.json) | Обидва actual exit0; кожен 136 files, відповідно125 099 583 / 125 099 591 bytes; 20 PNG, 5 fresh saves, 6 isolated negative catalogs×18files, report і2 runner evidence files |
| [Parent calibrated receipt](C:/Users/fipst/Desktop/ribterra/.unlazy/engine-completion-2026-10-08/material-calibrated-actual-2026-10-08.json) | Actual PASS: 181 sources / 3 098 bound files / 122 zero commands / 128 byte-exact derived files; 626 after-row labels derived у пам'яті, 40 in-memory negative controls |
| [Незалежний calibrated receipt](C:/Users/fipst/Desktop/ribterra/.unlazy/engine-completion-2026-10-08/material-calibrated-independent-2026-10-08.json) | Actual exit0 / **CLEAR**, ті самі181/3 098/122/128; manual rehash3 108unique input/artifact files, разом із receipt3 109files / 7 130bound observations, 0 mismatches; raw after-label rows збережено |

Той самий `GameViewMaterialProbe.exe`: 47 566 848 bytes, SHA256 `2F0AF8D761FC6479C998C7D6B2E199F8154A601B684291AD191DD5FCE509B23C`. Hardware: Direct3D12, NVIDIA GeForce RTX3050 Laptop GPU, driver32.0.15.9282, 1920×1080; graph`LegacyCombined`. World telemetry має zero Cumulative passes і zero contact/water/reflection work; explicit UiOnly menu має один Ui pass. `total_ms`43 771.9778 / 43 202.4178 — elapsed усього diagnostic run; FPS та GPU execution time null.

Перший запуск незміненого core verifier із seven actual arguments зупинився на `KeyError: relative_path` у after-tree rows. Runner записав там absolute path/bytes/SHA/observed after fields, а relative_path лише в before rows. Supplemental [calibrated verifier](C:/Users/fipst/Desktop/ribterra/tests/gameview_material_calibrated_evidence_tests.py) виводить відсутні relative labels із вже bound absolute paths **лише у пам'яті** та запускає незмінені повні original bindings/frame/query/semantic/repeat checks. Native JSON, captures, saves, snapshot і старий verifier не переписано. Calibration source створено після compiled cohort і не видається за її181-source snapshot. Parent та незалежний actual runs підтвердили before/after preservation і однакові semantic witnesses. Незалежний receipt має5 393bytes, SHA256 `7436297B3147C584B294AB0374281AC009F18A8BACBF8177F27C8A68DA29ABCD`; реальні day/night/HUD captures також оглянуто. Main build і full startup acceptance лишаються окремим OPEN кроком.

## Що саме виконано

Original schema1 catalog має 67 pages / 987 records. Підключено лише `building.house.ready` і `resident.man.walk.2.1` із accepted painted diagnostic collection; матеріали мають two-plane proxy XYZ. Resident crop139×213 зберігає original reference_width222, тому body scale не підмінено crop width. House crop529×428/pivot та resident pivot збережено. На zoom0.62/1.30/3.0 actual quads/query bounds/anchors відповідають original descriptors, typed UV є local0–1, RGB білий, normal frame має aligned neutral SurfaceVertex companions.

SDL input перевіряє covered/clear та half-open boundary для house/resident на кожному zoom. Це conservative max-pooled CPU alpha mask з longest side≤192 і threshold64, не full-resolution pixel picking. Parent verifier незалежно відновив original crops, pool cells/maximum alpha, source hashes, scale/pivot/UV і actual query rows.

| Zoom | Legacy↔mixed changed pixels, whole frame | House bounds | Resident bounds |
|---|---:|---:|---:|
| 1.30 | 1 373 429 | 34 569 | 1 476 |
| 0.62 | 320 463 | 7 879 | 365 |
| 3.00 | 1 843 026 | 184 290 | 7 701 |

Ці pixel differences порівнюють **повні Legacy/HDR routes**, включно з composition/background; вони не ізолюють внесок окремого material light і не доводять reference acceptance. Parent visibility oracle додатково підтверджує World nonuniformity: 364 646 unique RGBA / 1 479 795 nonmodal pixels. Qualified HUD-free central region має 304 801 unique RGBA / 970 757 nonmodal pixels і повторюється World↔HUD exact; opaque-black in-memory replacement відхиляється. Це знімає можливість видати uniform clear або закритий HUD scene за видимий світ.

Authority fixtures імпортуються й зберігаються реальним World, потім завантажуються ClientSession: day/wet ticks7200, night21600, winter calendar871200. Time/cover явно authored; earned cover/history replay не заявлено. Night hour0 проходить typed whiteRGB/lighting; wetness0.6005 походить із600permille+signed remainder14400/28800, змінює751 mixed-frame pixels і зберігає exact Legacy baseline. Known-weather calendar/Sun/ambient/wetness consistency перевірено; JSON current-sample witness не є незалежним seeded forecast replay.

Unmapped house level2/stage1/2/3/winter зберігають original UV/scale/pivot; actual one-axis move resident21,27→21,35 показує facing3 і phases0/1/2/3. Їхні first-use steps3/6/10/13 мають shown tick на1 позаду authority, original unmapped quads і один mapped house material. Інші directions/poses/cargo не покрито. Mine має original fallback: 7 964 vertices / 1 991 commands, typed0, canonical texture-binding equivalence і pixels equal. Actual pending localhost GNS connection маскує unknown weather і дає empty clear-only World; reconnect/recovery_incomplete route не симулювався.

Real RmlUi menu завантажується перед першим World render: 22Ui draws / 8textures. HUD додає47 draws / 10textures, змінює145 359pixels зі збереженням World prefix. Actual house selection/outline/annotations змінюють57 530pixels і зберігають2typed body draws. Outline тут original painted alpha; typed geometry outline equivalence OPEN. World→UI→World і обидва меню repeat exact. Усі6 cold invalid catalogs до submission доходять exact intended loader errors і зберігають mapping/frame/camera/selection/save; reload після actual UI submission відхиляється startup-only guard і також зберігає стан.

## Source та original assets

Fresh Build03 bound current source і snapshot, compiler/tools/libraries, four SDK trees, fresh shaders, control/UI kits, original181AssetCatalog inputs та material18file collection. Leaf1.33 компактно кодує всі1 607SDK header rows до/після як root-relative/base64 SHA256 triplets; їх повні paths і hashes відновлюються та перевіряються, поля не пропущено. Leaf1.28 original manifest має180rows, зберігає102old IDs і реєструє67world pages; reviewed live SHA256 `D28AA9DBC8C2566AACCDA51916D0C3F6EBE2183B92AA30852426C76F2DCB2218`, original archive retained. Images/catalog/prompts/notices не змінено цією registry repair.

| Snapshot source | SHA256 |
|---|---|
| `include/ribterra/view.hpp` | `76D195A14651548BDDCEC1403C120D4E1E50C326CDC169A8B248066AB0963903` |
| `src/client/view.cpp` | `4FA28CCDBDAA951A1C3DC80C5ED9BE5533695838549BB72EA2971202EA568C00` |
| `tools/gameview_material_probe.cpp` | `93182C7FB1976F79AA92B9D15FF90326843A01A5C8451B706F740E4F8770ADED` |
| `tools/build_gameview_material_isolated.ps1` | `7810B7B332D6C8192794DCD8B5A9AFDD29207E61A561417F89DC43C45BD1C1CD` |
| `tools/run_gameview_material_native.ps1` | `5DCAC3ECCD9880DA9CF1A78DF937FD1BC87F538014BA6634B5860E906C43E21D` |
| `tests/gameview_material_evidence_tests.py` | `365320787E37AD4EB54FAE6369A95B59BF71826971E5FDD3038DA1F05C5EFF0E` |
| `tests/gameview_material_semantics_tests.py` | `2E03E493E0F329D8091A83CB634B624129BDF2919291EBBDBAACF1514C7CB7BF` |
| Supplemental calibration, поза compiled snapshot | `0B10A11121AA826C929935202D5B6E559D05AE05AAF4FE9E7BC346722BA7C174` |

Build-report SHA256 `A5526AF7F9BEA9FD08D1DB2CD3425FA88012979A825924C26966110A6F3C752C`. Build01 strict compile failureC2666 і Build02 post-link128MiB budget failure retained. Build02 мав135 127 873bytes; lossless SDK-report codec зменшив metadata без зміни C++/flags/libs/shaders/SDK rows. Fresh Build03 пройшов output guard. Cleanup/deletion не виконувалося. П'ять literally named `.save` мають random World UUID і виключені лише з між-run byte equality; кожен окремо має власний hash binding. Решта128derived PNG/catalog files exact; deterministic report/semantic witnesses збігаються, varies тільки diagnostic total_ms.

## Межа checkpoint

У native report залишаються12OPEN scopes: full E01–E17/P01 engine; full normal main startup/package/release; neutral albedo та всі8production masters; reference fidelity; новий house draft export/native/production; Cumulative/Moon/weather/waterFX; typed geometry outline; усіresident directions/phases/cargo; remote reconnect/recovery_incomplete normal-view; physical two-PC/co-op; FPS/GPU timing; current full suite.

Original paint має baked lighting/glow і не є neutral albedo. Окремий repaired house XYZ draft усе ще має19 649unsupported original alpha pixels; новий14material neutral palette draft лише SOURCE CLEAR, без export/native/loader/alpha/outline acceptance. Цей normal diagnostic не підключає їх до production. Goal активний; checkpoint не закриває whole engine/reference gates і не є automatic OMX certification.
