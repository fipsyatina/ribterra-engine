# Час сесії для світла, води й туману — 08.10.2026

**Проміжний поріг пройдено:** opt-in `SessionShown` використовує той самий presentation `MotionClock`, що й рух мешканців. Світло, вода й туман отримують узгоджену фазу. Це локальна нативна перевірка двох нейтральних категорій; **повний E06, E01–E17 та reference fidelity залишаються OPEN / NOT_ACCEPTED**.

## Реалізація

`EngineControlSettings::time_source` за замовчуванням залишається `ExplicitDiagnostic`. `SessionShown` вимагає готовий світ, water і environment V1. Він підміняє лише environment hour та water/fog time. Cloud, haze, wind і flow поки явні діагностичні входи, а не отримані від хоста погодні величини.

Підготовлений `MotionClock` копіюється й перевіряється разом із candidate. Кадр, камера, picking та contact queries публікуються до commit годинника. Відхилений candidate не змінює наступну фазу часу. Normal composition повторно використовує цей prepare/commit і одноразове оновлення motion tracks після reset.

Календар має початок 06:00: ticks 0/7200/14400/21600/28800 відповідають 06/12/18/00/06. Безпосередньо перед опівніччю перетворення double у float не дає невалідну годину 24. Межа shown seconds ≤1 000 000 явно збережена; довготривалий clock rebasing ще не реалізований.

## Actual execution і source binding

- Repo `C:\Users\fipst\Desktop\ribterra`, branch `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`; наявний dirty source збережено.
- Fresh build `build/gameview-session-time-2026-10-08-attempt02`: MSVC C++20 `/O2 /MT /W4 /WX`, 21 production translation units і новий probe; 158 source bindings, 120 команд із фактичним zero exit. 23 shader programs / 46 stages; root 528 файлів / 119 150 652 bytes.
- Probe SHA256 `1B67C317295870723D992461C509F0BC314D1A9774A71CA6290DB9537F27B7A4`.
- Два actual native runs: `build/gameview-session-time-native-2026-10-08-attempt01` і `attempt02`, кожен exit 0. Direct3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080.
- Кожен run: 17 GPU PNG, raw WaterCoverage RGBA8, JSON і п'ять current-schema world saves. Виконано cumulative eight-pass graph без contact rows.
- Parent verifier actual exit 0: `session time current evidence checks passed; sources=158, bound_files=599, zero_commands=120, repeat_outputs=19`.
- Усі 19 derived outputs — 17 PNG, raw carrier та JSON — byte-exact між actual runs. Лише п'ять точно названих saves виключені з cross-run comparison через власні випадкові World identities; кожен save окремо SHA256-bound до свого execution.
- Independent source та actual-evidence review: CLEAR. Reviewer окремо перевірив snapshot/current hashes, обидва actual subprocess executions, усі 19 derived SHA256 і чотири calendar PNG.

Команда перевірки поточної cohort:

```powershell
C:\Python314\python.exe tests/gameview_session_time_evidence_tests.py build/gameview-session-time-2026-10-08-attempt02 build/engine-cumulative-kit-2026-10-08-check01/export-a build/gameview-session-time-native-2026-10-08-attempt01 build/gameview-session-time-native-2026-10-08-attempt02
```

Після наступної зміни bound source цей checkpoint стає історичним. Старий cumulative build04 також залишається історичним доказом контактів; він не видається за перевірку пізнішого source.

## Причинні контролі

Чотири calendar landmarks мають точну рівність full frame payload і native PNG з еквівалентним `ExplicitDiagnostic`. Native changed pixels від ранкового кадру: noon 375 018, evening 375 018, midnight 374 613. Це зміна діагностичної сцени, а не вимір референсної якості.

Вісім clock cases охоплюють actual ClientSession advancement, interpolation/catch-up без extrapolation, starvation, 1×/2×/4×, pause/resume, repeated-epoch production load/reset, pointer replacement, wall/authority rollback та actual changed epoch. Драйвер застосовує pause/speed policy нормального main loop: `ClientSession::advance()` сам по собі її не застосовує. За десять frame deltas по 0.1 секунди authority ticks становлять 20/40/80; shown endpoints 19/38/76. Кожна швидкість окремо перевірена через pause/resume без wall-time backlog. Окремий paused native repeat byte-exact.

Repeated-epoch load/reset є невакуумним: pointer, epoch 2 і authority tick 10 незмінні, wall time монотонний; shown tick до load 2.2, після production `reset_world_state()` — 10. Заміна pointer перевірена за однакових tick 14 / epoch 2; зміна epoch 1→2 — за незмінного authority tick 10. Великий tick gap або rollback не підміняє ці reset controls.

Усі 18 invalid candidates відхилені: enum, відсутні environment/water, конфлікт manual atmosphere, version, cloud/haze bounds і NaN, camera, sprite/owner/occluder IDs, instance NaN і wall NaN. Retained frame/camera/pick та наступний shown clock збігаються з незалежно завантаженим GameView oracle. Recovery tick 2.2 менший за authority 4 і відрізняється від snap, який спричинив би неправильний ранній commit.

У всіх captures exact raw carrier та UI marker RGBA `[31,189,78,255]`; local-light rows і emission/material metadata збережені. Відсутні невалідні WaterCoverage tuples; bridge deck 1 865, water 70 657 і land 302 496 pixels. Known UI marker не є перевіркою RmlUi HUD.

## Помилки та межі

Build01 збережений: actual compiler exit 2, probe `constexpr`-масив зі `std::string` вимагав persistent heap allocation під C++20. Виправлення лише `constexpr`→`const`; новий build02 збудований свіжо. Невдалий build не видається за успішний запуск.

Перевірка використовує zero-population authored terrain fixture, два нейтральні sprites та три authored completed Bridge records. Вона не виконує будівництво мостів. Weather authority/save/wire, rain/snow/wind particles, wetness/drying, footprints, normal startup, референсний арт, RmlUi/DPI, physical input/video й два фізичні ПК ще потребують роботи.

FPS і GPU execution time не вимірювалися (`null`). 174 182 400 attachment bytes для двох frame sets — номінальний обсяг targets, не peak VRAM і не підтвердження 120 FPS. Goal залишається ACTIVE; це direct execution evidence, без автоматичної gate-check certification.
