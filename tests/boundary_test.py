"""Keep platform, transport and presentation APIs outside SimCore contracts."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
include_dir = root / "include"
pending = [include_dir / "ribterra/world.hpp", include_dir / "ribterra/world_state.hpp"]
visited = set()
forbidden = ("sdl", "steam", "rmlui", "miniaudio", "windows.h", "winsock", "ribterra/net", "ribterra/view", "ribterra/client", "ribterra/game_server")
while pending:
    header = pending.pop()
    if header in visited:
        continue
    visited.add(header)
    for dependency in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', header.read_text(encoding="utf-8"), re.M):
        assert not any(word in dependency.lower() for word in forbidden), f"Forbidden SimCore dependency: {header}: {dependency}"
        local = include_dir / dependency
        if local.is_file():
            pending.append(local)
for source in (root / "src/sim").glob("*.cpp"):
    for dependency in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', source.read_text(encoding="utf-8"), re.M):
        assert not any(word in dependency.lower() for word in forbidden), f"Forbidden SimCore implementation dependency: {source}: {dependency}"
print(f"SimCore boundary passed: {len(visited)} public headers and all simulation sources")

# Renderer2D consumes only render descriptions; it cannot reach live game state.
render_sources = [include_dir / "ribterra/render_frame.hpp", include_dir / "ribterra/renderer2d.hpp"]
render_sources += list((root / "src/render").glob("*.cpp"))
for source in render_sources:
    for dependency in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', source.read_text(encoding="utf-8"), re.M):
        if dependency.startswith("ribterra/"):
            assert dependency in ("ribterra/render_frame.hpp", "ribterra/renderer2d.hpp"), f"Renderer must not access simulation or commands: {source}: {dependency}"
print("Renderer2D boundary passed: no gameplay or networking dependency")
