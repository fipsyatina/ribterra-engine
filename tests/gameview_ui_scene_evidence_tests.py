"""Read-only, nonvacuous World/HUD pixel checks for preserved UI native runs."""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def visible_world(raw: bytes) -> tuple[int, int]:
    require(len(raw) == 1920 * 1080 * 4, "World pixel extent differs")
    pixels = set(zip(raw[0::4], raw[1::4], raw[2::4], raw[3::4]))
    nonblack = sum(any(raw[index:index + 3]) for index in range(0, len(raw), 4))
    require(nonblack > 10000 and len(pixels) > 64, "Standalone World pixels are vacuous")
    return nonblack, len(pixels)


def verify(root: Path) -> dict:
    report = json.loads((root / "ui-report.json").read_text(encoding="utf-8"))
    require(report["complete"] is True, "Native report is incomplete")
    pictures = {}
    for name in ("ui-world-before.png", "ui-world-after.png", "ui-invalid-recovered.png", "ui-rml-hud.png"):
        with Image.open(root / name) as source:
            picture = source.convert("RGBA")
            require(picture.size == (1920, 1080), "Decoded World extent differs")
            pictures[name] = picture
    before = pictures["ui-world-before.png"].tobytes()
    nonblack, unique = visible_world(before)
    require(before == pictures["ui-world-after.png"].tobytes() ==
            pictures["ui-invalid-recovered.png"].tobytes(), "Recovered visible World pixels differ")
    # This declared central witness excludes the captured HUD's top/bottom/right panels.
    # It proves preserved scene pixels in this fixture, not every panel's clip coverage.
    region = (280, 100, 1590, 850)
    world_region = pictures["ui-world-before.png"].crop(region).tobytes()
    hud_region = pictures["ui-rml-hud.png"].crop(region).tobytes()
    require(world_region == hud_region, "HUD changed the declared central World witness")
    region_nonblack = sum(any(world_region[index:index + 3])
                          for index in range(0, len(world_region), 4))
    require(region_nonblack > 10000, "Central World witness is vacuous")
    return {"world_nonblack_pixels": nonblack, "world_unique_rgba": unique,
            "central_witness": list(region), "central_nonblack_pixels": region_nonblack,
            "central_world_hud_exact": True, "recovered_world_exact": True}


def main() -> int:
    require(len(sys.argv) == 3, "Expected two preserved native output roots")
    # The visibility oracle must fail on an opaque black image, without writing any file.
    try:
        visible_world(bytes((0, 0, 0, 255)) * (1920 * 1080))
    except ValueError:
        pass
    else:
        raise ValueError("Black-frame negative control was accepted")
    first, second = (verify(Path(argument)) for argument in sys.argv[1:])
    require(first == second, "Repeated World visibility witnesses differ")
    print("UI visible World evidence checks passed; " + json.dumps(first, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f"UI visible World evidence check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
