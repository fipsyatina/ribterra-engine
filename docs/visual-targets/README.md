# Ribterra — графічні оновлення по кроках

**Детальний актуальний план 28.09:** [повні технічні картки V01–V05 і DL](../../.omx/plans/2026-09-28-graphics-updates-implementation-ua.md), [аудит ассетів і рушія](ASSET_ENGINE_AUDIT_2026_09_28_UA.md).
Оновлено27.09.2026. **Поточний порядок: V00 → V01 UI → V02 світ → V03 об’єкти → V04 дощ → V05 зима. Повний Dynamic — пізніше.**

## Головні файли
| Файл | Для чого |
|---|---|
| [Нова галерея](index.html) | Реальний старий кадр і5 послідовних макетів; порівняння з попереднім кроком |
| [План V00–V05](GRAPHICS_PROGRESS_UA.md) | Що змінюється, що залишається, де реалізувати й чим приймати кожний крок |
| [Майбутнє освітлення](FUTURE_DYNAMIC_LIGHTING_PLAN_UA.md) | Зроблена заготовка API, відкладений перемикач, майбутні DL-01–08 |
| [Контракт агентів](AGENT_VISUAL_CONTRACT_UA.md) | Масштаб, HUD, правдиві дані та native докази |
| [Старі13 targets](public-versions.html) | Макети публічних gameplay-версій0.26.1–0.27.0; не шкала зміни графіки |
| [Картки gameplay-версій](NEXT_UPDATES_DETAILED_UA.md) | Майбутні житло, їжа, логістика, навчання, кооператив і demo acceptance |

V00 є незміненим збереженим кадром0.26.0. V01–V05 створені built-in imagegen шляхом послідовних edits. Незмінність геометрії приблизна; точні pivots/picking/правильність тексту доводить гра. Показані погодні числа ілюстративні. Готовність не можна оголошувати за generated PNG.

## Технічні додатки
- [Аудит і карта графічних робіт V2](GRAPHICS_ENGINE_PLAN_V2_UA.md): поточні можливості, розрив до концептів, work packages. Попередні24 dev-пакети не є новою обов’язковою послідовністю базової0.26.1.
- [Майбутній Render Core](RENDER_CORE_SPEC_UA.md) і [Dynamic Lighting](DYNAMIC_LIGHTING_SPEC_UA.md): пропозиція повного режиму, ще не реалізація.
- [World FX](WORLD_FX_SPEC_UA.md): вода, haze, precipitation, footprints. Ефекти потребують окремого native proof; наявність картинки їх не реалізує.
- [Asset production](ART_PRODUCTION_SPEC_UA.md): art masters/export/матеріали/бюджети; normal/depth обов’язкові тільки для майбутнього Dynamic2_5D.
- [World/UI](../../.omx/plans/2026-09-26-world-ui-implementation-spec-ua.md), [weather](../../.omx/plans/2026-09-26-weather-implementation-spec-ua.md): simulation/path/build/save/network dependencies.

## Походження
Нові сцени: [дані](progress/scenes.json), [промпти](progress/prompts.json), [маніфест](progress/manifest.json).
Старі13 зображень: [оригінальні промпти](generation-prompts.json), [оригінальний маніфест](manifest.json). Їхні PNG і hashes не змінюються.

## Початок виконання
Брати один V-крок, не всю графіку одразу. Звірити HEAD і чужі зміни. Реалізувати через справжні renderer/RmlUi/sim interfaces, потім зняти ту саму сцену до/після. Для погоди спершу пройти authority/save/net, для об’єктів — калібрування малого kit. Не додавати Road, замки, нову фізику або dynamic lighting приховано.

Динамічне освітлення наразі недоступне: API чесно лишає effective=Legacy або відхиляє явний unsupported startup. Перемикача в UI немає. Коли настане DL-етап, його введення не змінить simulation/save/protocol.
