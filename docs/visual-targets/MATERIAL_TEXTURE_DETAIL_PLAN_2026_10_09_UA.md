# Neutral texture details: the following contract

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Status 09.10.2026: **SOURCE design only; The implementation, export and native proof have not yet begun**. The current flat-palette cohort remains separate. The full purpose of the motion, E01–E17/P01, is three references and ≥120 FPS active/OPEN.

An independent review of the current `tools/export_material_master.py` confirmed: XYZ defines normal/depth/coverage, but RGB of each triangle is stable. Triangle UV and per-material textures are not supported. Large boards/blocks can be authorized by individual triangles; The smaller grain/weave/stone variations need a more compact source. References are appearance-based, not reliable physical coefficients.

Future implementation should include a clearly new `albedo_source.kind` and palette version. Old painted and flat branches retain their exact schemas and non-report bytes; The new fields in the old mode deviate. Material color is one-of: fixed RGB or bounded neutral texture reference. The texture brightness does not create emission or roughness.

The initial proposed texture contract: tight RGB8 sRGB, dimensions/byte count/SHA, author/license/notice/source identity. Raw RGB does not require an additional PNG decoder contract. Before implementation, set the maximum dimensions, texture count, combined decoded/source bytes and source-record count in the leaf contract; the form is no longer supported API. RGBA/texture alpha does not control the geometry coverage.

For three finite normalized UV on a triangle in order XYZ vertices; top-left origin, V down. UV can be kept in the palette assignment sidecar, keeping the geometry master schema. The first version has a clear nearest/clamp sampler: interpolation UV uses the already calculated barycentric weights winning XYZ triangle. Bilinear/repeat/color-space rounding will require certain rules.

Texture sampling changes only RGB. Normal comes from winding XYZ, depth from XYZ fragment position; The current AO/material/emission/caster rules and the geometry-derived binary alpha0/255 are maintained. Outside geometry, all channels remain zero. The illuminated old sprite crop is not a neutral texture source.

Offline export continues to create the same six end channels, so a new GPU UV stream/RHI/shader ABI is not needed for this function. Runtime View must have a separate bounded textured-mode validation branch; the general relaxation of the flat-palette tuple/field inspection is inadmissible. Palette512KiB/notice128KiB, canonical paths/no-reparse, source/texture/UV/generated-albedo hashes, transactional publication and published hit-mask16MiB/outline20MiB limits remain visible.

Texture bytes, UV assignments, palette, provenance and notices are included in the producer source bindings, manifest/source record and before/after/repeat ledger. The current helper counts20/14 cannot be silently applied to a new set: the new versioned closure must indicate the exact membership/count and all inputs. GPU/runtime/object/export/capture roots have their own 128MiB caps, free384MiB; aggregate — fair sum, without changing individual common limits.

Required actual checks: RGB-only texture change stores the other five channels; UV-only change changes only RGB; XYZ change affects normal/depth; common UV The edges don't create seams. Malformed dimensions/bytes/SHA/UV/one-of/provenance, oversize and reparse are excluded from publication. Old exports retain their non-report bytes; report/source SHA deltas fairly reflect the modified producer. Two actual exports/native repeats and an independent source-derived reader are required for reception.

This contract only provides a tool for detail. Production masters of warehouse/workshop/wood/stone/room with cargo, terrain/water/bridge materials, house completion silhouette/emitters/contacts and comparison with three references still need separate work. Art or physical authenticity cannot be derived from texture SHA.
