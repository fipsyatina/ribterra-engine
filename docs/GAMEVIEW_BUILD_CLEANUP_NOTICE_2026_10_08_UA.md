# The message before cleaning GameView compiled-copy — 08.10.2026

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

At the user's direct request not to accumulate older versions only 102 specific generated compiled files in four old test roots are prepared. This message was created by DO Apply; The state at the time of creation — nothing on this list has been removed yet.

Plan: `.omx/plans/gameview-build-prune-2026-10-08-01.json`.

Plan SHA-256: `2F0E381B68026EAF596D8F58C8AE4BEE6A2DD45CA2DCCF0E7C1B8C0980EDE56A`.

Helper SHA-256: `B376B91302865938664C8A78540AE0D4E0E470E6F8D9A9BC914E96B3F8680307`.

Summary **102 files / 400 058 964 logical bytes (400,06 MB) **. It's not a promise of equal growth of free space: the actual volume is measured after the operation.

Reason: try06–09 replaced with a fresh strict build10 and two repeated bounded native06/07. The OBJ/EXE/DLL/LIB/EXP is not needed for the current drive.

Protected 3859 files: all non-target files of these four roots, GameView05 rollback and 10 latest, all GameView native01–07, sky-authored05/10 and authored-native01–06, all kit check05 (A/B and negative evidence), 96 live source bindings, launcher/pointer, and actual launches EXE. Catalogs, source code, reports, screenshots/PNG, assets, shaders, generated headers and compiler-temp are NOT deleted.

Removing — final local `Remove-Item`, not a basket and not GitHub archives. The stored snapshots/logs/dependencies allow the functionally relevant set to be restored; The exact restoration of the historical EXE/OBJ is not guaranteed. The initial latest/rollback copies remain.

## A Complete and Accurate List of Goals

### gameview-engine-2026-10-08-attempt06

21 files; 59793017 logical bytes.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt06\obj\world.obj
```

### gameview-engine-2026-10-08-attempt07

27 files; 113414047 logical bytes.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt07\SDL3.dll
```

### gameview-engine-2026-10-08-attempt08

27 files; 113424631 logical bytes.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt08\SDL3.dll
```

### gameview-engine-2026-10-08-attempt09

27 files; 113427269 logical bytes.

```text
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.exe
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.exp
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\GameViewEngineProbe.lib
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\asset_catalog.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\atomic_file.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\client_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\contact_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\economy.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\foundation.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\game_art.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\game_server.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\gameview_engine_probe.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\gns_transport.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\lobby.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\localization.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\network_session.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\platform.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\protocol.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\renderer2d.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\skills.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\sky_shadow.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\texture_atlas.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\view.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\world_save.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\obj\world.obj
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\SDL3_ttf.dll
C:\Users\fipst\Desktop\ribterra\build\gameview-engine-2026-10-08-attempt09\SDL3.dll
```

## Before and after checks

Apply is only allowed for the reported unchanged Plan SHA. Helper re-creates paths, bytes, mtime and SHA; After deleting, it checks the entire protected inventory and zeroes the rest of the targets. The actual result will be a separate `.json.result.json`, without rewriting this Plan. Until the result, no cleansing is declared.

