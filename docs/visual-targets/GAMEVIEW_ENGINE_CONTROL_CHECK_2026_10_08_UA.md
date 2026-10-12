# GameView — обмежений контрольний GPU-шлях

08.10.2026. **Технічний компонент пройшов перевірку; повний двигун НЕ прийнято.**

Додано окремі `load_engine_control_art` і `compose_engine_control`: справжні `ClientSession` та кеш місцевості GameView, але вісім зовнішніх нейтральних presentation-контролів. Звичайні `compose`, запуск, launcher, генератор, HUD та каталог фінального арту не переключені на цей шлях. Це не нова версія гри і не зразок її фінального вигляду.

## Реалізація

- Рельєф береться зі справжніх shared terrain corners. Кожний quad має дві NW–SE трикутні площини зі своїми unit normals та узгодженими camera-relative P/N. Висота ніг контролів інтерполюється на цих самих трикутниках.
- Ground і lit-sprite draws проходять через typed `SkyShadowSlice`; schema-2 матеріали мають шість каналів. Sky triangles і local-occluder rows походять від тих самих anchors та масштабу.
- Нейтральний kit: будинок, склад, майстерня, дерево, скеля, мешканець без вантажу, з колодою й з мішком. Будинок має дві emission-window області. Це технічна геометрія, **не копії концепт-арту**. Поширення світла з вікон на оточення цим прогоном не доведене.
- Невідомі об'єкти пропускаються до перевірки sprite ID/масштабу/ID і до наповнення видимих буферів. Координати перевіряються до float→int, без clamping. Caller frame публікується лише після успішного складання; повна незмінність приватних кешів на кожному failure path не заявляється.
- Тимчасові межі: camera-relative ±32, zoom 0,5–2, cell radius ≤12, scale=1, ≤32 видимих контролів, лише surface layer і цілком розвідане requested ground window. Це не повний pan/zoom/reveal контракт гри.

## Фактичні докази

Свіжа `build/gameview-engine-2026-10-08-attempt05`: 21 production translation unit + probe, `/W4 /WX`, 27 command sidecars exit0. 96/96 repository/snapshot source bindings, 8/8 захищених inputs, 52/52 файли окремого kit і 69/69 shader files перевірені. Шейдери повторно використані з прийнятого authored attempt10 після hash/source-closure перевірки; **нової shader compilation/negative перевірки немає**.

Build root: 113 824 517 байтів, у межах 128 MiB; на C: виміряно 97 412 231 168 вільних байтів до native запуску, понад 384 MiB. Ранні build01–04 завершились невдачею та були виправлені: scoped-name/explicit-conversion warnings, isolated GNS include/static-link settings і spelling UV aggregate. Warning repairs не змінюють механіки або звичайне відмалювання.

`GameViewEngineProbe.exe` фактично запущено двічі, кожний exit0:

- `build/gameview-engine-native-2026-10-08-attempt01`
- `build/gameview-engine-native-2026-10-08-attempt02`

Кожний root: 5 PNG розміром **1920×1080** + complete JSON, 299 858 байтів. **6/6 файлів byte-exact** між прогонами, включно зі звітом. Незалежний огляд підтвердив лише цей bounded gate.

- Native D3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver 32.0.15.9282, software=false.
- Real offline seed70421, камера(21,21), zoom1,3/radius8: 289 ground cells, 8 controls, 248 sky triangles, 10 local occluders, 8 точних cell-picks.
- Кожний capture: submitted/captured, exact pass order SunShadow→MoonShadow→GroundGBuffer→GroundLighting→WorldForward→Composite→UI (`[6,7,0,1,2,3,4]`), exact UI RGBA(215,42,76,255).
- Sun і повтор Sun byte-exact на різних slots. Доданий невідомий control з некоректним ID/NaN scale/нульовими IDs не змінює геометрію або native pixels.
- Видалення лише sky casters змінює 53 850 pixels. Sun→Moon змінює освітлення без зміни geometry fingerprint.
- Invalid radius і скінченний `FLT_MAX` coordinate відхиляються без зміни caller frame.
- Окремий existing-output negative: actual exit1 до SDL запуску; 6/6 наявних файлів незмінні, failure JSON не створений у чужому каталозі.

Main переглянув сирі `gameview-sun.png` і `gameview-moon.png`. Видно нейтральний рельєф, зміну колірної температури, emission-вікна і напрямків тіней. Це GPU readback з окремого видимого SDL-вікна, не desktop scanout, інтерактивний control-test або немонтоване відео. Native computer-use APIs недоступні в поточному інструменті.

## Hash anchors

| Файл | SHA-256 |
| --- | --- |
| `src/client/view.cpp` | `69BD83299440672813C85C0D6134BAE2AFFE3976BFFC1640B35A5148C8DAA88F` |
| `tools/gameview_engine_probe.cpp` | `CD5BFA9B40AD426A20D295D2CB5A53FFAC2587C70C2301BAC6DAEA843490CD3B` |
| `GameViewEngineProbe.exe` | `69512E6426332756133B316B891CF6B128C25B8A8BEEF8C5E131E6C10E9BFC53` |
| `build-report.json` | `815F2398BF02BF315A5C64B5D365E033060E8846943743394DF1AAAC4CFF03E4` |
| Обидва `gameview-engine-report.json` | `4191CEA2E84DDEC928BB00E98EAE6D2084114D6A2402E7988EF228B13B970022` |

## Що залишається OPEN

Звичайний GameView, усі world objects, full camera/zoom/reveal matrix, water/bridge receivers, causal local/contact pixels, реальне поширення emission світла, повна сцена з погодою/туманом/снігом, художня відповідність концептам і HUD. GPU timing у captures — null; readback не є performance measurement. **120 FPS не виміряно**, повний CTest не запускали, E01–E17 не закривали. Далі — розширення виробничого engine-шляху й його перевірок; до ігрових механік не переходити.
