# Повідомлення перед очищенням GameView compiled-копій — 08.10.2026

На пряму вимогу користувача не накопичувати старі версії підготовлено лише 102 конкретні згенеровані compiled-файли у чотирьох старих test roots. Це повідомлення створене ДО Apply; стан на момент створення — нічого з цього списку ще не видалено.

Plan: `.omx/plans/gameview-build-prune-2026-10-08-01.json`.

Plan SHA-256: `2F0E381B68026EAF596D8F58C8AE4BEE6A2DD45CA2DCCF0E7C1B8C0980EDE56A`.

Helper SHA-256: `B376B91302865938664C8A78540AE0D4E0E470E6F8D9A9BC914E96B3F8680307`.

Сумарно **102 файли / 400 058 964 логічних байтів (400,06 MB)**. Це не обіцянка рівного приросту вільного місця: фактичний обсяг вимірюється після операції.

Причина: спроби06–09 замінені свіжою strict build10 і двома повторними bounded native06/07. Застарілі OBJ/EXE/DLL/LIB/EXP не потрібні для поточного прогону.

Захищено 3859 файлів: усі нецільові файли цих чотирьох roots, GameView05 rollback і10 latest, усі GameView native01–07, sky-authored05/10 та authored-native01–06, увесь kit check05 (A/B і negative evidence), 96 live source bindings, launcher/pointer та фактичні запускні EXE. Каталоги, вихідний код, звіти, screenshots/PNG, assets, shaders, generated headers і compiler-temp НЕ видаляються.

Видалення — остаточне локальне `Remove-Item`, не кошик і не GitHub-архів. Збережені snapshots/logs/залежності дозволяють відновити функціонально відповідну збірку; побайтове відновлення саме історичних EXE/OBJ не гарантується. Запускні latest/rollback копії залишаються.

## Повний точний список цілей

### gameview-engine-2026-10-08-attempt06

21 файлів; 59793017 логічних байтів.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\world.obj
```

### gameview-engine-2026-10-08-attempt07

27 файлів; 113414047 логічних байтів.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\SDL3.dll
```

### gameview-engine-2026-10-08-attempt08

27 файлів; 113424631 логічних байтів.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\SDL3.dll
```

### gameview-engine-2026-10-08-attempt09

27 файлів; 113427269 логічних байтів.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\SDL3.dll
```

## Перевірка перед і після

Apply дозволено лише для повідомленого незмінного Plan SHA. Helper повторно звіряє шляхи, bytes, mtime та SHA; після видалення перевіряє весь protected inventory і нуль решти цілей. Фактичний результат буде окремим `.json.result.json`, без переписування цього Plan. До результату не заявляти очищення виконаним.

