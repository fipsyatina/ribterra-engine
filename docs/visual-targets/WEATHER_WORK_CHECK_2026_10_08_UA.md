# Погода та фактичний прогрес роботи — 08.10.2026

Окремий CPU checkpoint пройшов strict build і два фактичні запуски. Це підтверджує робочі накази та keyed persistence; повний E06, графіка, референси й E01–E17/P01 залишаються відкритими. Звичайний launcher не оновлювався.

Авторитетне дерево: `C:\Users\fipst\Desktop\ribterra`, `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`. Save9/protocol23/rules19/generator8, unreleased0.26.3. Production-файли після authority baseline не змінено.

## Виконання й прив'язки

- `build/weather-work-2026-10-08-attempt01`: actual exit0, 15 translation units, 20 actual commands, `/std:c++20 /O2 /MT /W4 /WX`; 420 source/SDK rows, 63 510 839 bytes. Shader/GPU/UI збірки тут немає.
- `WeatherWorkProbe.exe`: 8 042 496 bytes, SHA256 `F9BF723153BB7853AEF3653945FA950AC68F41D930DA4CDC8E0465D15F318A4D`.
- `build/weather-work-native-2026-10-08-attempt01` та `...attempt02`: actual exit0; кожен має рівно два save, один report і два runner evidence files. Два save окремо прив'язані до кожного запуску; UUID payload не зобов'язаний повторюватися byte-exact.
- Parent read-only verifier actual exit0: 420 sources, 1018 bound paths, 20 zero commands. Перевіряє current/snapshot/SDK/tools/libs/logs/executable/argv/outputs, точний inventory, negative hash control і повторюваність усіх стабільних полів JSON. Відмінними можуть бути лише три явно названі measured поля.

```powershell
C:\Python314\python.exe -B tests/weather_work_evidence_tests.py build/weather-work-2026-10-08-attempt01 build/weather-work-native-2026-10-08-attempt01 build/weather-work-native-2026-10-08-attempt02
```

Незалежні source та actual evidence reviews CLEAR: reviewer особисто rehashed1018 unique bound files без розбіжностей і фактично виконав read-only verifier з exit0. Автоматичної gate-check/OMX certification не оголошено. Нових видалень не виконувалося.

## Фактичні результати

Обмежений пошук знайшов Standard seed15, day36, calendar/simulation tick1008000 після1489 перевірених вікон. Усі наступні20 канонічних денних samples мають temperature<=−200 deci°C, rate8500 і reasonCold. Це діагностичний seed, він не обраний для shipping зимового сценарію.

Woodcut/Mine/Forage на Surface: 20 eligible ticks, 17 реальних просувань, progress0→0.425, integerXP120→120. Build: progress0.25→0.42,1700 useful-work practice remainder, integerXP120→120. Перші залишки8500/7000/5500/4000, дозволиfalse/true/true/true. Denied tick не змінює відповідну практику. Підземний Mine просувається20 разів до0.5 без outdoor row.

Справжній remainder7000 отримано двома реальними ticks. Він зберігається під час чотирьох нічних ticks, чотирьох ticks руху з фактичною зміною позиції та одного tick незавершеного path request. Остання fixture явно містить8280 connected cells при planner budget4096.

Actual public same-key replacement, manual cancel, stop і construction cancel очищають accumulator. Повторне призначення створює новий рядок через фактичні ticks. Correlated legacy Replan timeout уночі очищає ключ без progress/XP; obsolete candidate проти новішої Activity відхиляється reservation guard без зміни світу. Це admission/source-branch доказ, а не виконання неможливої accepted obsolete reservation.

Earned-yield fixture явно починається з partial progress0.58 і вичерпаним diagnostic logistics ID limit. Двадцять справжніх cold ticks доводять progress до1 і залишок до0; blocked retry їх не витрачає. Явне повернення diagnostic calendar до канонічного cold sample зберігає earned state. Після відновлення ID limit yield дає3Wood та XP121 навіть коли rate8500 із залишком0 заборонив би нову роботу. Пропущена історія покриву не симулювалася.

Усі16 пошкоджених кандидатів відхилено на ненульовому simulation tick із genuine work rows; live checksum/WeatherView/work залишилися незмінними. Випадки охоплюють resident/job/target/building, duplicate/order/overflow, dead actor, missing Route.work та Activity mismatch.

`work-gather.save` і `work-build.save` реально записані з remainder7000. Load відновлює повний private ClimateRuntime, exact Activity binding та checksum; кожна пара world/loaded виконує200 фактичних ticks з рівними checksum, WeatherView і private work rows на кожному кроці.

## Межі приймання

Fixtures авторять terrain/skills/progress або явно пересувають diagnostic clock; production import не виправляє погоду й не програє пропущену історію. Native total13.015/11.544s і cold search389/369ms — timings усього діагностичного набору, вони не доводять simulation overhead, FPS чи швидкість звичайної гри.

Ще потрібні malformed wire/session continuity checkpoint, backup rotation failure runtime, актуальний широкий suite, certified snow-only winter seed та фізичний ration horizon, renderer/HUD/FX/snow/матеріали, native video/input, two-PC/WAN, full simulation/GPU performance і візуальне приймання референсів.
