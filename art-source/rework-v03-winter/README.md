# V03 Level II winter plates — 2026-09-30

Built-in Imagegen edits of the existing project-owned `assets/art/refresh-house.png`
and `refresh-workshop.png`. The source references remain unchanged. These are
RGBA bitmap variants, **not editable 3D masters or dynamic-light material maps**.

Outputs consumed by the game:
- `assets/art/rework-house-level2-winter.png`
- `assets/art/rework-workshop-level2-winter.png`

Exact prompt template (`{kind}` was `house` or `workshop`):

> Use case: lighting-weather. Production 2.5D game sprite edit. Edit target: the attached six-state {kind} atlas. Extract ONLY the bottom-right completed LEVEL II {kind} as ONE isolated object, same exact silhouette, architectural features, camera, stone foundation, entrance and work props. Add natural winter snow accumulated on horizontal surfaces/roofs, like the bottom-middle winter reference but KEEP the bottom-right LEVEL II architecture; do not revert to simpler level I. Keep warm window lights and uncovered vertical timber walls. Output a single centered full building cutout on genuinely transparent RGBA background, NO other cells, no text, no people, no background, no magenta, no extra ground island, no smoke. Entire object including footing in frame; bottom footprint rests at same level. Orthographic isometric game asset, same lighting from upper left. Preserve all level-II geometry and art style faithfully.

Registration is deterministic in `assets/art/world.json`: each crop includes an
8 px border around the alpha >100 bounds; the contact row is pinned to the same
runtime ground anchor. No image pixels were changed after generation. Hashes,
dimensions and ownership are checked by `v03_asset_passport`.

Art acceptance remains subject to the native before/after calibration and winter
capture. The two extra pages add 12,586,052 bytes of raw RGBA texture storage;
this is a measured-content-budget delta, not a claim of a new optimized atlas.
