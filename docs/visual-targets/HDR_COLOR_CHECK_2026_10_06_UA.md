# Лінійний HDR світу — перевірений частковий крок двигуна

Перевірки: 05–06.10.2026, Europe/Warsaw. Статус: **SOURCE / LOCAL NATIVE PROOF, НЕ ПРИЙМАННЯ ПОВНОГО ДВИГУНА**.

Додано опційний колірний шлях у тому самому `Renderer2D`, який використовує гра: світ накопичується в RGBA16F, окремий Composite застосовує exposure і один sRGB encode, після нього малюється UI. Збірка, 10/10 цільових CTest, свіжий D3D12 HDR-прогін і native регресійні перевірки пройшли. **Усі E01–E17 залишаються відкритими. Ціль ≥120 FPS із повною сценою не доведена.**

## Що реалізовано

Колірний порядок цього кроку:

`World RGBA16F → Composite / RGBA8 SDR → незмінний UI → SDR UNORM swapchain`

- `Frame2D::linear_hdr_world` — явний opt-in; за замовчуванням Legacy не змінюється. `world_exposure` має бути скінченним у `(0,64]`; не-unity exposure без HDR відхиляється. HDR вимагає непрозорого clear; `clear_commands()` скидає opt-in/exposure.
- Оpaque surface-lighting kernel може записувати лінійну яскравість понад 1 у RGBA16F, без раннього SDR clamp/encode. Порядок opaque lit/unlit команд збережено.
- Світові Opaque/Alpha/Additive/PremultipliedAlpha проходять через лінійний target. Encoded premultiplied input спочатку unpremultiply → decode → repremultiply; alpha=0 має окрему безпечну гілку.
- Це **BakedCompat color bridge**: існуючий encoded texture×vertex-color product декодується як сумісний результат. Він не є новим нейтральним Dynamic-матеріалом і не створює відсутніх normal/depth/emission каналів.
- Composite виконує exposure + explicit sRGB encode + SDR clamp. Остаточну художню tone curve ще не прийнято. UI використовує попередні byte-authored shader/blend/text шляхи й не входить до world exposure.
- Формат перевіряється для сукупного `COLOR_TARGET|SAMPLER` usage. Actual swapchain format повторно читається після зміни presentation parameters. Native результат тут — `B8G8R8A8_UNORM`, SDL enum 12; final target — `R8G8B8A8_UNORM`. Blit відбувається вже після encode, не підмінює HDR conversion.
- World/final targets — RAII, окрема пара на кожен активний кадр у польоті (1–3). Resize створює повний candidate; лише після успіху й GPU idle замінює старі ресурси. Помилка allocation не залишає напівстворений active set.
- Додано shader interface/register/constant-buffer contracts і source/include hashes для нових програм. Negative tests мають чистий baseline і окремий pristine source/output tree на кожну мутацію; всі згенеровані fixtures зберігаються.

`LightingMode::Dynamic` досі недоступний, а `dynamic_mode_available=false` перевіряється native probe. Повний Reflection → GroundGBuffer → GroundLighting → WorldForward → Composite/FoW → UI graph **ще не реалізовано**.

## Актуальна перевірка

Фінальний [runtime snapshot](../../artifacts/hdr-color-2026-10-06/check-finalized-v3/snapshot.json): **319 hash-verified файлів**. Це output-isolated runtime, не hermetic source snapshot: Python tests і repository inputs залишаються live; transitive source inputs не заморожені.

10/10 CTest, 7.37 с: `shader_contract`, `shader_negative`, `hdr_color_contract`, `surface_lighting_contract`, `render_frame`, `render_batch`, `frame_timing`, `minimap_cache`, `presentation_contract`, `presentation_ui`. [Повний log цього вибраного прогону](../../artifacts/hdr-color-2026-10-06/check-finalized-v3/runtime/Testing/Temporary/LastTest.log). Це **не повний suite**. Зареєстрований `hdr_color_contract` перевіряє software capability/rejection, а не native HDR; native доказ наведений окремо нижче.

Свіжий [HdrColorProbe report](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/hdr-color-report.json): **actual process exit 0**, 34 фінальні RGBA8 PNG. D3D12, NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, build label `0.26.3`. `complete=true` у JSON означає завершення **цього probe**, не приймання двигуна чи версії гри.

| Контрольний вхід | Фактично прочитані RGB |
|---|---|
| sRGB gray128, exposure 1 | 128 / 128 / 128 |
| Colored clear 128 / 64 / 200, exposure 1 | 128 / 64 / 200 |
| Straight white, alpha128, над чорним | 188 / 188 / 188, не encoded-space 128 |
| Encoded premult white RGB128 / alpha128 | 188 / 188 / 188 |
| Premult input з alpha0 | 0 / 0 / 0 |
| Gray128 + additive white alpha64 | 182 / 182 / 182 |
| White albedo × ambient4, exposure0.125 | 188 / 188 / 188 — over-range світло збережено до Composite |
| Світ gray200: exposure1 → 0.25 | 200 → 106 |
| UI RGB31 / 157 / 219 при обох exposure | 31 / 157 / 219 без змін |

Зразки мають alpha255. Контрольні UI quad і непорожня glyph region при зміні exposure збігаються піксельно. Окремо пройшли sun/local surface-kernel response, lit → unlit → lit painter order, empty/world-only/UI-only frame, невалідні exposure/clear/pass order та явна відмова software backend.

Raw examples: [HDR ambient4](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/hdr-ambient4-exposure0125.png), [exposure1/UI](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/exposure1-ui.png), [exposure0.25/UI](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/exposure025-ui.png), [additive](../../artifacts/hdr-color-2026-10-06/native-finalized-v3/additive-white-alpha64-gray128.png).

## Lifetime, presentation і відновлення

- 40 послідовних HDR frames **без capture/readback** submitted/queued; sequence монотонний. Це exercise frame ownership, **не benchmark чи доказ scan-out/FPS**.
- Активні frame sets 1/2/3 перевірено status і декодованими known-color captures; потім відновлено 2.
- VSync, Immediate та Mailbox у цьому native запуску підтримувані: 3/3 applied, requested=effective, queued non-capture frame і known-color capture пройшли. Після них відновлено VSync/2. Unsupported cases у коді позначаються skipped, не successful; JSON не заявляє FPS-limit proof.
- Resize 640×480 → 800×600; minimize без capture не submitted/queued; явний minimized capture submitted offscreen без presentation; restore пройшов.
- Fault injections 5/6/8 — перша world, перша final і друга final texture нової пари наборів. Тест приймає лише точний injected error/context, не довільний runtime error. Старий status/footprint збережено; **обидва** старі slots повернено до 640×480, captured, decoded і перевірено quad RGB180/120/80 та opaque black clear. Resize-retry 800×600 теж перевірено піксельно.
- `attachment_bytes` — логічний розмір лише world RGBA16F + final RGBA8: `width × height × active_sets × 12`. Для двох наборів: 7,372,800 bytes при 640×480 і 11,520,000 при 800×600. Це **не driver allocation, не загальний/піковий VRAM** і не бюджет майбутнього восьми-target graph.

Додаткові native `RenderFrameTests --gpu`, `RenderBatchTests --gpu …`, `SurfaceLightingProbe …` завершилися actual exit0: text upload recovery, buffer growth rollback, batch barriers, попереднє bounded surface ядро (18 PNG). Їхні executable/shader hashes з `check-finalized` повторно звірено з фінальним `check-finalized-v3`: ідентичні. [Surface report](../../artifacts/hdr-color-2026-10-06/surface-finalized/surface-lighting-report.json), [batch capture](../../artifacts/hdr-color-2026-10-06/batch-finalized/surface-batch-barriers.png).

## Справжній GameView і порівняння з концептами

`ReworkVisualProbe --linear-hdr-world` — лише diagnostic opt-in після звичайного `GameView`/RmlUi compose; нормальний game path і launcher не переведено на неповний Dynamic. [HDR view manifest](../../artifacts/hdr-color-2026-10-06/hdr-view-02/manifest.json): 6 конфігурацій, 15 PNG, ordinary/staged/winter, native D3D12. Це не намальований background замість гри.

F1 подано як SDL key event у fixture: whole HUD/annotations приховано, потім повернуто; restored HDR PNG побайтово збігається з кадром до F1. [Без HUD](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-hud-hidden.png), [повернений HUD](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-hud-restored.png). Це native rendering/UI event proof, не ручне натискання клавіші через OS automation.

Окремий Legacy matrix також пройшов 6/6 конфігурацій: **15/15 PNG побайтово ідентичні** попередньому `artifacts/presentation-2026-10-05/legacy-finalized`. Це доказ відсутності візуальної регресії старого шляху, не приймання старої графіки як цільової.

Переглянуто raw [літній HDR кадр](../../artifacts/hdr-color-2026-10-06/hdr-view-02/en-100-1920x1080-staged-selected-stockpile.png), [зимовий HDR кадр](../../artifacts/hdr-color-2026-10-06/hdr-view-02/uk-85-1280x720-staged-winter-selected-stockpile.png), F1-hidden і контрольні color/rollback captures; зимовий кадр зіставлено з [концептом 0.26.1](images/target-0261.png).

**Visual acceptance не пройдено.** Видимі розбіжності лишаються: надто світла/пласка зимова світлова сцена, інша щільність і масштаб композиції, baked вікна без доведеного впливу на сусідні поверхні, немає причинних тіней мешканців/дерев/дахів, периферійна reveal-вуаль не є просторовим ранковим/водним туманом. HUD/forecast також ще не тотожні концепту; їхнє подальше оформлення чекає повного двигуна.

Ці PNG — прямий native GPU readback фінального target; **не фотографія/знімок scan-out монітора**, не temporal video. Native desktop API комп'ютерного керування в поточному інструменті недоступний; запуск і перевірка виконані native SDL probes, а зображення переглянуті без видавання їх за OS automation. Новий адміністративний PresentMon/UAC запуск на цьому кроці не виконувався.

## Прив'язка до файлів

| Фінальний input | SHA256 / content hash |
|---|---|
| Ribterra.exe | `66F469F1BDC716D50BFA59DB074BEC44B8A4882FD9F76BA9CC7D35E6B23E8120` |
| HdrColorProbe.exe | `0DD1E5DEFD926E3FF4BBEF1D98DD9FC7936C9A93730FFC72A1C77ABD092ADAA8` |
| ReworkVisualProbe.exe | `213AA0BA165D9D5A88DAC60E252054FE83809C1DB623B9960313A8D387FA030E` |
| compiled-shaders/manifest.json | `A122A404FB3E3C757837794DB557C6EFD3AB92E0EC20F80553A73DFE3DB05D81` |
| Asset content | `f08ed26b1989791ec7d24109ae9ec865b01c7f65a947ca619e97b3df18f4329b` |

GameView matrix binaries/shaders/assets у `check-02` hash-identical відповідним фінальним inputs; новіших змін production renderer після його matrix не було. Нормальний launcher досі вказує на `dist/concept-rebuild-0.26.3-preview-2026-10-05-r2/Ribterra/Ribterra.exe`. Це source/local validation, **не встановлена нова версія, commit/push чи release**.

Попередні staging directories, failed `check-01` copy та проміжні probes/snapshots збережено; файли не видалялися. Snapshot-copy failure під час linking не використано як green proof; остаточний snapshot створено після завершеної збірки. Build warnings у раніше змінених `view.cpp`/`game_ui.cpp` не оголошуються виправленими цим кроком.

Незалежний code review перевірив pass/lifetime/gamma/alpha/rollback scope. Виявлені прогалини probe — pixel proof обох retained slots, build provenance та послідовність читання diagnostics після `set_presentation` — виправлено до остаточного native прогону. Ponytail/ultrawork розділили implementation/evidence/review; karpathy-guidelines/cpp-coding-standards вплинули на локальний scope, typed state й transactional RAII. Це не замінює повного приймання.

## Наступний обов'язковий крок

Схема-2 матеріалів і мінімальний контрольний art kit: нейтральне albedo, normal/material, camera-depth, emission, receiver/reveal masks; узгоджені world anchor/projection/reconstruction для terrain та одного будинку/мешканця. Відсутній channel відхиляється транзакційно; schema-1 залишається явним BakedCompat. Потім — повний graph, emission/перешкоди/тіні, sun/moon/weather, вода й просторова атмосфера, спільна temporal concept matrix та профілювання повної сцени. До приймання двигуна генератор, механіки і масовий каталог/HUD не продовжуються.

Технічні опори: SDL [combined format/usage query](https://wiki.libsdl.org/SDL3/SDL_GPUTextureSupportsFormat), [SDR swapchain composition](https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition), [actual swapchain format після parameter changes](https://wiki.libsdl.org/SDL3/SDL_GetGPUSwapchainTextureFormat). Внутрішні вимоги: [Render Core](RENDER_CORE_SPEC_UA.md) і [повний поріг E01–E17](ENGINE_FIRST_ACCEPTANCE_2026_10_05_UA.md).
