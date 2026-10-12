# Ribterra 0.13.0 — volume relief

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Update from 10 September 2026, performed without sub-agents.

## What changed

In the new worlds, the earth has a numerical height of −32 to 512 vertical units. The generator creates lowlands, hills, wide swamps, elevated plateaus, and mountain slopes. The height field is related to the biome; The lakes and ice remain horizontal. The start area 0...47 is flat, moving smoothly into the relief. The map remains 832×832 on the layer.

The surface is composed of textured triangles with common vertices, particularly within the jaws. The ground is deformed along with the height and gets lighting on the slope. The mountain plateaus became smaller and smaller, complementing the shape of the earth itself. The assets available are used; The PNG sources were not recycled.

Trees, rocks, inhabitants, and routes are all attached to the surface. During the movement, the height is interpolated on the same triangles that the client is drawing. Under the buildings on slopes, the foundation is flat. The selection of cells checks the visible geometry of the front and back. The lighting of the building is repeating the slope. The fog is coming after the land and the objects. The client does not get hidden altitudes.

This is an isometric relief in the actual rendering. The speed and rules of passage have not changed; ground-based work, jumps, falls, and free-spinning 3D cameras were not added.

## Storage and networking

The game version 0.13.0; The key is the format 8, rules 7, generator 4, protocol 9. The heights are in the storage, checksum and network images. Old preserves retain the previous surface; A whole new relief is available in the new world. For a network game, the client and server must have versions 0.13.0.

A network cell with a height of 12 bytes. The heights are transmitted by the exact differences inside the shank and restored to the data check. The old protocol deviates, the old schemes of the saves remain compatible. The historical network scheme is not rewritten: the protocol 9 has a separate baseline. The fixed speed GNS is increased from 256 to 512 Kb/s; The buffer limit 4 MB, priority of orders and recovery time have not changed. The network failure test fixed the Windows temporary marker reading conflict, without loosening the deadlines.

## Check and package

- The final results of all 54 tests were successful: the full gap in `artifacts/terrain-final-tests.log` had 52 successes and two network errors; After correcting the transport, repeat all 13 related checks, 13/13 successfully (`artifacts/terrain-ship-tests.log`). Repeat included protocol, transport, both network_adverse, chunk_network, network_session, individual server processes, client rendering, input, and biome.
- Checked common peaks within the tanks, level water, hidden altitude isolation, generator boundaries, checksum, storage, additional and subtractive network altitude differences, deviation from old protocol and inaccurate data.
- `ArtVisualProbe`: 1089 cell selection checks in nine biomes; real calls of choice and orders to climb the slope; The coordinates of the intersection of the motion are in contact with the surface. The real `summer.save` from the 0.12.0 packet is downloaded, rewritten to a separate file, and re-downloaded with the same checksum. The original was unchanged.
- Native images: `artifacts/terrain-release/overview-snow-mountain.png`, `overview-mountain.png`, `overview-meadow.png`. These are generated playgrounds with test fog detection. `slope-movement-test.png`  separate controlled test slope.
- Freshly unpacked ZIP: output `--smoke --compact` 0, Direct3D 12 / NVIDIA GeForce RTX 3050 Laptop GPU, version and title of the window 0.13.0. The image `artifacts/terrain-release/packaged-compact.png`, the report `package-verification.json` in the same folder. SHA-256 EXE matches the verified set.
- `git diff --check` is successful. The committee and publication on GitHub failed.

`dist/releases/0.13.0/Ribterra/Ribterra.exe` The archives: `dist/Ribterra-Windows-x64-0.13.0.zip`, 47 621 123 bytes, with the file `.sha256`. `Play Ribterra.cmd` is directed at 0.13.0; the 0.12.0 package is saved.

SHA-256 ZIP: `0968de47e7ee3be04ac5e45f8fae9d028ffb4ea9b5136c468247d174da9f1ae4`.
