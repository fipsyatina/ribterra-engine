# Перевірка детального плану — 28.09.2026

Об'єкт: `.omx/plans/2026-09-28-graphics-updates-implementation-ua.md` та `ASSET_ENGINE_AUDIT_2026_09_28_UA.md`.

Незалежний critic спочатку повернув REVISE із трьома блокерами:

1. Чинний draw_world/atmosphere уже створює Fog commands, тому додавання Atmosphere після них порушує renderer nondecreasing-pass contract.
2. Неоднозначність, чи V04 реалізує snow accumulation, чи лише резервує поля для V05.
3. Брак точної canonical формули погодного коефіцієнта роботи й golden vectors.

Виправлено в V04:

- Явне розділення terrain/objects, atmosphere і world interaction overlays; перенесення smoke/fire; FoW описано як current reveal filtering, а не вигаданий final mask pass.
- Повний authoritative climate/cover/wetness implementation і тести належать V04; V05 додає зимове відображення, сценарій та footprints.
- Додано integer snow/wet rates, work caps, remainder lifecycle/reset, affected jobs, tick ordering і контрольні приклади.

Повторний verdict: **APPROVED**. Блокерів у перевірених виправленнях немає. Це review плану, не перевірка реалізованого renderer або нової погодної системи.

Окрема перевірка документа: усі посилання нового плану/копій існують, file:line не виходить за межі файла; шість карток та десять ілюстрацій відкриваються в Desktop HTML на ширинах 390/1280/1920, без горизонтального overflow і JavaScript errors. Деталі у `detailed-plan-check.json`, `detailed-plan-render-check.json`; screenshot `detailed-plan-preview.png`.

Джерельний код рушія та runtime assets у цьому кроці не змінювалися. Наявні diff LightingMode/RenderFrameTests — попередня робота. Native game tests у цьому кроці не запускалися, оскільки результат — документація й інвентар.
