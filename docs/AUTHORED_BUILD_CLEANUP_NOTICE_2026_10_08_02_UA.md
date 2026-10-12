# Повторне повідомлення перед видаленням старих compiled-копій

08.10.2026. Остаточний план04 для того самого списку 56 файлів. Попередній Apply plan03 зупинився до першого видалення через неправильне зіставлення порядку записів; всі файли лишилися. Порівняння тепер за точним шляхом зі збереженням усіх перевірок size/timestamp/SHA/scope. Переставлені записи проходять, змінений SHA відхиляється.

Причина: authored attempts06–09 замінені перевіреною attempt10. Зберігаються вся latest attempt10 та rollback attempt05, свіжі GameView/native докази, усі нецільові source/report/log/PNG/asset/shader/kit/font файли і чинні launcher targets. Незалежна звірка всіх 4359 записів: без розбіжностей.

Цілі: тільки 56 наведених файлів, 228 214 270 логічних байтів (228,21 MB). Каталогів не видаляють. Локальне видалення остаточне, не через кошик; GitHub uploads не виконуються. Код для повторної збірки збережений, але відновлення історичного byte-exact executable не гарантується. Старі build reports лишаються історичними hash-записами, не доступними executable для повторного запуску.

Manifest: `.omx/plans/authored-build-prune-2026-10-08-04.json`.

SHA-256 manifest: `67EFFB72CDE0A8FE14815D6E007B0AB6CDE5F319B2E83F8A290C36B81EEC7B32`.

SHA-256 helper: `134854B8B3DE219D396284539D5DE93040E64EE5D2E9A5E693D54331F68F32FF`.

Після цього повідомлення plan04 фактично застосований: exit0, 56 видалених файлів, 0 цілей лишилося, 4303 захищені файли незмінні. [Повний результат і точні виміри](AUTHORED_BUILD_COMPACTION_2026_10_08_UA.md).

## Всі 56 точних шляхів

- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\AuthoredSkyBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\AuthoredSkyNativeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\MaterialBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\asset_catalog.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\authored_sky_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\authored_sky_native_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\contact_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\foundation.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\game_art.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\material_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\renderer2d.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\obj\sky_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\SDL3_ttf.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt06\SDL3.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\AuthoredSkyBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\AuthoredSkyNativeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\MaterialBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\asset_catalog.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\authored_sky_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\authored_sky_native_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\contact_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\foundation.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\game_art.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\material_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\renderer2d.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\obj\sky_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\SDL3_ttf.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt07\SDL3.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\AuthoredSkyBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\AuthoredSkyNativeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\MaterialBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\asset_catalog.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\authored_sky_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\authored_sky_native_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\contact_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\foundation.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\game_art.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\material_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\renderer2d.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\obj\sky_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\SDL3_ttf.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt08\SDL3.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\AuthoredSkyBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\AuthoredSkyNativeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\MaterialBridgeProbe.exe`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\asset_catalog.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\authored_sky_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\authored_sky_native_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\contact_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\foundation.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\game_art.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\material_bridge_probe.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\renderer2d.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\obj\sky_shadow.obj`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\SDL3_ttf.dll`
- `C:\Users\fipst\Desktop\ribterra\build\sky-authored-2026-10-08-attempt09\SDL3.dll`
