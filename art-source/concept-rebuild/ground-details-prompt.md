# Low ground details — corrective concept pass

Built-in imagegen; native RGBA transparency preserved unchanged. Style reference: `docs/visual-targets/images/target-0262.png`. Original: `exec-74410033-320e-49b4-baad-b91a03f37ade.png`; runtime copy: `assets/art/concept-ground-details.png`.

1536×1024; alpha 0–254; 1073783 fully transparent pixels. SHA256 `e4b35905dba8f0cf2e005de25d483168843eb464a28a89c13a42efeb3defb7d5`. Alpha>=32 quadrant bounds (x,y,w,h): `[125,157,545,338]`, `[837,151,596,340]`, `[84,658,652,240]`, `[806,665,661,225]`. Runtime rectangles include 3px padding. No raster editing, resizing or background removal.

## Exact prompt

Use case: stylized-concept
Asset type: production sprite atlas for the actual Ribterra isometric game, native RGBA transparent background.
Input image 1 is only the approved STYLE AND MATERIAL reference, NOT an edit target. Reproduce its finely detailed hand-painted pre-rendered aesthetic, natural scale, blue-gray rounded stone, olive-to-deep-green grass, tiny scattered white and yellow flowers. Do not reproduce its HUD or full scene.
Primary request: a single 2-column by 2-row atlas of four separate low ground-detail sprites. Top left: ONE naturally rounded blue-gray riverbank boulder, only a few small green moss patches in crevices. Top right: TWO differently shaped low blue-gray stones touching, one much smaller, sparse moss. Bottom left: a LOW, airy irregular tuft of alpine grass with five tiny white daisies, visible gaps between thin stalks. Bottom right: a LOW, sprawling grassy edge with six tiny yellow wildflowers and a few uneven leaves. Plants must be flat irregular edges, NOT round bushes, NOT hemispherical flower mounds.
Composition: each of the four sprites isolated in its own equal quadrant, generous fully transparent gutters and margins, absolutely no overlap; ground contact centered near bottom of each quadrant. Same orthographic 3/4 isometric camera as reference. Both rock sprites about twice as wide as tall; both grass sprites three times as wide as tall. High resolution micro-detail.
Lighting: soft warm upper-left daylight, deep cool crevice shadows, restrained saturation matching reference. No cast shadow outside each sprite, no grass island bases, no soil disc, no background, no labels, no border. Preserve genuine alpha transparency; not a checkerboard.
