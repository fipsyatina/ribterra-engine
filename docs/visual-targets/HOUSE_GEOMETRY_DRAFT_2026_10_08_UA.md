# The geometry of the house

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

An independent review of the sources confirmed the coordinated geometry `building.house.ready` after correcting the columns and the hood. This is the next material work piece; Export, launch on GPU and acceptance of references are not yet available.

| Verified from the sources | The result |
|---|---|
| Triangles / landmarks / surfaces | 114 / 206 / 53 |
| Numbers of probes / semantic samples | 20 / 52 |
| Maximum failure of the reverse projection | 5.684 × 10⁻¹⁴ px |
| Planning of the columns | residual ≤ 2.263045 × 10⁻¹⁶ units; the deviation of the norm ≤ 0.000001479° |
| The Vido | a common ring with 8 vertices, top edge + Z, four vertical edges with an angle 45° between neighbouring edges |
| Alpha>0 pixels, covered with geometry | 127 563 from 147 212 |
| Alpha pixel>0 without geometry | 19 649; All 1 829 RLE-runs independently verified |

The original image, crop, pivot, reference_width, provenance, and notices are preserved. The old artwork contains painted lighting and a halo. The geometry did not gain artificial surfaces merely to cover those pixels. The draft therefore does not yet satisfy the painted-export requirement to cover every alpha>0 pixel. It needs separate neutral material art with an honestly defined silhouette and attachment verification in the normal GameView.

The camera, scale and physical dimensions used are the author's assumptions described in `GEOMETRY_ASSUMPTIONS_UA.md`; The numerical alignment with them does not restore the shape of the object depicted. The top edge of the columns retains the set screen outlines; Their heights are not declared by measured physical data.

| The Source | SHA-256 |
|---|---|
| art-source/production-materials-v1/house/master.json | `0E602FA94A406EE3AEFD9C62550679434F2A75A87E37030312B2170D77299D9B` |
| art-source/production-materials-v1/house/landmarks.json | `2CA26A33C09E4B52275161AFABCBD53212E67A8A68C02CB8C7647AA995DDE2B7` |
| art-source/production-materials-v1/house/GEOMETRY_ASSUMPTIONS_UA.md | `D1C949159FC35A08BE58FFE491CCB0E2507022C960E98F20946EADF172ABDE59` |
| art-source/production-materials-v1/house-collection.json | `CD37D0B9936FA938213BDB26A151C2D4795141C0FAB287CDD9D0C6CC636937A1` |

The first version of the four sources is stored in `build/house-draft-source-2026-10-08-attempt01`: 5 files along with the attachment record, 338 732 bytes. No file has been deleted. Full neutral-albedo, alpha coverage, native anchors/lighting, other categories and options, normal startup, references and 120 FPS remain OPEN.
