# Authored XYZ draft of the original house

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

DRAFT. Production, neutral albedo, reference fidelity, export and native acceptance are not accepted. Original PNG/catalog/provenance/notices are preserved. The draft is not connected to normal GameView.

## Source and observations

building.house.ready: art/concept-houses-low.png, rect[106,37,529,428], normalized pivot[.5,.98], pixel pivot[264.5, 419.44000244140625]. PNG has obvious baked directional/ambient shadows, amber window/lantern glow and surrounding halo. It's a legacy painted RGBA, not a neutral albedo. Normals/depth were not derived from RGB.

Manually nominated 206 screen landmarks, 53 surfaces and 114 XYZ triangles roof/ridge/eaves, gable/two facades/foundation, chimney, door/three windows/lantern, steps/barrel/bucket/bench/part of the fence. Screen tolerance is mostly±3–5px — nomination assumption, no acceptance measurement has been performed. Master SHA256: 0e602fa94a406ee3aefd9c62550679434f2a75a87e37030312b2170d77299d9b. Faceted render proxy is not recovered from one PNG exact physical model; thin joints, hidden details and some edges OPEN.

## Camera, scale, and ground anchor

The existing basis is u=29(X−Y), v=14.5(X+Y)−29Z; depth=dot(XYZ,(1,1,1)/sqrt(3)). eppp4 is an explicit authored assumption about render units. For nominated screen sx/sy, Z is assigned and u=(sx−pivot_x)/4, v=(sy−pivot_y)/4; X=v/29+Z+u/58, Y=v/29+Z−u/58. Normals are formed by winding XYZ, not painted brightness.

Reference width529 and crop are preserved. Current house width is 76×2.1=159.6 before zoom; The future registered adapter is expected to use scale159.6×4/529≈1.2068053. Native confirmation of this expectation is absent. Ground origin remains original pivot/Z0; The gameplay footprint/placement/traversal is unchanged.

Vertical wall128px is designated as height128/(29×4), foundation52px as52/(29×4). Painted ground-edge slopes do not provide an orthogonal box at this base, so the plan draft is moderately skewed. Two roof triangles may have slightly different planes: painted quadrilateral is not proven planar. These are obvious geometric assumptions, not physical dimensions/camera acceptance.

Door/frame/housing patches have offset0.018, and inner glass0.024 along the camera ray for bounded layer separation without screen shift; glass has a separate frontmost layer above the frame, so the emission is not lost in the coplanar tie. The inner chimney floor is nominated at 0.15 below the rim: a visual assumption about the cavity, rather than an RGB estimate. Steps/props are coarse visible facets.

An independent draft review led to repairs of twisted post faces and the bucket fan. Four post faces will retain nominated screen outlines, but now each is a vertical plane through its two ground-edge endpoints. The top Z of each edge equals its own vertical source-height/(29×4): the top contours may be sloped, a level cap is neither physically established nor modeled. This is an explicit, bounded draft registration compromise, rather than a recovered post construction. The bucket body has been replaced by connected cylindrical proxy geometry: source center202/411.5, radius0.085 render units, height24/116; One common eight-vertex top ring and four vertical body facets. Normals differ by 45° between neighboring body facets and are consistent within each plane. Flat cap has normal + Z. Rim/cylinder fit — manual assumption to the contours of the original small bucket, not the brightness estimator.

## Materials and shadow metadata

The Collection copies the original cropRGBA without resampling/recolor/normalization. Triangle albedo — is an edited placeholder, replaced by paintedRGBA. Roughness/wetness bounded but not measured properties slate/plaster/timber/stone/glass. AO1 does not remove baked AO/shadows from RGB.

EmissionMask1 only on the nominated glass three windows and a lantern; radiance[1,.35,.08,1] provisional. For future legacy-painted diagnostic preview emission scale must explicitly be0 until neutral/source/causal review to avoid duplicate lighting. External emitters, schedules, and contacts are missing/OPEN: unsupported metadata was not included in the collection schema.

CasterMask1 is a draft policy; The collection is clearly painted-alpha-times-authored-mask, so fractional alpha is not forced to255. Sky triangles — only coarse opaque roof/main facades/chimney; fine-alpha shadows OPEN. Local occluders are still empty: a coordinated plan, real bounds, and owner-safe emitter anchors are required.

## Alpha support and probes

Maximum within-post-face normal angle after repair: 1.47877933347e-06°. Planarity residual maximum: 2.26304525762e-16 render units. The exact normals/angles/residuals and cylinder parameters are listed in the landmarks.coherence_repair. This is in-memory authoring calculation, not exporter/native acceptance.

Read-only in-memory authoring calculation on source pixel centers: nonzero alpha147212; supported by projected draft triangles127563; uncovered19649. landmarks.json contains an accurate RLE-map and 32px tile counts. This is neither an export/test/native run nor independent artistic acceptance. Missing pixels cover the surrounding baked halo and unmodeled edges/props; there is no automatic physical classification for each pixel.

No full-canvas/two-plane/billboard/nearest-fill support was created for halo. The current collection exporter must reject this incomplete draft by alpha>0 without XYZ. Export PENDING. The next art step: look at the missing pixels map, add only the real object geometry, and for baked glow create a separate neutral paint/render source with the originals preserved.

The Master has 20 face-qualified numeric probes; The read-only nomination used the frontmost authored triangle, source alpha>=64 and clearance>= 8px. Smaller window/ground/prop samples in landmarks.face_samples clearly have numeric_probe_8px_eligible false if they do not pass clearance. Comparison with their own assumed faces does not in itself prove source-form correspondence. Requires artist/source review, actual reconstruction/lighting and native anchor/occlusion on zoom0.62/1.30/3.0.

## Provenance and limitations

Original paint provenance: LicenseRef-Proprietary; Ribterra project / OpenAI image generation; sourceproject; noticelicenses/Ribterra-Assets.txt. Original prompt and target0261/0262 bound by their current hashes. The new geometry — Ribterra/Codex authored proxy, not reference author's 3D model. References are visual-only, not license grant or geometry ground truth.

The other seven masters, tiers/seasons/construction, residents/facings/poses/cargo, neutral albedo, reference fidelity, normal startup, full engine and FPS OPEN. The historical root goal remains active; formal acceptance checkboxes remain open.
