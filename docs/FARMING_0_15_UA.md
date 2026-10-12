# Ribterra 0.15.0 — work on the farms

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The farm no longer produces food by the timer. The harvest requires a resident who comes to the finished farm, re-works the cycle, and delivers the food to the store.

## How to Designate Farmers

1. Build a farm and wait for the construction to be completed.
2. Pick up the residents and press the right button on the farm. It's a job for farmers and this farm.
3. Or open Profession and Skills», select Farm and dial automatically. The worker will look for a farm available; You can also choose a specific job.

Two people work on a farm at the same time. Location is also taken into account when driving and delivering the crops. If the farm is occupied or unavailable, an additional worker is waiting.

## Work and Harvest

- One cycle gives 8 units of food after 400 basic working times, approximately 20 seconds per 1× speed. The farming habits change the pace. The road and the delivery will take extra time.
- The worker must physically reach the cell near the farm. The movement of the tool and the progress bar are visible during the work, and the resident card shows a percentage of the yield.
- Agricultural skills are developed by the work done. Expectations and inaccessible farming are not experiences.
- The food we gather becomes a physical burden. The farmer takes it to the warehouse and returns to work. The existing rules on the capacity of goods and land-based items shall be maintained.
- An empty, unfinished or unavailable farm does not produce food.
- A breeding stop cancels an unfinished cycle. The stop with the harvest in hand first completes the delivery and then stops working.
- Storage in the middle of the job restores crop progress and further logistics.

## Compatibility

Game version — 0.15.0, rules 9, generator 5, network protocol 11. The storage format remains 8. Support for 0.14.0 and previous migrations is retained; When the old farms are up and running, they also need workers. For a shared game, the client and server must have the same version.

## Proof of verification

Automatic scenarios check the empty farm, work on site, physical yield, delivery, limit of workers, unavailable workplace, stop, network coding, and determined continuation after storage.

A native verification of Direct3D 12 on NVIDIA GeForce RTX 3050 Laptop GPU built a farm, assigned people a right-click image of the building, and performed a work-and-delivery cycle. Interfaces 1440×900 and 1100×700 have been revised.

- [Working on the farm](../artifacts/farming-release/farm-working.png)
- [Profession of farmer](../artifacts/farming-release/farmer-profession.png)
- [Crop delivery](../artifacts/farming-release/farm-harvest-delivery.png)
- [Compact interface](../artifacts/farming-release/farm-working-compact.png)
- [Native inspection report](../artifacts/farming-release/farming-render-report.json)
- [Basic test scores](../artifacts/farming-tests.log)
- [Repeated checks after corrections](../artifacts/farming-final-tests.log)

After re-checking the two updated expectations and adding a native script, all 58 unique tests have a successful result. [Setting the results](../artifacts/farming-release/test-verification.json).

The ready ZIP 0.15.0 is packaged in a new folder: the check sum EXE matches the verified set, all 86 assets match the manifest, launch on Direct3D 12 has been completed successfully. [Checking the packet](../artifacts/farming-release/package-verification.json).
