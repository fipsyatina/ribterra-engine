"""Probe-only P/N MRT ABI. Not part of the production shader manifest."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(source, output, dxc):
    base = "material_calibration.frag"
    metadata = json.loads((output / f"{base}.json").read_text(encoding="utf-8"))
    # shadercross JSON counts all five declarations in the included production
    # source; the optimized DXIL bindings below must use only P/N's first three.
    require(metadata["samplers"] == 5, "Calibration must retain the five declared production sampler slots")
    require(metadata["uniform_buffers"] == 2, "Calibration must retain production b0/b1")
    require(metadata["storage_buffers"] == metadata["storage_textures"] == 0,
            "Calibration has unexpected storage resources")
    require({row["location"]: row["type"] for row in metadata["inputs"]} == {0: "float2", 1: "float4"},
            "Calibration varying ABI does not match the production sprite vertex shader")
    require({row["location"]: row["type"] for row in metadata["outputs"]} == {0: "float4", 1: "float4"},
            "Calibration must write independent float position and normal targets")
    dumped = subprocess.run([str(dxc), "-dumpbin", str(output / f"{base}.dxil")],
                            capture_output=True, text=True, check=True).stdout
    definitions = re.sub(r"(?m)^\s*; ?", "", dumped.split("; Buffer Definitions:", 1)[1]
                         .split("; Resource Bindings:", 1)[0])
    layout = re.search(r"cbuffer\s+(?:type_)?SpriteSurface\b(?P<body>.*?)\}\s*(?:type_)?SpriteSurface;"
                       r"\s*;\s*Offset:\s*0\s+Size:\s*(\d+)", definitions, re.S)
    require(layout is not None and int(layout.group(2)) == 160,
            "Calibration SpriteSurface must retain its complete 160-byte ABI")
    for field, offset in (("uv_regions", 0), ("anchor_export", 80), ("source_size_pivot", 96),
                          ("depth_scale", 112), ("emission", 128), ("response", 144)):
        require(re.search(rf"\b(?:SpriteSurface_)?{field}(?:\[5\])?;+\s*;\s*Offset:\s*{offset}\b", layout.group("body")),
                f"Calibration SpriteSurface.{field} ABI changed")
    bindings = dumped.split("; Resource Bindings:", 1)[1].split("; ViewId state:", 1)[0]
    actual = set(re.findall(r"\b((?:cb|t|s)\d+),space(\d+)\s+(\d+)\s*$", bindings, re.M))
    expected = {(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(3)} | {
        ("cb0", "3", "1"), ("cb1", "3", "1")}
    require(actual == expected, f"Calibration resource slots changed: {actual}")
    return {"schema": 1, "scope": "probe-only direct float P/N; not a production graph",
            "diagnostic_source_sha256": digest(source / f"{base}.hlsl"),
            "production_material_source_sha256": digest(source / "material_sprite.frag.hlsl"),
            "binary_sha256": digest(output / f"{base}.dxil"),
            "reflection_sha256": digest(output / f"{base}.json"),
            "declared_fragment_samplers": 5, "active_dxil_fragment_samplers": 3, "fragment_uniforms": 2,
            "sprite_surface_bytes": 160, "outputs": ["float4-position", "float4-normal"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("dxc", type=Path)
    parser.add_argument("--write-contract", action="store_true")
    args = parser.parse_args()
    expected = validate(args.source, args.output, args.dxc)
    path = args.output / "contract.json"
    if args.write_contract:
        path.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
    else:
        require(json.loads(path.read_text(encoding="utf-8")) == expected, "Stale calibration shader contract")
    print("Probe-only material calibration shader ABI passed")


if __name__ == "__main__":
    main()
