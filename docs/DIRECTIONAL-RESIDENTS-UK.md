# Eight-way residents and visible cargo

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The game includes 256 new frames: male and female figures, eight directions, four walking phases, ordinary walking, and carrying logs, bags, and carts. Direction is determined by the resident's movement on screen. The animation freezes while paused, and a resident keeps facing the same direction after stopping. The previous animation slowdown is preserved.

The visible load corresponds to a real object in the hands of the resident, obtained from the server state. The wood and wood are shown by the logs. Food and small bags. Parts of six pieces of stone, clay, brick, stone, tools or writing — loaded with a trolley. After loading, the normal walk is returned. The resource badge at the shoulder is replaced by this chart.

The viaduct is still a form of transportation in effect: it does not increase capacity by more than ten units, does not accelerate residents, and has no separate transport physics. Change does not start a voluntary extraction. Transportation to the camp, storage and construction uses existing logistics tasks.

## Checks

- `UnitMotionTests`: all eight directions, directional retention after stopping, loading, offline/online smoothness on ×1/×2/×4, packet delays, pauses, transition between orbits.
- `LogisticsTests`: reservation, capacity, delivery, storage, cancellation and recovery.
- `ResidentVisualProbe`: real Direct3D 12, all 256 footage; the delivery of wood, food and stone to the camp and storage facility through a normal simulation; verification of the actual place of discharge and storage during transport. The test starting stock is specially designed for verification.
- `CoopPerformanceProbe`: two clients with a render, 180 frames, RTX, 3050 Laptop GPU: 59.97 FPS; mean time CPU for both clients together 9.50 ms, p95 12.31 ms. This is a short-term control scenario with 20 residents, not a guarantee of the speed of any settlement.

Pictures and reports: `artifacts/directional-residents/`. Source, built-in inputs image_gen, checksums and reproduced registration: `art-source/directional-residents/` and `tools/register_resident_art.py`.

The finished set: `dist/directional-residents/Ribterra/Ribterra.exe`. The launch is done using a label on the desktop `Ribterra - New Graphics`.
