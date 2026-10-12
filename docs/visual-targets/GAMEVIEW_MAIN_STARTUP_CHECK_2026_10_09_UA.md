# Ribterra: фактичний звичайний запуск main, 09.10.2026

**Прийнято конкретний checkpoint запуску.** MainBuild01 і native03/04 пройшли основну та незалежну перевірки: вісім реальних процесів, вісім PNG, меню та ігрова сцена з optional material collection і без неї. Установлений пакет, показ mapped house, нейтральний production art, референси, повний рушій та ≥120FPS ще OPEN. Ціль активна.

Запускався справжній `Ribterra.exe` зі свіжими shaders/fonts/DLL та `--catalog-assets` на окремо перевірений оригінальний реєстр. Це запуск main із referenced assets; default package/launcher/release не змінено. Кожен процес використовував власний збережений automatic test profile. Усі36 файлів звичайного профілю зберегли membership, bytes і SHA256.

| Артефакт | Фактичний результат |
|---|---|
| [MainBuild01](C:/Users/fipst/Desktop/ribterra/build/gameview-material-main-2026-10-08-attempt01/main-build-report.json) | Strict compile main0/link0,23 accepted production objects,93 fresh shader runtime files;113files/60508353B |
| [Native03](C:/Users/fipst/Desktop/ribterra/build/gameview-material-main-native-2026-10-08-attempt03/main-native-execution.json) | Runner0, four game processes0;27files/15693758B, four empty isolated climate-v1 profiles |
| [Native04](C:/Users/fipst/Desktop/ribterra/build/gameview-material-main-native-2026-10-08-attempt04/main-native-execution.json) | Runner0, four game processes0;27files/15693855B, four empty isolated climate-v1 profiles |
| [Parent receipt](C:/Users/fipst/Desktop/ribterra/.unlazy/engine-completion-2026-10-08/main-startup-actual-2026-10-09.json) | Actual unchanged four-argument verifier exit0, eight processes/captures; direct unsigned receipt |
| [Independent receipt](C:/Users/fipst/Desktop/ribterra/.unlazy/engine-completion-2026-10-08/main-native-independent-2026-10-09.json) | Independent actual same CLI exit0/CLEAR; separate rehash2991unique files/14201observations/3214SDK triplets/0mismatches |

Main EXE47488000B SHA256 `47E40A41D78E585E61ED4EE117DF9B38EB7F2632BEE3A5BD3C168366F3236282`; report `A5F6183EACCA5DCAC005AD5B4EFFE8350C8B1C0C4B2D2CF25F19CC53C8285380`. Runner `95A5F178FD22F70E477CB354AE2AA94922E198FB8B57D6C99330F8E3E1ED042A`, unchanged verifier `BC434FACC1E3FB1DC22E416609D1F8E47E2B4138B2E7936C79D7509246870C9B`. Production/main compiler flags залишилися strict; probe-only `/Zc:inline` не перенесено до main чи production.

Actual hardware: D3D12/NVIDIA GeForce RTX3050 Laptop GPU/driver32.0.15.9282;1440×900,144Hz, vsync, two frames in flight, software=false. Refresh rate не є виміряним FPS. Menu decoded RGBA однакові в усіх чотирьох випадках, SHA256 `8c8ba335f18a3c9a527d284631d3b155939ba7b58410e999143f40820d73c782`. Усі PNG opaque; кожен World має1295998 nonblack pixels. Live frame clock змінює World pixels між запусками, тому точне повторення World не заявляється.

Центральна область `[320,200,1080,650]` має342000pixels. Ordinary World:341954nonmodal і227696/227655unique RGBA; optional World:341951nonmodal і236987/236988unique RGBA. Uniform black/grey in-memory controls відхиляються. Основний і незалежний огляди фактичних PNG підтвердили видимий світ та реальний HUD.

У frozen verifier є неточний текст «no selection»: `main.cpp`268–269 явно фокусує першого поселенця, і кадр показує Selected settlers/Marta. Ця панель має border-box/maxheight205/bottom14, її верх не вище681px у900px window, отже вона не перекриває область із нижньою межею650px. Pixel oracle залишається обґрунтованим; неточний prose scope уточнено тут і в незалежному receipt. Immutable verifier/source/raw results не переписано.

Демосцена містить gather hut/tent, а mapped house відсутній. Optional collection успішно проходить cold startup, проте сам прапорець не доводить material draw count. Typed body visibility, нова XYZ alpha/outline, neutral palette і production/reference acceptance перевіряються окремо.

Failed native02 збережено зі complete=false/accepted=false: усі чотири гри повернули0, але PowerShell-reader помилився в сортуванні незмінених36 preference rows. У leaf1.37/1.38 виправлено лише тип об'єктів до сортування; actual pure checks підтвердили113 main bindings і36 preferences. Нові03/04 пройшли весь цикл; failed02 не перетворено на accepted. Файли не видалялися.

Ці докази прийнято для frozen source/build/native cohort. Наступні зміни exporter/C++ зроблять її історичним snapshot; перед прийняттям нового коду потрібні нові strict CPU/GPU докази. Formal gate-check checkboxes залишаються pending, автоматичні сертифікати/OMX leases не заявляються.
