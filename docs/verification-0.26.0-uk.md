# Checking 0.26.0 — Materials and improvements

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

24 September 2026, the local Windows collection MSVC, `AInora/first-playable`.

## The usual start through the interface

`UpgradeFlowProbe` completed the same scenario twice through SDL, GameView, and RmlUi: in **1440×1000** and **1100×700** windows. The initial world has a Camp, 10 residents, 80 wood, 40 stone, and 100 food. The world is not imported from a prepared state; the test does not add items or completed buildings. Simulation time is accelerated by running ticks; clicking, selection, construction, recipes, and upgrades all use the game interface.

1. Residents gather wood and stone. The Sawmill, Clay Quarry, Workshop, and House are built through the catalog and map.
2. When boards and bricks are missing, the panel shows a clear shortage. An accepted upgrade waits for materials; canceling it releases the reserved materials.
3. 12 boards, 6 firewood, 12 clay, and an initial 8 bricks are produced and delivered.
4. The Workshop reaches level II for 6 boards and 4 bricks. Save and load are performed through the UI during the work phase; the economy state matches afterward.
5. Another batch produces 4 bricks. The measured work phase decreases from **400 to 200 ticks**; delivery time is not included in this measurement.
6. The House reaches level II for another 6 boards and 4 bricks; settlement capacity increases by **4 places**. The total cost of both upgrades is 12 boards and 8 bricks. The remainder is 0 boards and 4 bricks.
7. A fifth type, the Warehouse, is built. Its panel does not show Warehouse II. Reissuing the order for the completed House II does not change materials, the order, or the upgrade. Final save and load preserve the economy state.

Both runs complete at tick 8541. 13 frames and a text report from each run are stored in `artifacts/upgrades-0.26.0/` and `artifacts/upgrades-0.26.0/compact/`. The compact-window price and button, work progress, House II, and absence of Warehouse II were reviewed. This is an automated scenario using the real interface, not a manual playthrough. The initial food runs out during the scenario; the test does not prove balanced long-term settlement survival.

## Order life cycle

The separate `UpgradeTests` uses prepared simulation worlds to accurately enter border states. For each **** of the two improvements, the cancellation of the immediate, to lift the load, with the load in hand, in the middle of the work and before the last working stroke of the  **10 scenarios** was verified.

- The cargo in hand completes the delivery. All 6 tablets and 4 bricks remain physical objects; Unfinished work returns the materials to the structure. The cancelled improvement does not provide a level of II.
- Re-cancellation does not return the material a second time. The freed worker takes on the following task.
- Savings and recovery at different stages yield the same control amounts and continue to change the same. Two storage/loading cycles were performed amidst successful improvements.
- A simultaneous repeat order with another worker, a foreign nation order, and a repeat upgrade of the finished II level deviate without changing the world. Successful improvements have the same price tag.

## Automatic checks and Windows packages

The final result of each of the **18 CTest — selected is successful**: upgrades, upgrade_flow, economy, production_control, production_control_ui, building_inspector, alpha_rules_ui, input_feedback, client_input, ui, ui_lifecycle, localization, save_compatibility, save_compatibility_ui, coop_start, server, lobby, protocol.

The first parallel launch gave 16/18: the old UI- test mistakenly expected a hidden improvement of the Workshop; The expectations are renewed. `coop_start` ended with a timeout of the synchronization of the image; the individual repetition was performed over 10,08 p. The cause of the instability of this parallel launch is not established. The final launch of the 10 UI/localization tests passed for 54,94 s, the enhanced improvement test for — for 13,39 s. The logos are `build/0260-tests.log`, `0260-ui-final.log`, `0260-coop-final.log`, `0260-upgrade-final.log`; the aggregate — `artifacts/upgrades-0.26.0/test-results.json`. The full set of CTest didn't start.

The fixed EXE and the standard `Play Ribterra.cmd` separately completed `--smoke` with the code **0**: **0.26.0, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, software=false**. `dist/current-build.txt` indicates that 0.26.0. The images and the renderer data are stored alongside the passing evidence.

ZIP: `dist/Ribterra-Windows-x64-0.26.0.zip`, **158820820 bytes**. SHA256: `6eac355b182815b9887f102f7712a6e592e3ac63414ceec92fb342f38d6a0609`. CRC verified; four EXE coincide between the set, the installation and ZIP. UI, five languages, launcher, README and the update description match the sources. The seven hidden buildings are preserved. The proof is  `artifacts/upgrades-0.26.0/package-verification.json`.

The rules 17, protocol 19, file format 8 and generator 5 remained compatible. Verification on two physical Windows PCs ** failed**: the second PC is unavailable, the user reported. The change in housing influence on population reception is due to 0.26.1. GitHub push and release didn't work.
