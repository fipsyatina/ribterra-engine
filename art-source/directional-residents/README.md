# Directional residents and visible cargo

Eight original generated PNG plates are archived in `sources/` and copied unchanged into `assets/art/resident8-*.png`. They contain 256 frames: two appearances, four activities, eight screen-facing directions, four walking phases. Columns are E, SE, S, SW, W, NW, N, NE; rows are gait phases. Actual image dimensions are 1774 x 887; the registry uses those dimensions rather than the requested generation size.

`prompts.json` contains the built-in image_gen prompts and source paths. `correction-prompts.json` records the female-cart orientation repair and subsequent removal of an unwanted baked checkerboard. Its final plate uses the existing runtime magenta decoder; the seven other plates retain original PNG alpha. No CLI image generation or procedural replacement artwork was used.

Run `tools/register_resident_art.py` with Pillow and NumPy to reproduce the UV registration. The script reads alpha to align feet but never rewrites image pixels. It preserves unrelated world-art entries and adds provenance to `content/asset_sources.json`. `integration-report.json` records image hashes. When original generated-image paths no longer exist, it uses the archived source PNGs.

`UnitAnimation::facing` follows interpolated on-screen movement. Pausing preserves facing and phase. The existing cadence slowdown and interpolation remain in effect. `resident_art.hpp` selects a visible load only when the snapshot reports actual carried quantity. Wood/firewood use armfuls of logs; other light/food loads use sacks; heavy batches of stone, clay, bricks, planks, tools or spears use a handcart.

The cart is presentation of an existing physical hauling task: no additional vehicle simulation, capacity upgrade or speed bonus is implied. Residents still carry at most ten item units, unload at the authoritative destination, and return to empty-handed poses after unloading. Multiplayer clients use the same visibility-filtered cargo projection.

`ResidentVisualProbe` renders all 256 frames through Direct3D 12 and runs actual hauling to a camp and a stockpile. It validates inventory conservation, carry capacity, storage destination and carrying save/resume. Its arranged starting supplies are a test fixture; delivery itself uses the ordinary simulation and client renderer.
