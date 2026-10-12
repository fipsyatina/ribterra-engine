# Ribterra 0.17.0 — bridges and interface behind concepts

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

A wooden bridge is made up of sections that the residents build above the water. One section costs **12 wood and 4 stone**. Press «Mist» in the bottom panel or select it in the directory. Start with an open water cell near the passing shore. After the section is completed, you can build another one. The placement mode remains active; The right mouse button closes it.

Materials are physically delivered from available supplies. Until the construction is completed, the water remains impassable. The ready sections are used for ordinary path search; They're stored in the safe and passed on to the network customers. When construction is cancelled, the supplied materials remain on the available shore or in a ready-made adjacent section. The biome of water itself is preserved. Three construction stages are planned, a ready-made floor, a winter option and an icon.

The menu uses the Winter City and logo from the given concept, individual interactive buttons, golden pictograms and the Cormorant Garamond font. The boot button is also working until the world opens. The resource panel has two rows. The lower categories open a catalog of all buildings, resources, civil buildings, manufacturing, defense, dismantling and bridge placement. The available quick buttons for the house, farm, and warehouse remain available.

**Copper, coal, gold, silver, steel, crystal and iron are added to the interface only.** Their counters are zero, indicators explain the lack of production. These elements do not create new resources, recipes, mining or income. Ready resource icons from previous assets are used; The lead from the concept was not added.

Selection of the cell first checks the triangles of the actually shown surface in reverse drawing order. This ties the pressure to the visible slope and takes into account the relief cover. Cash is a representative of the camera and checks the camera, the camera and the scale. After the world was cast down, it was cleansed. For a frame that has not yet been built, a geometrical check of the raised surface remains.

## Validation

- Delivery, construction work, transition through three sections, preservation of the unfinished/ready bridge, cancellation and availability of returned materials: `BridgeTests`.
- Full catalog of buildings, typical network commands, and maintenance: `BuildingTests`.
- Windows / Direct3D 12 / NVIDIA GeForce RTX 3050 Laptop GPU: **225 Check the click coordinates **, the dimensions 1100×700, 1440×900, 1920×1080, three camera sizes; The resident reached the top of the mountain.
- The menu, the catalog opening, the bridge selection, the defense filter, and seven zero counters were verified by the native renderer. The panel and menu have also been reviewed at the 130% scale and in five locations.

During the full run, three failures UI were found: the timely release of the font texture in two scenarios and the hidden command button scrolling area. The textures are now held to the next frame, and the focus automatically rolls the controls into the visible area. After correction, all 19 checks UI/render have been repeated; In summary **62 tests have a successful result**. [A summary of all tests](../artifacts/tests-017-summary.json), [final verification UI](../artifacts/tests-017-ui-final.log).

[Report GPU](../artifacts/bridge-ui-017-native/verification.json), [ is a complete set of tests](../artifacts/tests-017-full.log).

![Menu](../artifacts/bridge-ui-017-native/concept-menu.png)
![Sit and panel](../artifacts/bridge-ui-017-native/bridge-hud.png)
![Resident at the chosen point of the mountain](../artifacts/bridge-ui-017-native/mountain-arrival.png)

## The following information is provided:

Generated originals and inputs: `art-source/production-v3`. The resource icons used are: `assets/ui/art/icons-cursors.*.png`. The script for the preparation: `tools/prepare_bridge_art.py`, `tools/prepare_ui_glyphs.py`. The bridge is packed AtlasTool into a separate atlas `assets/art/bridges-017-0.png`; The previous atlas is preserved. The background of the menu: `assets/ui/art/menu-landscape.png`. Gold pictograms: `assets/ui/art/glyph-*.png`.

The font from the [official Google Fonts catalogue](https://github.com/google/fonts/tree/main/ofl/cormorantgaramond) is supplied alongside the OFL-1.1 license in `licenses/Cormorant-OFL.txt`.

The game version 0.17.0, protocol 13, rules 11, generator 5, format of the save 8. The previous joint safes retain relief. For a network game, the client and server need to update together. The sets 0.16.1 and older remain in `dist/releases`.

The `dist/Ribterra-Windows-x64-0.17.0.zip` package is packed into a new folder. Verified SHA-256 of all 105 assets, matched EXE with the verified set, and successfully launched the unpacked game on Direct3D 12. Launcher switched to 0.17.0. [Checking the packet](../artifacts/bridge-ui-017-native/package-verification.json). The game 0.16.1 already open has not been closed to save the current session of the user.
