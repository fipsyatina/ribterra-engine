# Contact shadows — CPU prerequisite, 07.10.2026

Статус: **CPU-КОМПОНЕНТ ПЕРЕВІРЕНО; GPU/ART/ENGINE НЕ ПРИЙНЯТО**.
Усі E01–E17 відкриті. GameView/launcher лишаються чинними; жодного нового кадру гри, контактної тіні або результату120FPS цей checkpoint не доводить. До gameplay/generator/HUD/catalog не переходили.

## Що фактично реалізовано

- [contact_shadow.hpp](../../include/ribterra/contact_shadow.hpp): typed receiver/facing/art handle, ContactSurfaceGpu80/offsets0–64, prepared Draw112 із explicit zero padding. Це окремий CPU контракт, не reinterpretation довільного local receiver bit.
- [contact_shadow.cpp](../../src/render/contact_shadow.cpp): read-only validator, finite/range checks, reveal1, opacity`(0,1]`, nonzero art generation/stable/owner, unique stable IDs, known facing, reserved-zero, normalized/orthogonal tangents, full off-centre footprint плюс normalized-normal plane band у±32. Owner може повторюватися для кількох parts. Ненульова generation **не** доводить resource liveness.
- `contact_shadow_corners()` зберігає worldZ/pivot/elevated plane; майбутній bounded-scissor path має використати ці corners, але projection/scissor ще не реалізовано.
- Explicit conservative renderer slope policy: normal.z≥.5 (≤60° від horizontal), не зміна terrain traversal/buildability. Plane tolerance`[1e-4,.04]`, half extents≥1e-4; numerical bounds **не** є доказом відсутності native pixel leakage.
- Prepared cap128 provisional/unmeasured; overflow reject без truncation. Майбутній art adapter має відсіювати hidden/exact zero-opacity casters доIDs/cap; invalid opacity — error. Цей adapter ще відсутній, validator hidden/zero prepared entries відхиляє.
- [CMakeLists.txt](../../CMakeLists.txt) реєструє isolated ContactShadowTests/contact_shadow_contract. CMake regeneration/CTest запуск нового target у цьому checkpoint **не виконано**; тест скомпільовано й запущено безпосередньо MSVC.

## Фактичні перевірки

Докази: [cpu-contract-01](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01).

1. Перша команда запуску компілятора мала quoting error, exit1, компілятор не стартував; окремий compile log не створився. Наступний compile-02.log має реальний C4324/exit2 (implicit padding); його збережено, не названо PASS.
2. Явний reorder/zero padding усунув warning. Проміжний compile-03.log/test-01.log:149check() assertions PASS; ці sources/exe збережено в before-review до наступних виправлень.
3. Незалежний review знайшов zero-opacity cap issue, near-vertical cliff issue й W/coverage ambiguity. Усі виправлено: zero reject, normalized normal.z≥.5 із boundary/nextafter±/near-vertical tests, Ordinary naming, versioned Core/WFX semantics.
4. [compile-final-01.log](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01/compile-final-01.log): MSVC C++20 `/W4 /WX /permissive- /O2 /MT`, clean, exit0. [test-final-01.log](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01/test-final-01.log): **157check() assertions PASS**, exit0. Це assertion count, не157native cases: включає negative rejection та input-byte immutability assertions.
5. Позитивні: flat, off-centre pivot, slope, elevated anchor, exact min extent, exact/nextafter supported slope boundary, всі9facing enums, empty list,128unique IDs/same owner, ordinary/contact tag helpers. Негативні: NaN/Inf у всіх20float lanes, invalid reveal/identity/generation/facing/padding, opacity, UV/pivot/plane-band/extents/axes, bounds, downward/vertical/near-vertical plane,129overflow і duplicate stable IDs. Unknown receiver enum rejected.
6. Final executable SHA256: `DC1F27A26F74830A140D8C0BC432D3927E7A4F989B79D60874694006293D5A6A`. Новий final output не перезаписав проміжний executable.
7. Незалежний `/root/ground_review_oct7` re-review: **APPROVE тільки CPU prerequisite**; no GPU/art/performance/full-engine approval. Висновок збережено окремо в independent-review.md.
8. Повторний запуск final executable:157assertions/exit0, test-repeat-final-01.log. cpu-final-ledger.json фіксує current/final source copies4/4 match і reviewed EXE hash. Copies захоплено після compile/review; це provenance listed source bytes, не hermetic/frozen toolchain proof. `git diff --check` для змінених tracked files — PASS.

## Receiver contract та незмінні старі докази

Чинний GroundForward W0/1/old5passes та всі8local receiver bits збережені; renderer/shaders/art/assets/runtime binaries у цьому diff не змінені. Нова typed CPU mapping Ordinary→1, Flat/Slope/Bridge→2 ще **не** підключена до GPU. Core§5.2/WFX§5.3 описують proposed окремий GroundContactSlice V1, opaque nearest W0/1/2; Contact буде тестувати exact2, новий Lighting тільки{1,2}. Це не coverage/blend weight.

Contact V1 не має Reflection/Water. Для подальшого full engine потрібен окремий explicitly versioned antialiased coverage carrier і нові memory/resize/native-edge/performance докази; carrier OPEN. Жодну full-engine функцію не скасовано.

Перед edits збережено working CMake/Core/WFX копії в preserved. Старі [LocalOcclusion native/CTest докази](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md) не переоголошуються новою перевіркою contact. Новий prior-inputs-after-cpu.json **PASS**: runtime/source356/356, command inputs26, diagnostic runtime/source3/3, CTest2, shader sources/includes30, compatibility6 listed hashes незмінні після CPU кроку. Linked inputs не є immutable або hermetic; unlisted transitive/toolchain inputs не заморожені.

## Що не зроблено і наступний gate

GPU pass/shader bindings, art registry/linearR8/whole-texel UV validation, editable contact masks/building/tree/resident/cargo kit, art adapter filtering/rollback, resource lifetime/generations, projection/scissor contract, native causal ambient-only change, receiver clipping/motion/resize/fault controls і performance — **ще не реалізовані/не прийняті**. Статичні CPU fixtures не є artist/concept/screen acceptance. Ручний UI/екран прогін не проводився: візуальний runtime у цьому кроці не міняли; native control APIs у середовищі недоступні.

ДискC має приблизно0.5GBheadroom, недостатньо для isolated fullGPU build/staging/evidence. Погоджене видалення incomplete local-occlusion/check-final-02 раніше двічі заблоковано до запуску команди; нічого не видалено, обходу блокування немає. Нові compile/test outputs малі й окремі. Старі accepted hard-linked inputs не перебудовувалися.

Наступний engine-only крок — exact projection/registry contract, далі GPU/art contact integration після достатнього диска, не повернення до гри. Full acceptance та user≥120FPS1920×1080RTX3050Laptop залишаються відкритими.
