# Незалежне пряме світло сонця та місяця — частковий engine proof

Дата:07.10.2026. **Частковий opt-in GPU крок, не готовий двигун/гра/release.**
Усі E01–E17 OPEN; GameView=BakedCompat, Dynamic unavailable. ≥120FPS у1920×1080
наRTX3050Laptop, художня відповідність концептам та повна сцена ще не підтверджені.
Gameplay/генератор/новий HUD/каталог/launcher не змінювали в цьому кроці.

## Реалізовано

`WorldGraph::SkyContactSlice` явно розширює GroundContactSlice. Сонце/ambient/чотири
locals залишаються в незмінному `SurfaceLightingFrame`384. Окремий `MoonLightingGpu`
має32bytes/align16: direction float4 offset0 (toward source, w0, length² tolerance.001),
normalized linear RGB/intensity float4 offset16 (RGB[0,1], intensity[0,64]). Нульова
інтенсивність вимикає внесок, але не валідацію. DefaultRGB.55/.68/1 — diagnostic,
НЕ затверджені художні кольори або calendar/time curve.

Frame має explicit optional Moon: Sky вимагає присутності, усі інші graphs відхиляють
її до command acquisition/frame-slot release; `clear_commands` її скидає. Software
не підтримує Sky graph. Відсутній shader variant провалює construction, не fallback.

Два нові програми: ground_sky_lighting (b0lighting384,b1Moon32) і material_sky_forward
(b0lighting384,b1Sprite160,b2ForwardMetadata32,b3Moon32). Окремий moon diffuse+bounded
specular не використовує sun_direction/color, ground sun_visibility, contact/AO,
local visibility/receiver mask або emission. Старий sun equation не змінено.
Emission залишається після illumination; UI — після Composite і поза sky світлом.
Surface_hdr не розширювали: цей graph забороняє surface_lit compatibility geometry.

Contact registry/pass/GBuffer tags лишаються ті самі. Seven attachments,
5empty-contact/6populated passes; нових render targets немає. Це НЕ вимірювання
VRAM/peak або продуктивності. Місяць тут ще **без authored directional shadows**.

## Поточні докази

Capsule:`build/sky-direct-2026-10-07-attempt03/`, snapshot/directMSVC
C++20/O2/MT/W4/WX/permissive/UTF8, свіжі renderer/contact/probe objects; не старі
Ribterra/Presentation/GameArt libraries. No CMake/fullasset staging/install/delete.

- MoonLightingTests:51checks PASS; ContactShadowTests:211checks PASS.
- Shader positive: exact DXIL bindings/ABIs/include closure/manifest PASS.
- Старі24DXIL+24reflection файли:48/48байт-у-байт незмінні.
- Native-01/02:по9груп actual checks,11pre-GPU invalid payload cases,26PNG кожний;
  D3D12/NVIDIA GeForce RTX3050Laptop GPU,driver32.0.15.9282,software=false.
- Sun/Moon протилежно освітлюють дві normal-поверхні землі та typed sprite fixture.
  Moon direction -X→+X причинно змінює освітлену сторону. Sun visibility1→0 має
  позитивну різницю; moon visibility1/0 кадри byte-exact. Contact позитивно затемнює
  ambient, але sun-only/moon-only pair byte-exact. Контролі local-only/emission-only
  мають позитивні сигнали за однакового exposure, UI bytes незмінні.
- Twilight combined перевіряється в linear space після зворотного sRGB з tolerance.025;
  A/B/B/A двослотові кадри й resize800×600→640×480 відновлюються byte-exact.
- Повторні SkyPNG:26/26byte-exact; старі ContactPNG:31/31byte-exact.
- Fresh selected RenderFrameTests:software/D3D12PASS, Frameclear Moon/contact reset;
  усі57stagedshader inputs незмінні. Це НЕ повний CTest/GameView suite.

`shader-negative-fixed-02.log.exit.txt`: actual exit0. Відхилено38register/space/output/ABI
mutations,14direct+1two-levelincludehash mutations,2noncanonicalincludeforms,
owner/capsule-axis/stride,2numerical і3moon-independence mutations. Final independent
source/oracle/evidence audit: **APPROVE тільки bounded direct-light slice**.
Нативний build label чесно `unversioned`, не0.26release; provenance задає capsule/hash.

## Provenance і збережені невдачі

Attempt01 відхилено черезC4324 intentional CPU-only LightState padding; suppression
локальне тільки довкола цієї wrapper, не wireABI/flags. Attempt02 compile пройшов,
але helper мав PowerShell `/out:` array-precedence link bug. Обидва roots збережені.
Attempt03 production build/CPU/shaderpositive/native пройшли, але початковий whole
recipeexit1 на nested-include test fixture (неоновлений transitive Sky contract).
Fixed01 negativeexit1 на повторній назві semantic-fixture directory. Нічого не
видаляли/не перезаписували й не перейменовували FAIL→PASS. Кожний repair script/log
має власне ім'я. Protected старі5EXE/manifest hashes після кожної невдачі незмінні.

## Візуальна перевірка та наступний крок

Переглянуто unmodified native readbacks sun-a0.png/moon-b1.png: теплий/холодний
внесок на протилежних normals і той самий червоний UI control. Це прості geometry
та1×1schema2fixtures, **не копії concept artwork і не перевірка категорій будинку,
дерева, людини/вантажу**. Native SDL diagnostic window/readback не є desktop scanout,
керуванням ноутбуком, відео руху або FPS acceptance.

Далі потрібні independent authored sun/moon silhouettes/visibility carrier і
time/weather curves, мінімальний справжній editable art kit, atmosphere/water/FoW,
presets/профілювання та GameView. ContactAO не можна пакувати в sun/moonvisibility;
GroundMaterial reserved lanes, local receiver bits і World.W не є дозволеним
прихованим moonshadow каналом. До повного engine gate гра чекає.
