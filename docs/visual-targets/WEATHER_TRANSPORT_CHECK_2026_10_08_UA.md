# WeatherView: пошкоджені пакети й відновлення сесії — 08.10.2026

CPU/localhost checkpoint пройшов strict build, два фактичні запуски та незалежний actual review CLEAR. Це перевірка конкретних wire/session переходів; повні E06/E01–E17/P01, два ПК, WAN, UI/FX і FPS залишаються відкритими. Production transport у цій хвилі не змінено; launcher не оновлювався.

Авторитетне дерево `C:\Users\fipst\Desktop\ribterra`, гілка `AInora/first-playable`, HEAD `bdc53e82c3d65eee4a5d8b13497aea67dff4a33b`. Код save9/protocol23/rules19/generator8, unreleased0.26.3. Після наступних bound header змін точна cohort стає історичною.

## Actual cohort

- Build `build/weather-transport-2026-10-08-attempt02`: actual exit0, 15 translation units,20 zero commands,420 source/SDK rows, `/std:c++20 /O2 /MT /W4 /WX`;67 655 797 bytes.
- `WeatherTransportProbe.exe`:8 617 472 bytes, SHA256 `82148ECF238894975B7438826AAF4E04EC7E6C2FCFE6B9922486FA931F0A7859`.
- Native `build/weather-transport-native-2026-10-08-attempt02` та `...attempt03`:actual exit0,70 files/run. Точний inventory:65 packet files, два save, один report і два runner evidence files.
- Parent verifier actual exit0:420 sources/1148 bound files/20 zero commands/65 byte-exact packet files. Весь native JSON повторюється точно; measured порожній. Два названі save з random World UUID окремо прив'язані до свого run.
- Independent reviewer особисто rehashed1148 unique files без розбіжностей і фактично виконав current read-only verifier з exit0. Автоматичної certification не оголошено.

```powershell
C:\Python314\python.exe -B tests/weather_transport_evidence_tests.py build/weather-transport-2026-10-08-attempt02 build/weather-transport-native-2026-10-08-attempt02 build/weather-transport-native-2026-10-08-attempt03
```

Build01 був успішним, але native01 зупинився на full-baseline fingerprint після timeout/reset: authored fixture додавала chunks після вже відсортованого prefix, тоді як production apply сортує весь список. Додано сортування fixture перед encode/split; повний fingerprint oracle залишився. Failed build/native roots,65 packet files і logs збережено. Нових видалень немає.

## Підтверджені переходи

58 actual malformed frames мають правильно перебудовані outer length/SHA256. Чотири required references не проходять inner FlatBuffers verifier; інші54 структурно валідні та відхиляються weather validation. Покрито current/forecast/cards/counts/intervals, enums/profile/scenario/version, tick/offset/day/winter, температурні й cover bounds, signed remainder, canonical partitions/summary/storm metadata. Rejection не змінює complete publication, WeatherView чи WorldID. Є сім позитивних encoded counterparts.

12 apply rejects перевіряють транзакційність і climate identity на delta. Валідна changed-identity delta декодується структурно, але не застосовується; sender також вимагає full replacement. Full replacement проходить. Шість multipart parts мають чотири negative controls: malformed part, легально валідний weather header mismatch, conflicting repeated metadata та actual accept після11s timeout. Identical repeat залишається pending; після reset повний baseline застосовується точно.

Actual GNS localhost використовує два ClientSession та третій звичайний NetworkClient для незалежного received WorldID. Він не має observer role. Host стоїть, крім явно викликаних ticks.12 session controls проходять waiting/pending unknown, calendar day31→32, late join,1×/2×/4×, pause/resume та два справжні shutdown/disconnect/restart/resume цикли. Final simulation tick28846. Host WorldID, player IDs і resume tokens зберігаються; plaintext credentials у report немає.

Після другого restart непідтверджена команда попередньої epoch дає actual recovery_incomplete: навіть complete baseline не відкриває weather/calendar. Явна нова команда очищає цей стан, і клієнти знову збігаються з host WeatherView. Client RNG/cache не генерує погоду. Direct SnapshotAssembler transitions не видаються за fine-grained ClientSession part sampling.

8.909/9.179s subprocess timings діагностичні. Ще потрібні backup rotation failure runtime, широкий suite, актуальні renderer/HUD/FX, shipping зимовий startup, physical two-PC/WAN, simulation/GPU performance, input/video та візуальне приймання референсів.
