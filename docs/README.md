# Engine documentation

Start with the repository [README](../README.md) for the project idea, screenshots, intended audience, technology stack, build instructions, and the complete command-to-frame lifecycle.

## Architecture and core systems

- [Architecture](ARCHITECTURE.md): module ownership, simulation boundaries, networking, rendering, and persistence contracts.
- [Engine requirements](ENGINE_REQUIREMENTS.md): requirement IDs, implementation status, and acceptance criteria.
- [Physical logistics](LOGISTICS_PLAN.md): item ownership, reservations, carrying, delivery, and cancellation.
- [Manual commands](MANUAL_ORDERS.md): command queues, per-resident results, execution phases, and control ownership.
- [Save format](SAVE_FORMAT.md): serialized world state, versioning, validation, and migration fixtures.
- [Assets](ASSETS.md): asset registration, staging, hashes, font coverage, and atlas tooling.
- [Skills](SKILLS.md): independent skill progress and server-side modifier calculations.

## Rendering and visual development

- [Rendering core specification](visual-targets/RENDER_CORE_SPEC_UA.md).
- [Dynamic lighting specification](visual-targets/DYNAMIC_LIGHTING_SPEC_UA.md).
- [World effects specification](visual-targets/WORLD_FX_SPEC_UA.md).
- [Rendering development screenshots](screenshots/README.md).
- [Visual targets](visual-targets/README.md): distinguishes generated future targets from runtime evidence.

## Historical evidence and provenance

- [Engine and game status](ENGINE_GAME_STATUS_2026_10_08_UA.md).
- [Validation records](VALIDATION.md).
- [Original game overview](RIBTERRA_PROJECT_README.md).
- [Initial source import manifest](SOURCE_SNAPSHOT_2026_10_12.json).
- [English documentation update and screenshot provenance](DOCUMENTATION_UPDATE_2026_10_12.json).
- [CI configuration example](ci/build.yml).

Documentation and explanatory pages are in English. Legacy filename suffixes such as `_UA` and `-uk` remain stable to preserve existing links and recorded paths. Runtime translations, localized fallback strings, and Unicode test fixtures remain multilingual.

Historical documents describe the versions and source snapshots named in each record. Translating a report does not rerun its tests or validate the current engine. Original wording is retained in Git history at import commit `7388a2f`; documentation translations and supplied screenshots are follow-up changes.

The primary overview and core specifications were manually translated and reviewed. The larger historical archive uses offline contextual translation with protected technical literals and focused editorial checks. Consult the original import commit for exact historical wording. The documentation-update manifest records the method, scope, hashes, and verification limits.
