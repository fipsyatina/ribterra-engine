# Ribterra 0.26.3 — local concept rebuild preview for Windows x64

This is a locally built preview of the complete V01–V03 rebuild. It is not a published release and has not yet been accepted as an accurate match for the concept art. As of October 5, 2026, it has passed 92/92 local tests, 60/60 native D3D12 render configurations, and 8/8 ordinary fixed-seed starts. Visual parity remains incomplete. Current evidence and limitations are documented in `BACKGROUND_CHECK_2026_10_05_UA.md` and `CONCEPT_PREVIEW_LIMITATIONS_UA.md`. The `artifacts/...` paths and HTML comparisons referenced by those reports belong to the repository checkout, not to the standalone portable package.

## Run the local preview

1. Extract the entire locally built package into one folder.
2. Keep `Ribterra.exe`, the DLLs, `assets`, and `licenses` together.
3. Run `Play Ribterra.cmd` from that folder, or run `Ribterra.exe` directly.
4. Check the window title. It must show `Ribterra 0.26.3`.

Do not use an older public package as evidence for this rebuild. This README describes only the freshly built local concept rebuild preview.

## New games and world generation

- A normal new game starts with one Camp and 10 residents. The developed settlement shown in validation captures is a separate staged scene, not the player's starting state.
- Every new game created interactively receives a new random seed. Use `--seed <number>` to make generation reproducible for diagnostics and screenshot comparisons.
- New worlds use generator profile 7: a more winding river, elevation-based terrain, more natural mixed resource groups, and a viable starting area.
- Supported older saves are not regenerated. They retain their own generator profile and original terrain.
- Players cannot construct roads manually. Residents gradually wear visible paths only by actually walking over terrain; paths provide no hidden speed bonus.

## Basic controls

- Left-click to select a resident or object; hold Shift to add to the selection; press A to select all residents.
- Right-click to order movement or manually gather wood, stone, or wild food. Shift + right-click adds the order to the queue.
- Press H / F / B to place a House / Farm / Stockpile. Available buildings are also accessible through the **Build** button.
- Use the arrow keys or middle mouse button to move the camera, the mouse wheel to zoom, and C to center the settlement.
- Press Page Up / Page Down to show the surface or mine. Changing the view does not move units.
- Press Space to pause; press 1 / 2 / 3 for 1× / 2× / 4× speed.
- Press F1 to hide the entire HUD, including markers; press it again to restore the HUD.
- Press F5 to save the local world, F9 to load it, and F12 to capture a PNG screenshot.

## Saves

Local files are stored in `%APPDATA%\Ribterra\Ribterra`. The current formats are save 8, rules 18, and generator 7. Before testing an important older world, make a separate copy of its file. Do not overwrite the only backup with a new save until you have verified the world in the game.

F5 and F9 control the local world. The host or dedicated server saves a remote world; local F5/F9 controls are unavailable for it. Loading a supported older world must not change its generator profile or rebuild its terrain.

## Cooperative play

The client and server must use protocol version 21 and identical rules and content. Mixed local packages can be incompatible even if both windows display version 0.26.3.

Local network checks do not replace testing on two physical Windows PCs. The external “two PCs over a real network” scenario has not yet been confirmed for this concept rebuild preview, so the package must not be presented as a verified cooperative release.

## Intentionally outside V01–V03

The four-day forecast, temperature, rain, storms, and the full dynamic lighting system belong to V04 and later stages. The preview must not display fabricated weather values from the concept art. Their absence from 0.26.3 marks the boundary of this stage; it does not demonstrate a completed weather system.

A Direct3D 12-capable graphics card is required. Check the documentation included with the freshly built local package for the final visual-parity status, test results, and known limitations.
