# Ribterra 0.16.1 — Coursor and smooth shores

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

The cursor was previously drawn in game footage. During slow motion, the generation or preservation of its image also stopped. Now SDL creates systemic color cursors from existing game icons: regular shooting, construction, and job selection. The OS moves the cursor regardless of the staff. If the platform does not support a color cursor, the system indicator remains visible.

Additionally, a reworked image of the relief for each mouse movement event is removed: the image is updated for the frame and clicks where the cell definition is required. The release of the middle button is processed even over the interface panels so that the camera does not remain in overdraft mode.

The coastline is a smooth mixture of water, wet sand, and dry matter. The equal weights of the common peaks remove a sharp stick from both sides of the coastline. The slope is smoothed on several cells to the level surface of the water. This same surface is used for the positions of the inhabitants and the selection of the cell. Hidden areas do not affect the shore.

The changes relate to the display and the input. The versions of the rules, the generator, the protocol and the storage format are not changed; old cards are re-created without re-generation. The water bottles remain impassable, and the rules of the building are maintained.

The native creation and switching of system cursors on Windows/Direct3D 12, lack of frame-based cursor drawing, smoothness of shoreline weights, common peaks, water height, shoreline angles and selection of raised cells have been verified. The system cursor does not fit into the images that the game renderer makes.

- [Coast in the game](../artifacts/coast-release/shore-transition.png)
- [Native check of the cursor and GPU](../artifacts/coast-release/render-report.json)
- [Checks](../artifacts/coast-tests.log)

We passed all 11 target tests. ZIP is packaged in a new folder: EXE matches the verified set, all 86 assets match the manifest, launch on Direct3D 12 is successful. Launcher switched to 0.16.1. [Checking the packet](../artifacts/coast-release/package-verification.json).
