# The rocks of the speeches and HUD — 05.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

**Intermediate step of complete processing V01–V03. The overall artistic correspondence: REBUILD. Not a release.**

[Figures before/after and concept next to ](OUTCROP_HUD_COMPARE_2026_10_05.html). Reviewed true native PNG; The comparison page was not opened and checked in the browser. At the user's request, the user only works in the background: no keys, clicks, or restarts of his open game.

## Changes made

- `relief_details.hpp`: small groups of 2–3 rocks on real steep slopes. All four angular profiles have a fully known sampling area. Average exposure of the stone ≥0.60, average gradient ≥4.5, two adjacent angles with exposure ≥0.70. Only the Grass is discovered empty, with no resources, prototyped paths or building foundations.
- Coordinated priority does not allow two groups to live side by side. It's a cutting, not a guarantee of the stones in each seed. In the synthetic fit region 60×60 720 groups are obtained; The actual start cards are checked separately below.
- Reused current 4 summer/4 winter coastal silhouettes and the same draw path. Summer and winter positions and sizes are unchanged. There are no new resources, collisions, altitude geometry, dependencies or a single renderer.
- The edge and slope share a common budget: one cell does not get both types of groups. The projection, fog and painter depth take the actual point of detail; The base of the building at this point is also checked.
- The grass and flowers are sown on a bare rock with interpolated exposure to each detail. You can't clean a whole square cell. The resource bushes are unchanged.
- `hud-v01.rcss`: the visible band of the existing decorative frame 12→16, resource icons 42→46, numbers 28→30; A little bit bigger icons and action names. The panel positions, the miniature map geometry, DOM, commands and locations are preserved. The forecast and °C are not substituted by static numbers — this dependence V04.

## Checks on the status

So the evidence is rooted in `artifacts/outcrop-hud-2026-10-05`.

| The verification | The result |
|---|---|
| Ribterra, ReworkVisualProbe, ClientInputTests, TerrainV02Tests, UiTests | Collected; old staging preserved, not deleted |
| UiCheck / Location | Native RmlUi geometry and fonts passed; 5 languages, 172 you need characters |
| Isolated runtime | 279 files from the checksum before/after copying; `check/snapshot.json` |
| Target CTest | `check/selected.xml`: **3/3 PASS** — terrain_v02 107.50 s, ui 8.37 s, client_input 32.14 s |
| The usual start | `ordinary-outcrops/ordinary-seeds-report.json`: **8/8**, real `World(seed,10)`, 1 camp / 10 people / 861 known cell |
| Short native matrix | `matrix-outcrops/report.json`: ** 6/6 **, 15 frames, F1 hide/restore true |
| Full native matrix | `matrix-full/report.json`: **60/60**, 123 frames; 5 languages × 4 scale × 3 resolution; F1 hide/restore true |
| Independent code / pixel review | The narrow step is taken; general appearance not accepted |

Native backend: Direct3D 12, NVIDIA GeForce RTX 3050 Laptop GPU. The full array checks the rendering and layout, doesn't take all the graphics. All 123 files are there; The manual pixel review is selective, not all 123 images. Reviewed seed1337/70421, summer EN100 1080p, winter UK85 720p, UK130/ES130 720p and additional PL130/RU115/ES100 footage.

No apparent new circumcision HUD, overlapping of panels, seams of relief or hanging of stone in these frames were found. This is not proof of the impossibility of natural overlapping of resources by neighboring spray in all states. There's no separate new interactive desktop drive and outcrop pixel drive on all zooms.

## Real start cards, not synthetic tests

[Diagnosis](../../artifacts/outcrop-hud-2026-10-05/diagnostic/actual-start-results.json) counts the helper + apron composition to screen culling, not visible on the stone screen. Source, assembly command, exe and obj are stored next to each other.

| Seed | Useful cells after cell-apron | Groups after distance and subcell-apron | Details |
|---|---:|---:|---:|
| 70421 | 11 | 2 | 5 |
| 4204434016 | 12 | 2 | 6 |
| 572988801 | 9 | 2 | 4 |
| 2035859997 | 14 | 3 | 6 |
| 301968671 | 2 | 0 | 0 |
| 2247006640 | 10 | 2 | 6 |
| 1337 | 32 | 7 | 17 |
| 3735928559 | 12 | 3 | 7 |

In all 8 world checksums before/after is the same and there is no seasonal shift of details. The groups are in 7/8: in seed301968671 both suitable cells are seeded with a coordinate priority. It's not a hidden seed pass, and it's not guaranteed to be visible on every starting map. Neither seed, land, nor resources were edited for this purpose.

## Identification of the collection and boundaries

185 assets; content SHA-256 `87e0581403150f6e81ab259a233643f622a91d184ee910bcc1bf9a4e9d81639b`. Five executable files after the tests are reset from the runtime snapshot:

- Ribterra: `7990C4E1E01D1E551622003D2282C01B2DF20D5AFAD533C7AAC7CC5C0FE2850D`
- ReworkVisualProbe: `9CB6D832182738BD90158D4FC6B35C5CA68988A75AF9C64A5C8037E6384E0ADD`
- ClientInputTests: `1CE731A5F05C2D3EB5C45E20A31B81009D56DFB0A19C598346708D583E06378A`
- TerrainV02Tests: `8ECAEE83F7C6753C93F5664795C091DD23CF2B1F28E0E2E9EB96A0E276A4B4EC`
- UiTests: `B9E7CAA0AED209811525E661A860EF9C2B4051E48D7B4849275945C7965E6598`

The usual PNG before/after have the same declared seed/camera but not a complete per-capture checksum/weather passport. The diagnosis is the helplessness of the helper, not the integrity of all the stages in the frame. The content HUD varies between passages. The snapshot is isolated from the source files, not a sealed copy of the entire repository.

The full CTest suite was not launched at this stage. The last general Gen8 strain remains **84/92**, with subsequent successful selective patches; There's no fresh one 92/92. The old 92/92 preview r2 doesn't replace that. `Play Ribterra Rework.cmd` and the installed r2 are unchanged; His exe SHA-256 `DCF9514876AD73ED0B4EDA8E6DFBE1C452835A4E478B6AF007DB657E58706B35` is reconfirmed. There was no commit/push, no release releases or two-PC/WAN- reception.

The tests removed only their own save/settings in the new `check`, with exact paths reported prior to launch. Previous files, evidence and user savings are saved.

## The impact of the skills in question and the next criteria

`ponytail` directed reuse of existing stone art and draw path without a new subsystem. `karpathy-guidelines` — clear conditions of acceptance and narrow changes; `cpp-coding-standards` — pure value-based helper on `std::array` without owning raw pointers. `ultrawork` — separate parallel performers HUD and helper, independent code/pixel verification; The root was doing the test.

Current coastal silhouettes are already repeating on the slopes; And you don't have to just multiply them. The next distinct artistic step is the summer/winter oriented rock steps with a distinct pedestrian and upper edge attached to the actual gradient/curve with the same masks. New same-seed native comparisons and control of seasonal geometry are needed. The large gray exploration boundary, composition/density of the settlement and the correspondence of all the details of the concept remain open. Global zoom or artificial map unlocking is not used.
