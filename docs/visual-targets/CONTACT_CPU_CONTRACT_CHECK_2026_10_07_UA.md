# Contact shadows — CPU prerequisite, 07.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: **CPU-COMponent has been reviewed; GPU/ART/ENGINE Not allowed **.
All E01–E17 are open. GameView/launcher remain in force; No new game footage, contact shadow or 120FPS result this checkpoint does not prove. They didn't go to gameplay/generator/HUD/catalog.

## What Really Happened

- [contact_shadow.hpp](../../include/ribterra/contact_shadow.hpp): typed receiver/facing/art handle, ContactSurfaceGpu80/offsets0–64, prepared Draw112 with explicit zero padding. It's a separate CPU contract, not a reinterpretation of an arbitrary local receiver bit.
- [contact_shadow.cpp](../../src/render/contact_shadow.cpp): read-only validator, finite/range checks, reveal1, opacity`(0,1]`, nonzero art generation/stable/owner, unique stable IDs, known facing, reserved-zero, normalized/orthogonal tangents, full off-centre footprint plus normalized-normal plane band at±32. Owner can be repeated for several parts. Non-zero generation **ne** proves resource liveness.
- `contact_shadow_corners()` keeps the worldZ/pivot/elevated plane; The future bounded-scissor path should use these corners, but projection/scissor hasn't been implemented yet.
- Explicit conservative renderer slope policy: normal.z≥.5 (≤60° from horizontal), no change in terrain traversal/buildability. Plane tolerance`[1e-4,.04]`, half extents≥1e-4; numerical bounds **no** is proof of the absence of native pixel leakage.
- Prepared cap128 provisional/unmeasured; overflow rejection without truncation. The future art adapter will have to seed hidden/exact zero-opacity casters to IDs/caps; invalid opacity — error. This adapter is still missing, validator hidden/zero prepared entries rejects.
- [CMakeLists.txt](../../CMakeLists.txt) registers isolated ContactShadowTests/contact_shadow_contract. CMake regeneration/CTest launching a new target at this checkpoint ** failed**; the test is compiled and run directly MSVC.

## The actual checks

The evidence is that the [cpu-contract-01](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01).

1. The first command to start the compiler had quoting error, exit1, the compiler did not start; There's no separate compile log. The following compile-02.log has a real C4324/exit2 (implicit padding); It's not named PASS.
2. The explicit reorder/zero padding removed the warning. Intermediate compile-03.log/test-01.log:149check() assertions PASS; These sources/exe are saved in the before-review until further corrections.
3. The independent review found zero-opacity cap issue, near-vertical cliff issue and W/coverage ambiguity. All corrected: zero reject, normalized normal.z≥.5 with boundary/nextafter±/near-vertical tests, Ordinary naming, versioned Core/WFX semantics.
4. [compile-final-01.log](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01/compile-final-01.log): MSVC C++20 `/W4 /WX /permissive- /O2 /MT`, clean, exit0. [test-final-01.log](../../artifacts/contact-shadow-2026-10-07/cpu-contract-01/test-final-01.log): **157check() assertions PASS**, exit0. This is assertion count, not 157native cases: includes negative rejection and input-byte immutability assertions.
5. Positive: flat, off-centre pivot, slope, elevated anchor, exact min extent, exact/nextafter supported slope boundary, all9facing enums, empty list,128unique IDs/same owner, ordinary/contact tag helpers. Negatives: NaN/Inf in all20float lanes, invalid reveal/identity/generation/facing/padding, opacity, UV/pivot/plane-band/extents/axes, bounds, downward/vertical/near-vertical plane,129overflow and duplicate stable IDs. Unknown receiver enum rejected.
6. Final executable SHA256: `DC1F27A26F74830A140D8C0BC432D3927E7A4F989B79D60874694006293D5A6A`. The new final output did not overwrite the intermediate executable.
7. Independent `/root/ground_review_oct7` re-review: **APPROVE only CPU prerequisite**; no GPU/art/performance/full-engine approval. The result is stored separately in independent-review.md.
8. The final executable is rebooted: 157assertions/exit0, test-repeat-final-01.log. cpu-final-ledger.json fixes the current/final source copies4/4 match and reviewed EXE hash. Copies captured after compile/review; This is provenance listed source bytes, not hermetic/frozen toolchain proof. `git diff --check` for modified tracked files — PASS.

## The receiver contract and the immutable old evidence

The actual GroundForward W0/1/old5passes and all8local receiver bits are saved; renderer/shaders/art/assets/runtime binaries in this diff are unchanged. The new typed CPU mapping Ordinary→1, Flat/Slope/Bridge→2, **not yet ** is connected to GPU. Core§5.2/WFX§5.3 describe the proposed single GroundContactSlice V1, opaque nearest W0/1/2; Contact will test exact2, the new Lighting only{1,2}. It's not coverage/blend weight.

Contact V1 does not have Reflection/Water. For further full engine requires a separate explicitly versioned antialiased coverage carrier and new memory/resize/native-edge/performance evidence; carrier OPEN. No full-engine function has been cancelled.

Before edits are saved working CMake/Core/WFX copies in preserved. The old [LocalOcclusion native/CTest evidence](LOCAL_OCCLUSION_CHECK_2026_10_07_UA.md) is not overstated by the new contact check. The new prior-inputs-after-cpu.json **PASS**: runtime/source356/356, command inputs26, diagnostic runtime/source3/3, CTest2, shader sources/includes30, compatibility6 listed hashes are unchanged after the CPU step. Linked inputs are not immutable or hermetic; unlisted transitive/toolchain inputs are not frozen.

## What not done and the next gate

GPU pass/shader bindings, art registry/linearR8/whole-texel UV validation, editable contact masks/building/tree/resident/cargo kit, art adapter filtering/rollback, resource lifetime/generations, projection/scissor contract, native causal ambient-only change, receiver clipping/motion/resize/fault controls and performance — ** have not yet been implemented/accepted**. Static CPU fixtures are not artist/concept/screen acceptance. Manual UI/screen scan was not performed: visual runtime in this step was unchanged; native control APIs are not available in the environment.

The diskC has about 0.5GBheadroom, not enough for isolated fullGPU build/staging/evidence. The agreed removal incomplete local-occlusion/check-final-02 was previously blocked twice before the command was run; Nothing is deleted, no blockage bypass. The new compile/test outputs are small and separate. The old accepted hard-linked inputs were not rebuilt.

The next engine-only step is — exact projection/registry contract, then GPU/art contact integration after sufficient drive, no return to play. Full acceptance and user≥120FPS1920×1080RTX3050Laptop remain open.
