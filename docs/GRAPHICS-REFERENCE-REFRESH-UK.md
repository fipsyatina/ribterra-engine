# Updates to the chart by reference — 15.09.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

New material is already connected to the game. A separate launch on the desktop: **Ribterra - New Graphics**. The completed file: `dist/reference-refresh/Ribterra/Ribterra.exe`.

30 sheets are made with 256 frames and textures: all 20 building types, construction stages, winter options, land, water, snow, caves, wood, stone, ore, pebbles, dwellings, and four types of animals. The originals, description and generation parameters are stored in `art-source/reference-refresh/`.

The game features smoke from the chimneys, coastal mud, flashes and foam on the water, fire, dark shadows of buildings, and warm glow at night. The camera can be approached at 3,0. The pink edge of the leaves is removed, seams between the road sections are made and the snow is applied. The network traffic interpolation is preserved.

Verified by a true renderer Direct3D 12: day, night and winter appearance, building catalog, compact screen, transitions between biomes and launch of the collected game. The frames in `artifacts/reference-refresh/showcase/` show a specially arranged settlement for charting. The usual new world remains a world with a launch camp; These images are not a demonstration of automatic city generation.

Online control session: 2 clients, by 10 residents, RTX 3050 laptop GPU, 240 frames after warm-up, restrictions 60 FPS. To: 59,98 FPS, the average work of the frame 8,81 ms. After: 59,97 FPS, 9,54 ms; Percentile 95 was 12,52 ms. This is a short check of one specific scene, rather than a large-city benchmark. The transparency, atmosphere and movement tests have been completed.

The result uses the current Ribterra isometric renderer. The pixel-performance of the concept art is not achieved: other proportions of the world, interface and lighting mode remain. The drawings of second-level buildings and part of the decorative items are prepared separately; They don't mean that this update is implementing new research or all the actions from the reference panels.
