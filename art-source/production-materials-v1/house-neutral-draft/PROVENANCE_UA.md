# Neutral house palette — authored draft

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

DRAFT / SOURCE ONLY / NOT_ACCEPTED. These four files are not registered in the collection or live assets. Export, native render, production materials, reference fidelity and FPS are not verified and not accepted.

`master.json` separates new flat material colours from the preserved XYZ- geometry of the house. `palette.json` contains 14 materials and exactly 114 unambiguous triangle assignments. RGB values are authored provisional sRGB bytes, not measured reflectance and not sampled paint. A single material has the same RGB and response on all its surfaces: no separate sun-facing/night/contact grade is baked into the colors. For example, all chimney facets, rim and interior floor have the same material; The dark void is not imitated by a single baked RGB shadow.

For each triangle `master.albedo` is equal to assigned `palette.albedo_srgb` plus alpha255; roughness/emission_mask/wet_mask/caster_mask are copied from the same palette material. These are draft response values, rather than measured properties of slate/plaster/wood/stone/glass/metal. AO from the original master is kept separate without modification. Glass — opaque coloured proxy; transmission/refraction OPEN. All emission masks and emission_radiance zero; window/lantern schedules, radiance and local-light anchors need a separate source/causal review.

## Preserved geometry

Sources: `art-source/production-materials-v1/house/master.json`, SHA256 `0e602fa94a406ee3aefd9c62550679434f2a75a87e37030312b2170d77299d9b`.

Preserved: exact XYZ numbers, 114 triangle IDs/order/winding/topology, each triangle AO, 10 coarse sky-shadow triangles with vertices/source_mask, empty local_occluders, 20 numeric probe IDs/triangle IDs/barycentrics, coordinate system and export settings: eppp4, padding0. The new purpose/IDs, RGBA and material scalars do not change geometry or its normals. Normals and depth are formed by authored XYZ/winding; RGB is not used for their recovery.

It's the same faceted proxy geometry, manually nominated for screen landmarks, with corrected coplanar fence posts and bucket ring/facets. It didn't become a physically accurate model because of the palette replacement. The old assumption skewed facades/roof planes/thin joints and missing hidden parts are preserved.

The original registration target is stored in crop529×428, rect[106,37,529,428], normalized pivot[.5,.98], pixel pivot[264.5,419.44000244140625], reference_width529 and eppp4. `palette.source_registration_target` copies the old original-catalog declaration only as source/future goal; active_registration=false. Its old painted-alpha caster policy is not the accepted policy of the new neutral mode. Unregistered default master bounds cannot be silently announced as a new registered collection.

## Alpha and silhouette

The old read-only map original paint: nonzero alpha147212, supported127563, unsupported19649. Source — bound `house/landmarks.json`; It's a legacy measurement of old source paint, not a new export result.

This draft does not add geometry to baked glow/halo or missing edges/props. Palette alpha255 means opaque authored triangles, not forced conversion of the original fractional paint alpha to 255. The new raster alpha/silhouette, pixels beyond the old paint support and actual picks are not measured. Future geometry-derived neutral output may lose the old baked halos; It's not reference acceptance. Original PNG, landmarks/RLE-map, old master and collection remain unchanged.

## Provenance

Original visual art: Ribterra project / OpenAI image generation, sourceproject, LicenseRef-Proprietary, notice `licenses/Ribterra-Assets.txt`. Source sheet SHA256: `c232e87e5891e37b3ebfd90f214c70bea6628b2ce6c2c6521e086d947a0af7f9`; exact original catalog entry SHA256: `45d5827d29a0eef1ed8c49824e20126f28cfe408cf34d53b0df363e2dd5ce0d5`.

Source geometry: Ribterra project / Codex authored registered proxy draft. New palette: Codex-authored provisional flat material palette for Ribterra. These are separate attribution roles; The original visual art is not declared a neutral albedo, and the proxy is not attributed to the author of the reference 3D model.

`palette.json` stores original license/source/author/notice metadata and hashes, original prompt and target0261/0262 hashes for visual-only purpose. Separate full-file source_bindings bind the 11 entries: old master, landmarks, assumptions, collection, original PNG/catalog, asset_sources, original notice, prompt and two references. Additionally bound new `NOTICE.txt`. References do not allow redistribution. The new notice does not change the original license.

| Preserved source | SHA256 |
|---|---|
| house/landmarks.json | 2ca26a33c09e4b52275161afabcbd53212e67a8a68c02cb8c7647aa995dde2b7 |
| house/GEOMETRY_ASSUMPTIONS_UA.md | d1c949159fc35a08be58ffe491ccb0e2507022c960e98f20946eadf172abde59 |
| house-collection.json | cd37d0b9936fa938213bdb26a151c2d4795141c0fab287cdd9d0c6cc636937a1 |
| licenses/Ribterra-Assets.txt | ca14f2c54c511542928e26d3903b8acbc1f7f5d48410f440787cec87271479e9 |

## Next bounded step

An explicit mode `authored-xyz-triangle-materials-v1` is required in the existing exporter/loader: mode-specific source_record/hash and alpha validation, exact extent/pivot/eppp, direct palette→geometry raster correspondence, preservation of the original PNG/catalog/notice bindings. Now the painted collection exporter replaces the triangle albedo with the original RGBA; the normal loader requires exact original cropped RGBA and rejects this different RGB. So connecting the new master through the old painted mode would mean losing the neutral palette idea.

After the source acceptance of the new mode, retained channel exports, independent geometry/palette/alpha controls, actual lighting/anchor/occlusion on zoom0.62/1.30/3.0, new CPU alpha-mask/selection and typed geometry outline are required. Apart from the model silhouette completion, artists/reference review, emission/glass/local occluders/contacts, the other seven masters and animation/cargo/tier/season families remain. This data-only draft closes no root engine/reference/performance gate.
