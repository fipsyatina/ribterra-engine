# Engine calibration geometry master

`material-master.json` is an editable, deliberately small triangle model for the
Ribterra schema-2 material bridge. It is a diagnostic cabin-like volume with two
facades, a gable roof, sloped ground, and a window emission mask. It is **not** a
concept-art or production-art deliverable.

Export into a path that does not exist:

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
python tools/export_material_master.py art-source/engine-calibration/material-master.json artifacts/material-export-new
```

The exporter refuses an existing destination and never deletes or replaces
files. It uses only the Python standard library. The six `.rtex` files are tight
row-major blobs with no header: albedo is RGBA8 sRGB; normal/material is linear
RGBA8 containing octahedral normal XY, roughness, and AO; depth is little-endian
R16 UNORM over the `depth_range` recorded in both `world.json` and
`calibration.json`; emission and caster are R8; surface is linear RGBA8 with wet
mask in R and coverage in A.

Projection is fixed to `U=29*(X-Y)`, `V=14.5*(X+Y)-29*Z`, and camera depth is
`dot(XYZ,(1,1,1)/sqrt(3))`. One affine barycentric sample is evaluated at each
texel center, and the maximum camera-depth fragment wins. Bounds are snapped to
integer export pixels and padded before `source_size` and `pivot_px` are derived.
Calibration probes are selected only from winning texels at least eight export
pixels from their triangle edges, and retain the source triangle vertices plus
the actual center-sampled position and winding-derived normal for independent
ray/plane reconstruction.

`calibration.off` and `calibration.on` share identical texture bytes and geometry.
Only their immutable material emission vectors differ. Thus the native probe can
test emission without duplicating or baking warm RGB into the neutral albedo.
