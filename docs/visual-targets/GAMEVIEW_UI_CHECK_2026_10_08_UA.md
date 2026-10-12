# Native RmlUi, menu and typed World — check 2026-10-08

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status: This limited UI checkpoint is adopted after fresh strict assembly, two actual GPU- launches, and independent review. The full target remains active. The usual game launch, reference art, all the panels and FPS more OPEN.

Build `gameview-ui-2026-10-08-attempt03`: 178 sources, 24 translation units, 122, actual commands from exit 0; 554 files / 132 383 967 bytes. EXE: 47 034 880 bytes, SHA-256 `E3AED475E84A97B7C753F789843AB961FBBFE9C423090DAD81F642D8AFD56618`. Native `attempt02` and `attempt03` completed exit 0 on Direct3D12 / NVIDIA GeForce RTX 3050 Laptop GPU, driver `32.0.15.9282`, 1920×1080. Each run has 20 files: 31 112 220 / 31 112 219 bytes.

Parent and independent read-only verifier passed: 178 sources / 1245 bound files / 122 zero commands / 16 accurate derived outputs. Independent hash review: 3032 rows of attachments, 1245 unique files, 0 discrepancies. All 15 PNG and raw WaterCoverage repeated byte to byte; actual save with UUID is tied separately. The report was repeated except for the measured time of the entire diagnostic run.

`Frame2D::content` clearly distinguishes `World` and `UiOnly`, by default `World`. Renderer checks for the absence of World parameters in `UiOnly` and uses the existing SDR- path UI, storing the selected cumulative graph for the next World frame. A blank wrong World frame does not become UI automatically. `clear_commands()` returns the original `World`.

RmlUi now adds the canonical `SurfaceVertex{}` to its own vertices if the World prefix already has an aligned typed/HDR stream. The wrong prefix is deleted, the previous data is kept. Legacy prefix without companion stream keeps its convention. The intermediate-top buffer re-uses the capacity between draw calls.

## Resources and sources

Two separate UI kits were created: `build/engine-ui-kit-2026-10-08-attempt01` and `attempt02`. Each contains 50 original resources, a new manifest and a separate report — 52 files, 26 580 243 bytes. All 52 files were byte byte. Verified original sources, metadata, four license notices, RML/RCSS references and 11 ready building portraits; parent oracle checked 164 bound files. An attempt to re-export to an existing folder is rejected with an actual Python exit 2, after which the files remain unchanged.

GameUi uses its own AssetCatalog of this UI kit; typed GameView uses a separate diagnostic control kit. The full production catalog is not copied. The original `art/world.json` in the UI kit also contains pages outside this subset, so the kit is intended for the menu/HUD and the specified portraits, not for all panels or the entire world.

The collection actually uses 24 translation units, installed RmlUi 6.3 Core from FreeType and their static dependencies, 23 shader programs / 46 stages / 93 fresh shader artifacts. SDK headers/configs and all link libraries checked by way of and hash before/after collection; large SDK-libraries were not copied. Each fresh folder is limited to 128 MiB, with a minimum free space  384 MiB.

## The actual verification

The probe created the equals 15 PNG, one actual World save, one raw WaterCoverage and `ui-report.json`. Runner added two actual execution files. There were 14 positive controls, 25 renderer negatives and four RmlUi prefix negatives. The wrong frame did not change the targets or completed submission; The next correct frame was rendered. There is no separate acquisition counter API, so no such verification is claimed.

Verified authentic Ukrainian fonts/menu/HUD, premultiplied textured UI/scissor/text, optional neutral companions, full World-prefix and neutral UI suffix, World → UiOnly → World, retained targets. Presentation depths 1/2/3 supported in fact, all relevant ring slots visited. Odd resize 1601×901 and return 1920×1080 are over. UI-only odd resize verified by direct UI control; All of the UI panels/DPI/F1 have not yet been accepted by this probe.

Menu: 648 tops, 22 UI commands, 8 textures, 2 073 596 modified pixels. HUD stored the World prefix with 1624 top / 270 commands, added 47 UI commands with neutral companions; I changed the 145 328 pixels. Premultiplied control gave `[74,47,36,255]`, clip and text coverage passed.

A separate read-only `tests/gameview_ui_scene_evidence_tests.py` checked the visible World: 375 018 non-zero RGB-pixels / 5818 RGBA- colors in each repeat; before/after/recovered is accurate. The center of the `[280,100,1590,850]` contains all these scene pixels and is exactly saved to HUD. Black negative control is only rejected in memory, without recording files. This additional verifier is attached to the receipt, but it is not in the previously frozen Build03 snapshot. The initial impression of the black preview is simplified by a straight read PNG; The production renderer has not changed.

Actual RmlUi has one lifecycle per configured renderer. The equation Legacy/cumulative uses a separate direct UI oracle; This is not proof of the real RmlUi on the second Legacy renderer. The old RmlUi atlas handles are not rendered after the next compose/retirement.

## Limitations of Acceptance

The usual `main`, production material art, three references matching, other UI panels/settings actions, falls/snow/smoke/fire FX, full set of tests and full performance baseline remain OPEN. Diagnostic World contains neutral controls. The time of the entire check is not FPS or GPU duration; the corresponding fields are left `null`.

The first C++ attempt is rejected by the C5054 shortcut expression; It's corrected by the apparent integer casts. Build02 passed, but Native01 rejected the later installation of schema-2 art after the UI submit. The unchanged cold-load block is moved to the probe before the first submit; production GameArt guard is left. Build03 and Native02–03 tested this patch. All failed outputs are saved.

Previous accepted weather GPU snapshot saved; The changes to `render_frame.hpp`/renderer made its exact cohort historic. UI Build03 confirms its own frozen source snapshot. The next bound sources changes will make this cohort historic for live checks. Actual receipt: `.unlazy/engine-completion-2026-10-08/ui-gpu-actual-2026-10-08.json`. Formal automatic gate certification is not claimed. The files were not deleted.
