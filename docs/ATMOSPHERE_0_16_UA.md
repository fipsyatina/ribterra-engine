# Ribterra 0.16.0 — Biomachines, fog and review

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The textures of snow, rock, grass, and sand are blended across a band several cells wide. Neighboring areas use the same values at shared vertices, so cell and chunk boundaries do not break the transition. Water remains within lakes and rivers.

The vertical rectangular fog curtains are removed. The limit of visibility is now gradually fading on the relief itself; The buildings, the trees, and the people around it are also gradually disappearing. The presence of a mist mask is used as a weak layer of serpentine on the surface. Hidden biomes and heights do not affect the image.

## Distance of view

| The source of the intelligence | It was. | It's become |
|---|---:|---:|
| New Camp | 7 | 10,5 cells |
| Resident while on the move | 3 | 4,5 cells |
| The mine entrance is completed | 4 | 6 cells |

Each radius is multiplied by the same amount of times 1,5. The form of the probe is a circle, taking into account the radius of the pulse. New values for residents apply during movement also in old preserves.

## Why the Inhabitants Lost Their Health

On the screenshot sent, the food supply is zero. The current feeding mechanism checks available, unreserved food on the stocks every 200 tact — 10 seconds at the rate of 1×. Without a portion of hunger increases by 1, in winter — by 1,5. From the level 5/10 every missed meal takes away 1 health.

The hunger rules are preserved. A warning is added when food is not in stock or the inhabitants are hungry, and an explanation of the reason is added on the inhabitant card. Food on the ground or in the cargo still needs to be delivered to the warehouse. Nutrition stops the damage and gradually reduces hunger. There is no automatic recovery of lost health.

## Compatibility and verification

Game version — 0.16.0, rules 10, network protocol 12. The generator remains 5, the storage format — 8. Support for 0.14/0.15 and previous migrations is preserved. Texture blending also works on previously generated maps. A shared game requires a client and a server of the same version.

A separate test tested the smoothness and normalization of the mixture, the absence of hidden cell exposure, the fog limit, both detection radii, the absences of damage to the inhabitants, the hunger thresholds and the cessation of damage after feeding.

Native scenes were rendered on Direct3D 12 with an NVIDIA GeForce RTX 3050 Laptop GPU. Real generated biomes with a revealed test area were used to review the transitions. The hunger warning was verified at 1440×900 and 1100×700.

- [Transition from rock to snow](../artifacts/atmosphere-release/snow-transition.png)
- [The boundary of the desert](../artifacts/atmosphere-release/desert-transition.png)
- [Expanded view and mild fog](../artifacts/atmosphere-release/expanded-view.png)
- [Explanation of the famine](../artifacts/atmosphere-release/starvation-compact.png)
- [Report GPU](../artifacts/atmosphere-release/render-report.json)
- [Tests](../artifacts/atmosphere-tests.log)

All 60 unique tests have a successful result after updating the historical review check. In 25 real v7 storage, all fields remain unchanged during migration. To compare 64 the following moves with the frozen old move, the test reproduces the old reconnaissance mask and removes only the additional open cells from the revisions calculators. The new mask is checked to ensure that the old one is all there. The rest of the historical fields are completely compared. Re-loading a new format compares a full state to a new review without normalization.

- [Summary of 60 tests](../artifacts/atmosphere-release/test-verification.json)
- [Repeatedly checking the records](../artifacts/atmosphere-final-tests.log)
- [Details of historical continuations](../artifacts/atmosphere-release/legacy-continuation-details.log)

ZIP 0.16.0 is packed into a new folder. EXE corresponds to a verified set, all 86 assets correspond to the control amounts of the manifest. Launch of the package on Direct3D 12 is successful; The regular launcher is switched to 0.16.0. [Checking the packet](../artifacts/atmosphere-release/package-verification.json).
