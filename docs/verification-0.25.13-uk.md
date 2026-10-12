# The correction check 0.25.13

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

24 September 2026, the local Windows collection MSVC, `AInora/first-playable`.

## Cause and Correction

The message from the user's screenshot — `Missing localization key: Мешканця обрано`. `GameView` transmitted the finished Ukrainian text, and the main cycle 0.25.12 caused him to search for a rigorous localization key. This caused an unprecedented exception and the end of the game. But the same mistake involved selecting the plot, cancelling, moving, and other messages.

`GameView::take_status_text` now returns the finished text. At the edge of the old channel, messages are translated by known keys, and the rest of the text is transmitted without repeated rigorous search. The same protection is applied to text rejection reasons and network messages in the main cycle and HUD. `Localization::text/format` remain strict for conventional signatures; The unknown key test is still testing the exception.

The previous `BuildingInspectorProbe` transmitted the message directly to HUD, avoiding the error transform in the main cycle. It's updated and along with the new `InputFeedbackProbe` uses the same message processor as the game.

## Evidence

**12/12 CTest have been passed, 82,05 with ** (`build/02513-tests.log`): input_feedback, client_input, building_inspector, localization, ui, ui_lifecycle, production_control_ui, save_compatibility, save_compatibility_ui, client_smoke, host_lobby_flow, online_entry_flow.

- InputFeedbackProbe: real events SDL for choosing a tenant, placing a house, cancellation and movement. It is verified that after the order is processed, the number of buildings increases by one. The message is consumed once and seen in RmlUi. Separately verified translation of known server cause and secure display of unknown message. All seven scenarios were submitted for five languages: **35 frameworks** to `artifacts/hotfix-0.25.13/`.
- ClientInputTests: selection, movement order, resource collection, construction, conservation and extension. BuildingInspectorProbe re-checks the manufacturing status and the map message. The migration of the safe has been re-examined.
- The existing ready-made Ukrainian messages were not re-translated into other languages within the scope of this term correction. The five-language check shows the absence of flight and correct display, rather than the full localization of historical lines.
- The fixed EXE and the standard `Play Ribterra.cmd` completed `--smoke` with the code **0**: **0.25.13, Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, software=false**. Both the labels on the desk lead to this launcher; `dist/current-build.txt` indicates that 0.25.13. The user's profile and save were not deleted.

ZIP: `dist/Ribterra-Windows-x64-0.25.13.zip`, **158816834 bytes **. SHA256: `903940fe287657f8bea9e09e7752e0969ad61a067506a283ab505ff3180616a4`. CRC the archive is checked; four EXE coincide between the set, the installation and ZIP. Verified UI, language, launcher, README and description of the source update. The machine-readable proofs are: `artifacts/hotfix-0.25.13/test-results.json`, `package-verification.json`, `installed-renderer.json`, `launcher-renderer.json`.

The full set of CTest didn't start. A verification on two physical PCs remains unchecked after a user's previous notification. The rules 17, protocol 19, file format 8 and generator 5 have not been changed. GitHub push and release didn't work.
