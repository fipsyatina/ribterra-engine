# Professions and work speed in 0.9

> English translation of a historical project document. Results apply to the named versions and snapshots. The original wording is retained in import commit `7388a2f`; translation does not validate the current build.

Status: local, GPU, package, and CI checks for 0.9 at `d3bc785` passed. After initializer corrections, repeated CI for `42036ff` passed without C++ warnings or UBSan errors. [VALIDATION.md](VALIDATION.md) distinguishes the source from the unchanged Windows package. Remaining profession, manual-control, and complete Utility AI requirements are in the [engine registry](ENGINE_REQUIREMENTS.md).

## Experience and practice

There are eight independent skills: woodcutting, mining, farming, construction, hauling, smithing, medicine, and combat. A skill record does not mean every corresponding mechanic is implemented. Existing wood/mining/farming XP remains the single value for each of those three skills; the additional five have separate fields. Profession does not overwrite experience.

Curves and timing parameters are in [data/skills.json](../data/skills.json). The build validator generates a shared C++ table for server and simulation. Canonical JSON contributes to the gameplay content hash. Data changes require rebuilding and restarting, compatible versions, and save migration. A runtime file cannot silently change the rules of an active world.

One game day has 1200 ticks. Useful work uses integer units: 100 units is one standard work tick, and 4000 units is one XP. A fully productive day yields 30 XP. The current curve is 0 XP → −8%, 30 → −4%, 120 → 0%, 480 → +5%, 1440 → +12%, 2880 → +15%, with integer linear interpolation between points. These are initial balance data, not the finished balance of the entire game.

After three days without practice, familiarity slowly decays. Over the next ten days, the loss reaches a maximum of 2 percentage points. One quarter of a standard workday restores it completely. XP never decreases. An unused skill does not lose invented practice. Requesting a tooltip changes neither state nor timestamp; zero useful work does not restore familiarity.

All relevant skills and abilities are added together first, followed by familiarity, then **one** −1500…+1500 basis-point cap (−15…+15%). The server returns individual contributions, the pre-cap sum, applied effect, speed, and a capped flag. The UI displays that result; there is no separate client XP formula. +15% speed means approximately −13% time, not −15% time.

## Useful-work boundary

Gathering grants XP only after physical output is successfully created. Waiting, route admission, repeated commands, retry, and delivery create no additional harvest or XP. Construction credits only the progress actually added, including the final partial increment; the fractional learning remainder is saved. A passive Farm without a worker grants no XP to an arbitrary resident.

Profession/priority assignment and Stop return an outcome for each group member. A new assignment is recorded immediately; execution changes after the current protected physical delivery. Deferred marks that wait and retains the accepted assignment. Stopping one participant does not cancel a shared external pending path for the entire group: that resident's new policy waits for the search-completion boundary. Old Move/Gather still have an atomic group reply at this stage. Complete partial routing, manual-order queues, and transferable control rights remain required follow-up work.

Executable professions in this slice are woodcutter, miner, forager, builder, and carrier. Unassigned residents retain the previous automatic construction/hauling assistance. Farmer/Smith/Medic/Guard do not yet have complete work mechanics, are absent from the assignment form, and report no suitable work at the server boundary. Complete Utility AI with needs, risk, social causes, and alternative selection after an unreachable target is unfinished.

## 0.9 validation

The full local CTest run passed 46/46 in 126,97 seconds. After the cosmetic hover/focus separation, all 11 relevant UI/render/localization scenarios passed again in 40,06 seconds. The benchmark contains 25 successful stages for 10–6000 residents. CI `d3bc785` passed Windows 46/46, Linux 30/30, and UBSan 10/10; GCC reported omitted initializers. Adding them explicitly in `42036ff` preserves semantics. The focused local 6/6 and repeated CI passed without C++ warnings/UBSan errors. Details, limits, and exact results are in [VALIDATION.md](VALIDATION.md).
