# Restore the source resource register

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Current `assets/manifest.json` updated from an independently verified candidate. It now registers 180 files, stores all 102 previous ID and allows reading of all 67 pages of the current world directory. Image, `art/world.json`, UI, location and provenance are unchanged.

The previous manifest did not register the 63 page, and its entry for `art/world.json` had outdated SHA/bytes. `AssetCatalog` checks all records when created and only allows for registered paths, so this blocked the download of the current source art.

The candidate is created through the existing `tools/asset_tool.py::build` in memory, without calling its live-folder recording function. The independent read-only review re-checked the 181 existing asset file, reproduced all 180 records, and confirmed the candidate's intended integrity. Verified 4 notices, 14 prompt files, 5 source images, 30 inputs UI- powers and five locations for 671 non-direct key. Trails are confined, there are no reparse-accused.

| The bond | The result |
|---|---|
| SHA new manifesto | `d28aa9dbc8c2566aaccda51916d0c3f6ebe2183b92aa30852426c76f2dcb2218` |
| Canonical content hash | `8e7db099671da984990bed5d888fb5e94f0a1d650b71edaa09fdf2cf9afd7fd6` |
| Look at the Resources | 164 textures / 6 data / 3 fonts / 5 styles / 2 UI |
| Licenses | 177 Proprietary / 3 OFL |
| The applicant's case file | 5 files, 229 610 bytes |

The `build/original-asset-registry-2026-10-08-attempt01` retains the previous manifest, candidate, before/after inventories, notice bindings, and a separate record of the actual publication. Before the replacement, the parental check once again confirmed the consistency of all sources; Only `assets/manifest.json` that has changed since then is the one that is almost identical to the verified candidate. No files have been deleted.

This is a working tree metadata fix. Actual normal `AssetCatalog`/`GameArt`/GPU launches, packaging, launcher, production-neutral art, references and performance are checked separately.
