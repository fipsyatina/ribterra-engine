# Ribterra 0.12.0 — Biomass and fog

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Update from 10 September 2026, performed without sub-agents.

## The World

- 832×832 cells on each of the two layers instead of 256×256: the area is increased by 10,5625 times. The boundaries of the coordinates are −96...735. In general, the 1352 is the 32×32 chain.
- Nine permanent biomes: the plains, the forests, the lakes, the mountains, the snow fields, the snow forests, frozen lakes, snow mountains, deserts.
- The seed determines the smooth fields of altitude, humidity, and temperature. Biomes form regions, and resources depend on biomes. Lakes  real water cells; Forests contain trees, mountains contain rocks.
- The original resources of the training valley and field 20...28 around the camp are preserved. A new generation is beginning outside of it.
- Seasonal winters add snow to the usual biomes. The permanent snow areas remain white in summer. The circuit in this update keeps the water impervious. The mountains use mountain slopes and rocky soil; The individual physics of altitude was not added.

## Fog and images

Unknown, missing the client's bag and the space behind the card is covered in a blurred blue-grey fog. The `fog-masks.13` masks from the available atlas create clouds and a soft edge of open territory. RGB The mask is coloured during loading; the original PNG is unchanged. Hidden biomes are not passed on to the client.

The available surfaces, summer and winter trees, rocks, tops and ridges are used. Corrected UV for the negative coordinates. The mini-map has fixed screen boundaries regardless of the size of the world.

## Reservation, network and boundaries of resources

The new saves are: format 8, rules 7, generator 3. Network protocol 8 — client and server should be updated together. Biomining is a separate cell field and is part of the state integrity check.

Old worlds full of 128 bags are being expanded with new bags; The old cells don't regenerate. To see the new biomes closer to the settlement, create a new world. Available backups and migration procedures are preserved. Unfinished routing searches from old caves are completed within the initial limits; The following commands use an extended map.

The integrity check casts the hashes of the unchanged tangents and discards the cache during importation; The render cuts the bags off the screen.

Searching for one route is limited to 131 072 search engines and the overall budget 64 MiB. A large card does not reserve its full area for each order; exceeding the limit has a clear reason for refusal. The network image may contain up to 256 the particles, to 24 MiB the accounting balance; timeout 10 seconds without progress or 30 seconds overall.

## Checks

- Successful final result of all 54 tests: complete CTest scan and repeat 5/5 joint checks after partial group movement correction. In addition, manual instructions, preservation, biology, road planning and world validation are repeated. The logos are `artifacts/biomes-final-tests.log`, `artifacts/biomes-release-tests.log`.
- Three seeds: all nine biomes, area connectivity, increased area, starting field, save/load, expansion of old chanks, dropping hash cages, networked biome preservation, and refusal to transmit hidden biome.
- Migrations of old safes, including 65 historical control states for each v7 fixture, have been made; The old ways of finding the way are still alive.
- Network: full extended card transfer, late input, override, timeout/resync, early ACK, loss profiles 1% and 3%.
- GPU: Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU, `software=false`. The fog, mountains, lake, desert, snow scenes, and compact interface were all reviewed.
- The `dist/Ribterra-Windows-x64-0.12.0.zip` file is packaged into a separate new folder. SHA256 the executed file matches the test set; native smoke 1100×700 is successful. The evidence is: `artifacts/biomes-0.12.0/package-verification.json`, `packaged-device.json`, `packaged-compact.png`.
- `git diff --check` is over. Previous work changes and package 0.11.1 are retained; The committee and publication GitHub in this update were not performed.

Scenes `artifacts/biomes-0.12.0/biome-*.png` rendered by a real GameView to Direct3D 12 from procedurally generated cells. To examine the biomes, the test opens a small area and moves the camera. It's not a normal player's progress. `summer.png` and `winter.png` show settlements after authoritative construction in the simulation.
