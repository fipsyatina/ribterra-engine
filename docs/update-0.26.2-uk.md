# Ribterra 0.26.2 — V02: valley, river and protopted path

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

0.26.2 is moving the world to a new profile **Alpine Valley 1**. The New World receives one connected river with shores, wider material zones, level starting fields and clustered forests, rocks, and wild food. Compact HUD V01 and old simple buildings and inhabitants are preserved.

## What changed

- The generator 6 builds the river as one continuous strip with a single water level. The terrain, water, shoreline and elevation are determined by the seed.
- The translucency, altitude change and suitability of the foundation are determined by common clean rules. The rendering, movement, construction, selection of cells and bridges use the same surface geometry.
- Start is checked before the game: the camp has accessible wood, stone, food, mine and at least 80 suitable cells. If the check fails, a limited fixed correction is made or the creation of the world ends in a clear error.
- The little noise of the earth is attenuated; The plants and rocks form readable groups. Water has a moving band and a coastal foam, but the waves do not draw over the finished bridge.
- The units gradually ** prototype traces ** on the cells they actually pass through. The relationship is an authoritative state of the world, kept in a safe and passed on in a cooperative. It's a visual memory of routes: it doesn't give a speed bonus and it doesn's not change the rules of construction.
- Manual construction of Road (`Дороги`) has been removed from the catalog, hot keys, and commands. Its historical ID 19 is not reused; The old roads that existed remained visible and passable, but no longer accelerated traffic. Bridge (`Міст`) remains available.
- The initial open area is increased to 16,5 cells so that the player immediately sees the river, resources and the construction site.
- `F1` as in V01 completely hides HUD; The next push push returns it.

## Compatibility

The following versions are available: engine `0.26.2`, protocol `20`, rules `18`, generator `6`, save format `8`.

The new client and server must be the same version. The old saves that are maintained do not reproduce the already recorded saws: they retain their generator/profile and old passing. The trace wear field added at the end of the Tile table and for old safes is `0`.

## The Limits of Consciousness

This is V02, not a complete transition to 3D. New building and occupation models, rain, winter, dynamic lighting, physical erosion, waterfalls and bonus trail fees are not included. For an external co-op, it still needs separate reception on two physical Windows PCs and between different networks.
