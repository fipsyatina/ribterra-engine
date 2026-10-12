"""Validate compiled shader interfaces against Renderer2D and its asset manifest."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


PROGRAMS = {
    "sprite": {
        "vertex_inputs": {0: "float2", 1: "float2", 2: "float4"},
        "vertex_streams": [
            {"slot": 0, "stride": 20, "step_rate": "vertex",
             "attributes": {0: "float2", 1: "float2", 2: "unorm8x4"}},
        ],
        "fragment_uniforms": 0,
        "fragment_bindings": {("t0", "2", "1"), ("s0", "2", "1")},
        "linked_interface": {0: "float2", 1: "float4"},
        "color_space": "legacy encoded texture multiplied by normalized vertex color",
        "alpha": "sampled texture alpha multiplied by normalized vertex alpha",
    },
    "surface_lit": {
        "vertex_inputs": {0: "float2", 1: "float2", 2: "float4", 3: "float3", 4: "float3"},
        "vertex_streams": [
            {"slot": 0, "stride": 20, "step_rate": "vertex",
             "attributes": {0: "float2", 1: "float2", 2: "unorm8x4"}},
            {"slot": 1, "stride": 24, "step_rate": "vertex",
             "attributes": {3: "float3", 4: "float3"}},
        ],
        "fragment_uniforms": 1,
        "fragment_bindings": {("t0", "2", "1"), ("s0", "2", "1"), ("cb0", "3", "1")},
        "linked_interface": {0: "float2", 1: "float4", 2: "float3", 3: "float3"},
        "color_space": "sRGB albedo decode once, linear lighting, explicit sRGB encode to RGBA8_UNORM",
        "alpha": "opaque",
    },
}

PROGRAMS["surface_hdr"] = dict(PROGRAMS["surface_lit"],
    color_space="sRGB albedo decode once, unencoded linear HDR lighting",
    target_format="R16G16B16A16_FLOAT",
    includes={"vert": ["surface_lit.vert.hlsl"], "frag": ["surface_lit.frag.hlsl"]})
PROGRAMS["world_linear"] = dict(PROGRAMS["sprite"],
    fragment_uniforms=1,
    fragment_bindings={("t0", "2", "1"), ("s0", "2", "1"), ("cb0", "3", "1")},
    color_space="legacy encoded byte product decoded once into linear HDR",
    alpha="straight or unpremultiplied-decode-repremultiplied linear coverage, selected by blend",
    target_format="R16G16B16A16_FLOAT",
    includes={"vert": ["sprite.vert.hlsl"], "frag": ["color_transfer.hlsli"]})
PROGRAMS["hdr_composite"] = {
    "vertex_inputs": {}, "vertex_streams": [], "vertex_uniforms": 0,
    "fragment_uniforms": 1,
    "fragment_bindings": {("t0", "2", "1"), ("s0", "2", "1"), ("cb0", "3", "1")},
    "linked_interface": {0: "float2"},
    "color_space": "linear HDR exposure, one explicit sRGB encode and SDR clamp; UI excluded",
    "alpha": "opaque final scene",
    "includes": {"frag": ["color_transfer.hlsli"]},
}
PROGRAMS["material_sprite"] = dict(PROGRAMS["sprite"],
    fragment_samplers=5,
    fragment_uniforms=2,
    fragment_bindings={(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(5)} |
                      {("cb0", "3", "1"), ("cb1", "3", "1")},
    color_space="hardware sRGB albedo decode before filtering; linear data maps and HDR lighting/emission",
    alpha="straight linear coverage; alpha blend into opaque HDR scene",
    target_format="R16G16B16A16_FLOAT",
    includes={"vert": ["sprite.vert.hlsl"], "frag": ["atmosphere.hlsli"]})
PROGRAMS["ground_gbuffer"] = dict(PROGRAMS["surface_lit"],
    fragment_outputs={0: "float4", 1: "float4", 2: "float4", 3: "float4"},
    color_targets=[
        {"location": 0, "name": "ground_albedo", "format": "R8G8B8A8_UNORM_SRGB", "blend": "OPAQUE"},
        {"location": 1, "name": "surface_world", "format": "R16G16B16A16_FLOAT", "blend": "OPAQUE"},
        {"location": 2, "name": "surface_material", "format": "R8G8B8A8_UNORM", "blend": "OPAQUE"},
        {"location": 3, "name": "surface_flags", "format": "R8G8B8A8_UNORM", "blend": "OPAQUE"},
    ],
    color_space="typed sRGB albedo; camera-relative world, oct normal and bounded material data",
    alpha="opaque MRT with albedo alpha carrying authored ambient/contact visibility",
    includes={"vert": ["surface_lit.vert.hlsl"]})
PROGRAMS["ground_lighting"] = {
    "vertex_inputs": {}, "vertex_streams": [], "vertex_uniforms": 0,
    "fragment_samplers": 4, "fragment_uniforms": 1, "fragment_storage_buffers": 1,
    "fragment_bindings": {(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(4)} |
                         {("t4", "2", "1"), ("cb0", "3", "1")},
    "linked_interface": {0: "float2"},
    "fragment_outputs": {0: "float4", 1: "float4"},
    "color_targets": [
        {"location": 0, "name": "scene_linear", "format": "R16G16B16A16_FLOAT", "blend": "OPAQUE"},
        {"location": 1, "name": "world_meta", "format": "R16G16_FLOAT", "blend": "OPAQUE"},
    ],
    "color_space": "linear ground lighting from point-sampled GBuffer",
    "alpha": "opaque valid ground; invalid GBuffer pixels write zero",
    "includes": {"vert": ["hdr_composite.vert.hlsl"],
                 "frag": ["local_occlusion.hlsli", "atmosphere.hlsli"]},
}
PROGRAMS["material_forward"] = dict(PROGRAMS["material_sprite"],
    fragment_uniforms=3, fragment_storage_buffers=1,
    fragment_bindings={(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(5)} |
                      {("t5", "2", "1")} |
                      {(f"cb{slot}", "3", "1") for slot in range(3)},
    fragment_outputs={0: "float4", 1: "float4"},
    color_targets=[
        {"location": 0, "name": "scene_linear", "format": "R16G16B16A16_FLOAT",
         "blend": "SRC_ALPHA,ONE_MINUS_SRC_ALPHA"},
        {"location": 1, "name": "world_meta", "format": "R16G16_FLOAT",
         "blend": "ONE,ONE_MINUS_SRC_ALPHA"},
    ],
    color_space="unchanged schema-2 material radiance plus coverage-weighted forward metadata",
    alpha="straight-alpha scene and premultiplied depth/reveal metadata",
    includes={"vert": ["sprite.vert.hlsl"],
              "frag": ["local_occlusion.hlsli", "material_sprite.frag.hlsl",
                       "atmosphere.hlsli"]})
PROGRAMS["ground_contact"] = {
    "vertex_inputs": {}, "vertex_streams": [], "vertex_uniforms": 0,
    "fragment_samplers": 3, "fragment_uniforms": 1,
    "fragment_bindings": {(f"{kind}{slot}", "2", "1")
                          for kind in ("t", "s") for slot in range(3)} |
                         {("cb0", "3", "1")},
    "linked_interface": {0: "float2"},
    "fragment_outputs": {0: "float4"},
    "color_targets": [{
        "location": 0, "name": "ground_albedo", "format": "R8G8B8A8_UNORM_SRGB",
        "write_mask": "A", "blend": "ZERO,ONE_MINUS_SRC_ALPHA,ADD"}],
    "sampler_contracts": [
        {"slot": 0, "name": "surface_world", "filter": "NEAREST", "address": "CLAMP"},
        {"slot": 1, "name": "surface_flags", "filter": "NEAREST", "address": "CLAMP"},
        {"slot": 2, "name": "contact_mask", "filter": "LINEAR", "address": "CLAMP", "lod": 0},
    ],
    "active_target_reads": [],
    "color_space": "linear R8 contact mask attenuates only stored ground ambient/contact visibility alpha",
    "alpha": "mask times opacity; RGB writes disabled; destination alpha multiplicative attenuation",
    "includes": {"vert": ["hdr_composite.vert.hlsl"]},
}
PROGRAMS["ground_contact_gbuffer"] = dict(PROGRAMS["ground_gbuffer"],
    color_space="contact graph typed receiver tag in SurfaceWorld.W; packed flags unchanged",
    includes={"vert": ["ground_gbuffer.vert.hlsl", "surface_lit.vert.hlsl"],
              "frag": ["ground_gbuffer.frag.hlsl"]})
PROGRAMS["ground_contact_lighting"] = dict(PROGRAMS["ground_lighting"],
    color_space="contact graph lighting accepts only exact SurfaceWorld.W tags 1 and 2",
    includes={"vert": ["ground_lighting.vert.hlsl", "hdr_composite.vert.hlsl"],
              "frag": ["ground_lighting.frag.hlsl", "local_occlusion.hlsli",
                       "atmosphere.hlsli"]})
PROGRAMS["ground_sky_lighting"] = dict(PROGRAMS["ground_contact_lighting"],
    fragment_uniforms=2,
    fragment_bindings={(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(4)} |
                      {("t4", "2", "1"), ("cb0", "3", "1"), ("cb1", "3", "1")},
    color_space="contact graph ground lighting plus independent bounded moon direct light",
    includes={"vert": ["ground_lighting.vert.hlsl", "hdr_composite.vert.hlsl"],
              "frag": ["ground_contact_lighting.frag.hlsl", "ground_lighting.frag.hlsl",
                       "local_occlusion.hlsli", "atmosphere.hlsli"]})
PROGRAMS["material_sky_forward"] = dict(PROGRAMS["material_forward"],
    fragment_uniforms=4,
    fragment_bindings={(f"{kind}{slot}", "2", "1") for kind in ("t", "s") for slot in range(5)} |
                      {("t5", "2", "1")} |
                      {(f"cb{slot}", "3", "1") for slot in range(4)},
    color_space="schema-2 forward lighting plus independent bounded moon direct light and unchanged metadata",
    includes={"vert": ["material_forward.vert.hlsl", "sprite.vert.hlsl"],
              "frag": ["material_forward.frag.hlsl", "local_occlusion.hlsli",
                       "material_sprite.frag.hlsl", "atmosphere.hlsli"]})
PROGRAMS["ground_shadow_lighting"] = dict(PROGRAMS["ground_sky_lighting"],
    fragment_samplers=6, fragment_sampler_slots=[0, 1, 2, 3, 4, 5],
    fragment_uniforms=3,
    fragment_bindings={(f"{kind}{slot}", "2", "1")
                       for kind in ("t", "s") for slot in range(6)} |
                      {("t6", "2", "1")} |
                      {(f"cb{slot}", "3", "1") for slot in range(3)},
    sampler_contracts=[
        {"slot": 4, "name": "sun_shadow", "filter": "NEAREST", "address": "CLAMP", "lod": 0},
        {"slot": 5, "name": "moon_shadow", "filter": "NEAREST", "address": "CLAMP", "lod": 0},
    ],
    shadow_maps=[{"slot": 4, "source": "sun", "extent": 1024, "format": "D32_FLOAT"},
                 {"slot": 5, "source": "moon", "extent": 1024, "format": "D32_FLOAT"}],
    color_space="contact ground plus independent sun/moon direct visibility from bounded depth maps",
    includes={"vert": ["ground_sky_lighting.vert.hlsl", "ground_lighting.vert.hlsl",
                       "hdr_composite.vert.hlsl"],
               "frag": ["sky_shadow.hlsli", "ground_sky_lighting.frag.hlsl",
                        "ground_contact_lighting.frag.hlsl", "ground_lighting.frag.hlsl",
                        "local_occlusion.hlsli", "atmosphere.hlsli"]})
PROGRAMS["material_shadow_forward"] = dict(PROGRAMS["material_sky_forward"],
    fragment_samplers=7, fragment_sampler_slots=[0, 1, 2, 3, 4, 5, 6],
    fragment_uniforms=4,
    fragment_bindings={(f"{kind}{slot}", "2", "1")
                       for kind in ("t", "s") for slot in range(7)} |
                       {("t7", "2", "1")} |
                       {(f"cb{slot}", "3", "1") for slot in range(4)},
    sampler_contracts=[
        {"slot": 5, "name": "sun_shadow", "filter": "NEAREST", "address": "CLAMP", "lod": 0},
        {"slot": 6, "name": "moon_shadow", "filter": "NEAREST", "address": "CLAMP", "lod": 0},
    ],
    shadow_maps=[{"slot": 5, "source": "sun", "extent": 1024, "format": "D32_FLOAT"},
                 {"slot": 6, "source": "moon", "extent": 1024, "format": "D32_FLOAT"}],
    color_space="schema-2 forward plus independent sun/moon direct visibility and unchanged metadata",
    includes={"vert": ["material_sky_forward.vert.hlsl", "material_forward.vert.hlsl",
                       "sprite.vert.hlsl"],
               "frag": ["sky_shadow.hlsli", "material_sky_forward.frag.hlsl",
                        "material_forward.frag.hlsl", "local_occlusion.hlsli",
                        "material_sprite.frag.hlsl", "atmosphere.hlsli"]})
PROGRAMS["sky_shadow_depth"] = {
    "vertex_inputs": {0: "float3"},
    "vertex_streams": [{"slot": 0, "stride": 12, "step_rate": "vertex",
                        "attributes": {0: "float3"}}],
    "vertex_uniforms": 1,
    "fragment_samplers": 0, "fragment_uniforms": 0, "fragment_bindings": set(),
    "linked_interface": {}, "fragment_outputs": {}, "color_targets": [],
    "color_space": "depth-only directional shadow raster in normalized light space",
    "alpha": "no color output; D32_FLOAT depth uses LESS and opaque cull-none rasterization",
}

PROGRAMS["water_ground_gbuffer"] = dict(PROGRAMS["ground_contact_gbuffer"],
    fragment_uniforms=2,
    fragment_bindings={("t0", "2", "1"), ("s0", "2", "1"),
                       ("cb0", "3", "1"), ("cb1", "3", "1")},
    fragment_outputs={0: "float4", 1: "float4", 2: "float4", 3: "float4", 4: "float4"},
    color_targets=[
        {"location": 0, "name": "ground_albedo", "format": "R8G8B8A8_UNORM_SRGB", "blend": "OPAQUE"},
        {"location": 1, "name": "surface_world", "format": "R16G16B16A16_FLOAT", "blend": "OPAQUE"},
        {"location": 2, "name": "surface_material", "format": "R8G8B8A8_UNORM", "blend": "OPAQUE"},
        {"location": 3, "name": "surface_flags", "format": "R8G8B8A8_UNORM", "blend": "OPAQUE"},
        {"location": 4, "name": "surface_coverage", "format": "R8G8B8A8_UNORM", "blend": "OPAQUE"},
    ],
    color_space="contact tags remain exact; linear authored water coverage is isolated in MRT4",
    alpha="opaque ground MRTs plus independent R8-compatible coverage in target 4",
    includes={"vert": ["ground_contact_gbuffer.vert.hlsl", "ground_gbuffer.vert.hlsl",
                       "surface_lit.vert.hlsl"],
              "frag": ["ground_gbuffer.frag.hlsl"]})
PROGRAMS["water_ground_lighting"] = dict(PROGRAMS["ground_shadow_lighting"],
    fragment_samplers=9, fragment_sampler_slots=list(range(9)),
    fragment_uniforms=4,
    fragment_bindings={(f"{kind}{slot}", "2", "1")
                       for kind in ("t", "s") for slot in range(9)} |
                      {("t9", "2", "1")} |
                      {(f"cb{slot}", "3", "1") for slot in range(4)},
    sampler_contracts=[
        {"slot": slot, "name": name, "filter": filter_name, "address": address, "lod": 0}
        for slot, name, filter_name, address in (
            (0, "ground_albedo", "NEAREST", "CLAMP"),
            (1, "surface_world", "NEAREST", "CLAMP"),
            (2, "surface_material", "NEAREST", "CLAMP"),
            (3, "surface_flags", "NEAREST", "CLAMP"),
            (4, "surface_coverage", "NEAREST", "CLAMP"),
            (5, "water_reflection", "LINEAR", "CLAMP"),
            (6, "water_normal", "LINEAR", "REPEAT"),
            (7, "sun_shadow", "NEAREST", "CLAMP"),
            (8, "moon_shadow", "NEAREST", "CLAMP"))],
    shadow_maps=[{"slot": 7, "source": "sun", "extent": 1024, "format": "D32_FLOAT"},
                 {"slot": 8, "source": "moon", "extent": 1024, "format": "D32_FLOAT"}],
    color_space="opaque HDR ground with point-clipped water coverage and linear half-resolution reflection",
    alpha="opaque valid ground; coverage gates reflection but never output alpha",
    includes={"vert": ["ground_lighting.vert.hlsl", "hdr_composite.vert.hlsl"],
              "frag": ["sky_shadow.hlsli", "ground_lighting.frag.hlsl",
                       "local_occlusion.hlsli", "atmosphere.hlsli"]})
PROGRAMS["water_reflection"] = dict(PROGRAMS["material_shadow_forward"],
    fragment_samplers=7, fragment_sampler_slots=list(range(7)),
    fragment_uniforms=4,
    fragment_bindings={(f"{kind}{slot}", "2", "1")
                       for kind in ("t", "s") for slot in range(7)} |
                      {("t7", "2", "1")} |
                      {(f"cb{slot}", "3", "1") for slot in range(4)},
    fragment_outputs={0: "float4"},
    color_targets=[{"location": 0, "name": "water_reflection", "format": "R16G16B16A16_FLOAT",
                    "blend": "ONE,ONE_MINUS_SRC_ALPHA"}],
    sampler_contracts=[
        {"slot": slot, "name": name, "filter": filter_name, "address": "CLAMP", "lod": 0}
        for slot, name, filter_name in (
            (0, "albedo", "LINEAR"), (1, "normal", "LINEAR"), (2, "orm", "LINEAR"),
            (3, "emission", "LINEAR"), (4, "mask", "LINEAR"),
            (5, "sun_shadow", "NEAREST"), (6, "moon_shadow", "NEAREST"))],
    shadow_maps=[{"slot": 5, "source": "sun", "extent": 1024, "format": "D32_FLOAT"},
                 {"slot": 6, "source": "moon", "extent": 1024, "format": "D32_FLOAT"}],
    color_space="linear HDR reflected source radiance",
    alpha="premultiplied reflection coverage with ONE,ONE_MINUS_SRC_ALPHA blending",
    includes={"vert": ["material_shadow_forward.vert.hlsl", "material_sky_forward.vert.hlsl",
                       "material_forward.vert.hlsl", "sprite.vert.hlsl"],
              "frag": ["sky_shadow.hlsli", "local_occlusion.hlsli",
                       "material_sprite.frag.hlsl", "atmosphere.hlsli"]})

PROGRAMS["atmosphere_ground_lighting"] = dict(PROGRAMS["water_ground_lighting"],
    color_space="opaque HDR water ground lighting plus bounded source-space atmospheric haze",
    includes={"vert": ["water_ground_lighting.vert.hlsl", "ground_lighting.vert.hlsl",
                       "hdr_composite.vert.hlsl"],
              "frag": ["water_ground_lighting.frag.hlsl", "sky_shadow.hlsli",
                       "ground_lighting.frag.hlsl", "local_occlusion.hlsli",
                       "atmosphere.hlsli"]})
PROGRAMS["atmosphere_material_forward"] = dict(PROGRAMS["material_shadow_forward"],
    color_space="schema-2 shadowed forward lighting plus bounded source-space atmospheric haze",
    includes={"vert": ["material_shadow_forward.vert.hlsl", "material_sky_forward.vert.hlsl",
                       "material_forward.vert.hlsl", "sprite.vert.hlsl"],
              "frag": ["material_shadow_forward.frag.hlsl", "sky_shadow.hlsli",
                       "material_sky_forward.frag.hlsl", "material_forward.frag.hlsl",
                       "local_occlusion.hlsli", "material_sprite.frag.hlsl",
                       "atmosphere.hlsli"]})
PROGRAMS["atmosphere_water_reflection"] = dict(PROGRAMS["water_reflection"],
    color_space="linear HDR reflected source radiance plus bounded source-space atmospheric haze",
    includes={"vert": ["water_reflection.vert.hlsl", "material_shadow_forward.vert.hlsl",
                       "material_sky_forward.vert.hlsl", "material_forward.vert.hlsl",
                       "sprite.vert.hlsl"],
              "frag": ["water_reflection.frag.hlsl", "sky_shadow.hlsli",
                       "local_occlusion.hlsli", "material_sprite.frag.hlsl",
                       "atmosphere.hlsli"]})


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_dependencies(source_dir, source):
    """Return stable, recursive include closure in first-use order."""
    result = []
    seen = set()

    def visit(path):
        # Follow preprocessor line splicing/comments before enumerating every
        # include. Otherwise angle or macro forms could affect DXIL without
        # appearing in the hashed closure. This bounded contract accepts only
        # quoted relative includes; other forms fail closed.
        source_text = re.sub(r"\\\r?\n", "", path.read_text(encoding="utf-8"))
        source_text = re.sub(r"//[^\r\n]*|/\*.*?\*/",
                             lambda match: "\n" * match.group(0).count("\n") or " ",
                             source_text, flags=re.S)
        for directive in re.finditer(r"^[ \t]*#[ \t]*include\b([^\r\n]*)", source_text, re.M):
            quoted = re.fullmatch(r'\s*"([^"\r\n]+)"\s*', directive.group(1))
            require(quoted is not None, f"Noncanonical shader include in {path.name}: quoted relative path required")
            include = quoted.group(1)
            dependency = (path.parent / include).resolve()
            require(dependency.is_relative_to(source_dir.resolve()),
                    f"Shader include escapes source directory: {include}")
            relative = dependency.relative_to(source_dir.resolve()).as_posix()
            if relative in seen:
                continue
            require(dependency.is_file(), f"Shader include is missing: {relative}")
            seen.add(relative)
            result.append({"source": relative, "sha256": digest(dependency)})
            visit(dependency)

    visit(source)
    return result


def interface(metadata, direction):
    result = {}
    for item in metadata[direction]:
        location = item["location"]
        require(location not in result, f"Duplicate shader {direction} location {location}")
        result[location] = item["type"]
    return result


def manifest_locations(values):
    return {str(location): value for location, value in values.items()}


def validate_surface_lighting_abi(dump):
    definitions = dump.split("; Buffer Definitions:", 1)
    require(len(definitions) == 2, "surface_lit fragment DXIL has no buffer definitions")
    definitions = definitions[1].split("; Resource Bindings:", 1)[0]
    # shadercross round-trips HLSL through SPIR-V, adding type/member prefixes.
    # Strip only the dump comment prefix, retaining reflected byte offsets.
    definitions = re.sub(r"(?m)^\s*; ?", "", definitions)
    frame = re.search(r"cbuffer\s+(?:type_)?SurfaceLighting\b(?P<body>.*?)\}\s*(?:type_)?SurfaceLighting;"
                      r"\s*;\s*Offset:\s*0\s+Size:\s*(?P<size>\d+)", definitions, re.S)
    require(frame is not None, "SurfaceLighting DXIL layout is missing")
    require(int(frame.group("size")) == 384, "SurfaceLighting DXIL size must be 384 bytes")
    body = frame.group("body")
    frame_fields = (("ambient", 0), ("sun_direction", 16),
                    ("sun_color_intensity", 32), ("counts", 48), ("lights", 64))
    for field, offset in frame_fields:
        require(re.search(rf"\b(?:SurfaceLighting_)?{field}(?:\[4\])?;+\s*;\s*Offset:\s*{offset}\b", body),
                f"SurfaceLighting.{field} DXIL offset must be {offset}")
    light = re.search(r"struct\s+(?:struct\.)?LightGpu\s*\{(?P<body>.*?)\}\s*(?:SurfaceLighting_)?lights\[4\]", body, re.S)
    require(light is not None, "SurfaceLighting LightGpu[4] DXIL layout is missing")
    member_offsets = []
    for field, offset in (("anchor_radius", 0), ("direction_cone", 16),
                          ("color_intensity", 32), ("occluder_range_flags", 48),
                          ("height_falloff_reveal", 64)):
        member = re.search(rf"\b{field};\s*;\s*Offset:\s*(\d+)\b", light.group("body"))
        require(member is not None, f"LightGpu.{field} DXIL offset is missing")
        member_offsets.append(int(member.group(1)))
    member_base = member_offsets[0]
    for (field, offset), actual in zip((("anchor_radius", 0), ("direction_cone", 16),
                                        ("color_intensity", 32), ("occluder_range_flags", 48),
                                        ("height_falloff_reveal", 64)), member_offsets):
        require(actual - member_base == offset,
                f"LightGpu.{field} DXIL offset must be {offset}")
    require((int(frame.group("size")) - 64) // 4 == 80,
            "LightGpu DXIL array stride must be 80 bytes")


def validate_atmosphere_surface_lighting_abi(dump):
    definitions = buffer_definitions(dump)
    frame = re.search(r"cbuffer\s+(?:type_)?SurfaceLighting\b(?P<body>.*?)\}\s*(?:type_)?SurfaceLighting;"
                      r"\s*;\s*Offset:\s*0\s+Size:\s*(?P<size>\d+)", definitions, re.S)
    require(frame is not None, "Atmosphere SurfaceLighting DXIL layout is missing")
    require(int(frame.group("size")) == 1024,
            "Atmosphere SurfaceLighting DXIL size must be 1024 bytes")
    body = frame.group("body")
    fields = (("ambient", 0), ("sun_direction", 16),
              ("sun_color_intensity", 32), ("counts", 48), ("lights", 64),
              ("atmosphere_origin_time", 384), ("atmosphere_depth", 400),
              ("atmosphere_density", 416), ("atmosphere_far_color_noise", 432),
              ("atmosphere_morning_color", 448), ("atmosphere_coastal_color", 464),
              ("atmosphere_wind_shore", 480), ("atmosphere_counts_version", 496),
              ("atmosphere_water_aabbs", 512))
    reflected_offsets = {}
    for field, offset in fields:
        count = 4 if field == "lights" else 32 if field == "atmosphere_water_aabbs" else None
        suffix = rf"\[{count}\]" if count is not None else ""
        member = re.search(rf"\b(?:SurfaceLighting_)?{field}{suffix};+\s*;\s*Offset:\s*(\d+)\b", body)
        require(member is not None and int(member.group(1)) == offset,
                f"Atmosphere SurfaceLighting.{field} DXIL offset must be {offset}")
        reflected_offsets[field] = int(member.group(1))
    light = re.search(r"struct\s+(?:struct\.)?LightGpu\s*\{(?P<body>.*?)\}\s*(?:SurfaceLighting_)?lights\[4\]",
                      body, re.S)
    require(light is not None, "Atmosphere SurfaceLighting LightGpu[4] DXIL layout is missing")
    member_offsets = []
    light_fields = (("anchor_radius", 0), ("direction_cone", 16),
                    ("color_intensity", 32), ("occluder_range_flags", 48),
                    ("height_falloff_reveal", 64))
    for field, offset in light_fields:
        member = re.search(rf"\b{field};\s*;\s*Offset:\s*(\d+)\b", light.group("body"))
        require(member is not None, f"Atmosphere LightGpu.{field} DXIL offset is missing")
        member_offsets.append(int(member.group(1)))
    member_base = member_offsets[0]
    for (field, offset), actual in zip(light_fields, member_offsets):
        require(actual - member_base == offset,
                f"Atmosphere LightGpu.{field} DXIL offset must be {offset}")
    require((reflected_offsets["atmosphere_origin_time"] - reflected_offsets["lights"]) // 4 == 80,
            "Atmosphere LightGpu DXIL array stride must be 80 bytes")
    require((int(frame.group("size")) - reflected_offsets["atmosphere_water_aabbs"]) // 32 == 16,
            "Atmosphere water AABB DXIL array stride must be 16 bytes")


def buffer_definitions(dump):
    parts = dump.split("; Buffer Definitions:", 1)
    require(len(parts) == 2, "fragment DXIL has no buffer definitions")
    return re.sub(r"(?m)^\s*; ?", "", parts[1].split("; Resource Bindings:", 1)[0])


def validate_constant_buffer(dump, name, size, fields):
    definitions = buffer_definitions(dump)
    layout = re.search(rf"cbuffer\s+(?:type_)?{name}\b(?P<body>.*?)\}}\s*(?:type_)?{name};"
                       rf"\s*;\s*Offset:\s*0\s+Size:\s*(\d+)", definitions, re.S)
    require(layout is not None and int(layout.group(2)) == size,
            f"{name} DXIL size must be {size} bytes")
    for field, offset in fields.items():
        require(re.search(rf"\b(?:{name}_)?{field}(?:\[\d+\])?;+\s*;\s*Offset:\s*{offset}\b",
                          layout.group("body")),
                f"{name}.{field} DXIL offset must be {offset}")


def validate_atmosphere_source(source_dir):
    exact_fields = ("    float4 atmosphere_origin_time;\n"
                    "    float4 atmosphere_depth;\n"
                    "    float4 atmosphere_density;\n"
                    "    float4 atmosphere_far_color_noise;\n"
                    "    float4 atmosphere_morning_color;\n"
                    "    float4 atmosphere_coastal_color;\n"
                    "    float4 atmosphere_wind_shore;\n"
                    "    uint4 atmosphere_counts_version;\n"
                    "    float4 atmosphere_water_aabbs[32];")
    ground = (source_dir / "ground_lighting.frag.hlsl").read_text(encoding="utf-8")
    material = (source_dir / "material_sprite.frag.hlsl").read_text(encoding="utf-8")
    require(exact_fields in ground and exact_fields in material,
            "Atmosphere SurfaceLighting fields must retain exact order and types in both shared shaders")
    require("apply_atmosphere(radiance, world.xyz, flags.a)" in ground,
            "Ground atmosphere must haze source-space world position using ground reveal")
    require("apply_atmosphere(radiance, position, RIBTERRA_ATMOSPHERE_REVEAL)" in material,
            "Material atmosphere must haze source-space position using the selected reveal")
    forward = (source_dir / "material_forward.frag.hlsl").read_text(encoding="utf-8")
    reveal_gate = "#ifdef RIBTERRA_ATMOSPHERE\n    if (reveal.x == 0.0)\n        discard;\n#endif"
    require(reveal_gate in forward and forward.index(reveal_gate) < forward.index("output.scene = legacy_material_main(input);"),
            "Atmosphere forward must discard hidden sources before lighting without changing old variants")
    helper = (source_dir / "atmosphere.hlsli").read_text(encoding="utf-8")
    for expression in (
            "if (atmosphere_counts_version.z == 0u)",
            "camera_position.xy + atmosphere_origin_time.xy",
            "const uint water_count = min(atmosphere_counts_version.x, 32u);",
            "const float2 outside = max(max(bounds.xy - absolute_xy,",
            "1.0 - smoothstep(0.0, atmosphere_wind_shore.w, distance_to_water)",
            "const float amount = opacity * saturate(reveal);",
            "return lerp(radiance, atmosphere_color, amount);"):
        require(expression in helper, f"Bounded atmosphere source contract changed: {expression}")


def validate_moon_source(source_dir, program):
    wrapper = (source_dir / f"{program}.frag.hlsl").read_text(encoding="utf-8")
    register = "b1" if program == "ground_sky_lighting" else "b3"
    require(f"#define RIBTERRA_MOON_LIGHTING_REGISTER {register}" in wrapper,
            f"{program} must bind MoonLighting at {register}, space3")
    require("#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity;" in wrapper,
            f"{program} must declare the exact MoonLightingGpu32 field order")
    shared_name = "ground_lighting.frag.hlsl" if program == "ground_sky_lighting" else "material_sprite.frag.hlsl"
    shared = (source_dir / shared_name).read_text(encoding="utf-8")
    start = shared.find("    const float3 moon_to_source")
    require(start >= 0, f"{shared_name} is missing independent moon direct lighting")
    end = shared.find("#endif", start)
    require(end >= 0, f"{shared_name} moon lighting hook is unterminated")
    block = shared[start:end]
    require("const float moon_diffuse = max(dot(normal, moon_to_source), 0.0);" in block and
            "moon_color_intensity.rgb * moon_color_intensity.a" in block and
            "specular_term(normal, moon_to_source, view_direction," in block,
            f"{shared_name} moon diffuse/specular equations changed")
    forbidden = (("material.a", "albedo_ao.a", "receiver_mask", "local_light_visibility", "emission", "reveal",
                  "sun_direction", "sun_color_intensity")
                 if program == "ground_sky_lighting" else
                 ("authored_ao", "surface_mask", "RIBTERRA_LOCAL_LIGHT_ATTENUATION", "emission", "reveal",
                  "sun_direction", "sun_color_intensity"))
    require(not any(value in block for value in forbidden),
            f"{shared_name} moon direct light depends on ambient/sun/local/emission/reveal controls")


def validate_sky_shadow_source(source_dir, program):
    helper = (source_dir / "sky_shadow.hlsli").read_text(encoding="utf-8")
    exact_struct = ("struct ShadowSourceGpu\n{\n    float4 u_min;\n    float4 v_min;\n"
                    "    float4 f_min;\n    float4 inverse_span_enabled;\n"
                    "    float4 bias_texel;\n};")
    require(exact_struct in helper, "ShadowSourceGpu must retain five float4 fields in exact ABI order")
    require("ShadowSourceGpu sun_shadow;\n    ShadowSourceGpu moon_shadow;" in helper,
            "SkyShadows must contain exact sun then moon 80-byte records")
    for expression in (
            "source.inverse_span_enabled.w == 0.0",
            "any(abs(position) > float3(32.0, 32.0, 32.0))",
            "(dot(position, source.u_min.xyz) - source.u_min.w) * source.inverse_span_enabled.x",
            "(dot(position, source.v_min.xyz) - source.v_min.w) * source.inverse_span_enabled.y",
            "(dot(position, source.f_min.xyz) - source.f_min.w) * source.inverse_span_enabled.z",
            "receiver_depth <= 0.0 || receiver_depth >= 1.0",
            "uv + float2(x, y) * source.bias_texel.y",
            "shadow_texture.SampleLevel(shadow_sampler, tap_uv, 0.0)",
            "receiver_depth - source.bias_texel.x <= stored_depth",
            "return visibility / 9.0;"):
        require(expression in helper, f"Sky shadow bounded 3x3 PCF contract changed: {expression}")
    require(helper.count("any(tap_uv < 0.0) || any(tap_uv > 1.0)") == 1 and
            "visibility += 1.0;" in helper and
            "for (int y = -1; y <= 1; ++y)" in helper and
            "for (int x = -1; x <= 1; ++x)" in helper,
            "Each out-of-range 3x3 PCF tap must be independently visible")
    require("owner" not in helper.lower(), "Directional shadow maps must not exclude same-owner casters")

    wrapper = (source_dir / f"{program}.frag.hlsl").read_text(encoding="utf-8")
    if program == "ground_shadow_lighting":
        declarations = ("#define RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b2",
                        "#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t4",
                        "#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s4",
                        "#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t5",
                        "#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s5",
                        "#define RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t6")
    elif program == "water_ground_lighting":
        declarations = ("#define RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b2",
                        "#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t7",
                        "#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s7",
                        "#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t8",
                        "#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s8",
                        "#define RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t9")
    else:
        declarations = ("#define RIBTERRA_SKY_SHADOW_EMBED_MOON",
                        "#define RIBTERRA_MOON_LIGHTING_REGISTER b3",
                        "#define RIBTERRA_MOON_BUFFER_NAME SkyMoonLighting",
                        "#define RIBTERRA_MOON_LIGHTING_FIELDS float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow;",
                        "#define RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t5",
                        "#define RIBTERRA_SUN_SHADOW_SAMPLER_REGISTER s5",
                        "#define RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t6",
                        "#define RIBTERRA_MOON_SHADOW_SAMPLER_REGISTER s6",
                        "#define RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t7")
    require(all(value in wrapper for value in declarations),
            f"{program} shadow registers violate the fixed SDL fragment layout")
    require("sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)" in wrapper and
            "sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)" in wrapper,
            f"{program} must map each direct source only to its own shadow record and map")

    shared_name = ("ground_lighting.frag.hlsl"
                   if program in ("ground_shadow_lighting", "water_ground_lighting")
                   else "material_sprite.frag.hlsl")
    shared = (source_dir / shared_name).read_text(encoding="utf-8")
    require("sun_diffuse * sun_direct_visibility" in shared and
            "moon_diffuse * moon_direct_visibility" in shared and
            shared.count("sun_direct_visibility") == 3 and
            shared.count("moon_direct_visibility") == 3,
            f"{shared_name} must multiply only each source's own direct diffuse and specular terms")


def validate_shadow_depth_source(source_dir):
    source = (source_dir / "sky_shadow_depth.vert.hlsl").read_text(encoding="utf-8")
    for expression in (
            "float3 position : TEXCOORD0;",
            "(dot(input.position, u_min.xyz) - u_min.w) * inverse_span_enabled.x",
            "(dot(input.position, v_min.xyz) - v_min.w) * inverse_span_enabled.y",
            "(dot(input.position, f_min.xyz) - f_min.w) * inverse_span_enabled.z",
            "return float4(2.0 * u - 1.0, 1.0 - 2.0 * v, depth, 1.0);"):
        require(expression in source, f"Shadow depth projection contract changed: {expression}")
    fragment = (source_dir / "sky_shadow_depth.frag.hlsl").read_text(encoding="utf-8")
    require(re.fullmatch(r"\s*void\s+main\s*\(\s*\)\s*\{\s*\}\s*", fragment),
            "Shadow depth fragment shader must remain a minimal no-output SDL-required stage")


def validate_occluder_buffer(dump):
    definitions = buffer_definitions(dump)
    layout = re.search(r"struct\s+(?:struct\.)?OccluderGpu\s*\{(?P<body>.*?)\}[^\n]*"
                       r"(?:\n[^\n]*){0,3}?\bSize:\s*(?P<size>\d+)", definitions, re.S)
    if layout is None:
        # Pinned shadercross lowers SPIR-V storage blocks into a raw byte SRV.
        # Do not pretend its DXIL still reflects the named HLSL struct: prove
        # the actual consuming byte addresses/stride on that exact resource.
        resource = re.search(r"(?m)^;\s*local_occluders\s+texture\s+byte\s+r/o\s+T(\d+)\s+t\d+,space2\s+1\s*$", dump)
        require(resource is not None, "OccluderGpu DXIL resource must be a reflected raw byte SRV")
        handles = re.findall(rf"(?m)^\s*(%\d+) = call %dx.types.Handle @dx.op.createHandle\(i32 57, i8 0, i32 {resource.group(1)}, i32 \d+, i1 false\)", dump)
        require(handles, "OccluderGpu DXIL raw resource handle is missing")
        definitions_by_id = dict(re.findall(r"(?m)^\s*(%\d+) = ([^\r\n]+)", dump))

        def address(value):
            expression = definitions_by_id.get(value, "")
            base = re.fullmatch(r"shl i32 (%\d+), 6", expression)
            if base:
                return base.group(1), 0
            offset = re.fullmatch(r"(?:or|add) i32 (%\d+), (\d+)", expression)
            require(offset is not None, "OccluderGpu DXIL stride must be 64 bytes (index << 6)")
            root, previous = address(offset.group(1))
            return root, previous + int(offset.group(2))

        groups = {}
        for handle in handles:
            loads = re.findall(rf"@dx.op.bufferLoad.i32\(i32 68, %dx.types.Handle {re.escape(handle)}, i32 (%\d+), i32 undef\)", dump)
            require(loads, "OccluderGpu DXIL raw resource is not consumed")
            for value in loads:
                root, offset = address(value)
                groups.setdefault(root, set()).add(offset)
            dimensions = re.findall(rf"(%\d+) = call %dx.types.Dimensions @dx.op.getDimensions\(i32 72, %dx.types.Handle {re.escape(handle)}, i32 undef\)", dump)
            require(dimensions, "OccluderGpu DXIL byte size query is missing")
            for dimension in dimensions:
                values = [key for key, expression in definitions_by_id.items()
                          if expression == f"extractvalue %dx.types.Dimensions {dimension}, 0"]
                require(any(f"lshr i32 {value}, 6" in definitions_by_id.values() for value in values),
                        "OccluderGpu DXIL record count must divide byte size by 64")
        require(groups and all(offsets == {0, 16, 32, 48} for offsets in groups.values()),
                "OccluderGpu DXIL member offsets must be 0/16/32/48")
        return
    require(int(layout.group("size")) == 64, "OccluderGpu DXIL stride must be 64 bytes")
    for field, offset in (("center_base", 0), ("axis_x_extent", 16),
                          ("axis_y_radius", 32), ("shape_flags_id", 48)):
        require(re.search(rf"\b{field};\s*;\s*Offset:\s*{offset}\b", layout.group("body")),
                f"OccluderGpu.{field} DXIL offset must be {offset}")


def validate_local_occlusion_source(source_dir):
    source = (source_dir / "local_occlusion.hlsli").read_text(encoding="utf-8")
    require(source.count("StructuredBuffer<OccluderGpu> local_occluders") == 1,
            "Local occlusion must declare exactly one OccluderGpu StructuredBuffer")
    require("receiver_owner != 0u && occluder.shape_flags_id.w == receiver_owner" in source,
            "Local occlusion owner exclusion must use the nonzero receiver owner and owner field w")
    capsule = source.split("bool local_occlusion_capsule", 1)
    require(len(capsule) == 2 and
            "const float2 axis_y = float2(-axis_x.y, axis_x.x);" in capsule[1].split("float local_light_visibility", 1)[0],
            "Capsule occlusion must derive axis Y perpendicular to axis X")
    require("min(occluder_count, local_occluders_per_light)" in source,
            "Local occlusion must bound each light to the declared per-light cap")
    require(source.count("return last > first;") == 3 and
            "height_last > height_first" in source and
            "local_occlusion_endpoint_epsilon = 1.0e-7" in source,
            "Local occlusion intervals must separate ray parameter endpoints from physical geometry epsilon")
    require("const float closest_t = -dot(origin, direction) / a;" in source and
            "const float2 closest = origin + direction * closest_t;" in source and
            "interior_radius * interior_radius - dot(closest, closest)" in source and
            "sqrt(interior_squared / a)" in source and "b * b - a * c" not in source,
            "Local capsule circle roots must use the stable closest-point formulation")


def validate(source_dir, output_dir, dxc):
    validate_local_occlusion_source(source_dir)
    validate_shadow_depth_source(source_dir)
    records = []
    program_records = {}
    for program, contract in PROGRAMS.items():
        metadata = {}
        stage_records = []
        vertex_uniforms = contract.get("vertex_uniforms", 1)
        require(vertex_uniforms <= 4 and contract["fragment_uniforms"] <= 4,
                f"{program} exceeds pinned SDL_GPU's four-uniform-buffer per-stage limit")
        for stage, name, samplers, uniforms in (("vert", "vertex", 0, vertex_uniforms),
                                                ("frag", "fragment", contract.get("fragment_samplers", 1), contract["fragment_uniforms"])):
            base = f"{program}.{stage}"
            source = source_dir / f"{base}.hlsl"
            binary = output_dir / f"{base}.dxil"
            reflection = output_dir / f"{base}.json"
            data = binary.read_bytes()
            require(len(data) > 128 and data[:4] == b"DXBC" and b"DXIL" in data,
                    f"{binary.name} is not a DXIL container")
            info = json.loads(reflection.read_text(encoding="utf-8"))
            dump = subprocess.run([str(dxc), "-dumpbin", str(binary)], check=True,
                                  capture_output=True, text=True, timeout=20).stdout
            resources = dump.split("; Resource Bindings:", 1)[1].split("; ViewId state:", 1)[0]
            bindings = set(re.findall(r"\b(cb\d+|[tsu]\d+),space(\d+)\s+(\d+)\s*$", resources, re.M))
            expected_bindings = (({("cb0", "1", "1")} if vertex_uniforms else set()) if stage == "vert"
                                 else contract["fragment_bindings"])
            require(bindings == expected_bindings,
                    f"{program} {name} DXIL register bindings violate SDL_GPU: {bindings}")
            require("EntryFunctionName: main" in dump, f"{program} {name} entrypoint must be main")
            expected_outputs = contract.get("fragment_outputs", {0: "float4"})
            exact_targets = set(int(value) for value in re.findall(
                r"; SV_Target\s+\d+\s+xyzw\s+(\d+)\s+TARGET", dump))
            require("OutputPositionPresent=1" in dump if stage == "vert" else
                    exact_targets == set(expected_outputs),
                    f"{program} {name} system output semantic does not match SDL_GPU")
            storage_buffers = contract.get("fragment_storage_buffers", 0) if stage == "frag" else 0
            for key, expected in (("samplers", samplers), ("uniform_buffers", uniforms),
                                  ("storage_textures", 0), ("storage_buffers", storage_buffers)):
                require(info[key] == expected,
                        f"{program} {name} {key}: expected {expected}, got {info[key]}")
            if program in ("surface_lit", "surface_hdr", "material_sprite") and stage == "frag":
                validate_surface_lighting_abi(dump)
                if program == "material_sprite":
                    definitions = re.sub(r"(?m)^\s*; ?", "", dump.split("; Buffer Definitions:", 1)[1]
                                         .split("; Resource Bindings:", 1)[0])
                    layout = re.search(r"cbuffer\s+(?:type_)?SpriteSurface\b(?P<body>.*?)\}\s*(?:type_)?SpriteSurface;"
                                       r"\s*;\s*Offset:\s*0\s+Size:\s*(\d+)", definitions, re.S)
                    require(layout is not None and int(layout.group(2)) == 160,
                            "SpriteSurface DXIL size must be 160 bytes")
                    for field, offset in (("uv_regions", 0), ("anchor_export", 80),
                                          ("source_size_pivot", 96), ("depth_scale", 112),
                                          ("emission", 128), ("response", 144)):
                        require(re.search(rf"\b(?:SpriteSurface_)?{field}(?:\[5\])?;+\s*;\s*Offset:\s*{offset}\b", layout.group("body")),
                                f"SpriteSurface.{field} DXIL offset must be {offset}")
            elif program == "ground_gbuffer" and stage == "frag":
                validate_constant_buffer(dump, "GroundMaterial", 48,
                                         {"response": 0, "flags": 16, "visibility": 32})
                require("#define RIBTERRA_GROUND_WORLD_TAG(material_flags) 1.0" in
                        source.read_text(encoding="utf-8"),
                        "legacy ground_gbuffer must preserve SurfaceWorld.W=1")
            elif program == "ground_contact_gbuffer" and stage == "frag":
                validate_constant_buffer(dump, "GroundMaterial", 48,
                                         {"response": 0, "flags": 16, "visibility": 32})
                require("material_flags.y == 0u ? 1.0 : (material_flags.y <= 3u ? 2.0 : 0.0)" in
                        source.read_text(encoding="utf-8"),
                        "ground_contact_gbuffer must map typed Ordinary to W1 and Flat/Slope/Bridge to W2")
            elif program == "water_ground_gbuffer" and stage == "frag":
                validate_constant_buffer(dump, "GroundMaterial", 48,
                                         {"response": 0, "flags": 16, "visibility": 32})
                validate_constant_buffer(dump, "WaterGround", 16, {"water_receiver": 0})
                source_text = source.read_text(encoding="utf-8")
                require("#define RIBTERRA_WATER_GBUFFER 1" in source_text,
                        "water_ground_gbuffer must enable the versioned water MRT contract")
                require("material_flags.y == 0u ? 1.0 : (material_flags.y <= 3u ? 2.0 : 0.0)" in
                        source_text,
                        "water_ground_gbuffer must preserve exact Contact V1 W tags")
            elif program in ("water_ground_lighting", "atmosphere_ground_lighting") and stage == "frag":
                if program == "atmosphere_ground_lighting":
                    validate_atmosphere_surface_lighting_abi(dump)
                else:
                    validate_surface_lighting_abi(dump)
                validate_occluder_buffer(dump)
                validate_constant_buffer(dump, "MoonLighting", 32,
                                         {"moon_direction": 0, "moon_color_intensity": 16})
                validate_constant_buffer(dump, "SkyShadows", 160,
                                         {"sun_shadow": 0, "moon_shadow": 80})
                validate_constant_buffer(dump, "WaterSurface", 64,
                                         {"water_world_origin_time": 0, "water_flow_response": 16,
                                          "water_tint": 32, "water_viewport": 48})
                validate_moon_source(source_dir, "ground_sky_lighting")
                validate_sky_shadow_source(source_dir, "water_ground_lighting")
                source_text = (source_dir / "water_ground_lighting.frag.hlsl").read_text(encoding="utf-8")
                require("#define RIBTERRA_WATER_GROUND_LIGHTING 1" in source_text,
                        "water_ground_lighting must enable the versioned water composite")
                require("RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t9" in source_text,
                        "water_ground_lighting occluder storage buffer must remain t9")
                if program == "atmosphere_ground_lighting":
                    validate_atmosphere_source(source_dir)
                    wrapper = source.read_text(encoding="utf-8")
                    require("#define RIBTERRA_ATMOSPHERE" in wrapper and
                            '#include "water_ground_lighting.frag.hlsl"' in wrapper,
                            "atmosphere_ground_lighting must opt into atmosphere through the water base")
            elif program in ("ground_lighting", "ground_contact_lighting", "ground_sky_lighting",
                             "ground_shadow_lighting") and stage == "frag":
                validate_surface_lighting_abi(dump)
                validate_occluder_buffer(dump)
                if program == "ground_lighting":
                    require("#define RIBTERRA_GROUND_WORLD_VALID(world_tag) (!((world_tag) <= 0.0))" in
                            source.read_text(encoding="utf-8"),
                            "legacy ground_lighting validity behavior changed")
                elif program == "ground_contact_lighting":
                    require("world_tag == 1.0 || world_tag == 2.0" in
                            source.read_text(encoding="utf-8"),
                            "ground_contact_lighting must accept only exact W1/W2 validity tags")
                elif program == "ground_sky_lighting":
                    validate_constant_buffer(dump, "MoonLighting", 32,
                                             {"moon_direction": 0, "moon_color_intensity": 16})
                    validate_moon_source(source_dir, program)
                    require("ground_contact_lighting.frag.hlsl" in source.read_text(encoding="utf-8"),
                            "ground_sky_lighting must retain exact contact W1/W2 validation")
                else:
                    validate_constant_buffer(dump, "MoonLighting", 32,
                                             {"moon_direction": 0, "moon_color_intensity": 16})
                    validate_constant_buffer(dump, "SkyShadows", 160,
                                             {"sun_shadow": 0, "moon_shadow": 80})
                    validate_moon_source(source_dir, "ground_sky_lighting")
                    validate_sky_shadow_source(source_dir, program)
            elif program == "ground_contact" and stage == "frag":
                validate_constant_buffer(dump, "ContactSurface", 80,
                                         {"anchor_opacity": 0, "axis_u_extent": 16,
                                          "axis_v_extent": 32, "uv_region": 48,
                                          "pivot_plane": 64})
                source_text = source.read_text(encoding="utf-8")
                for declaration in (
                        "#define RIBTERRA_CONTACT_TARGET_FORMAT R8G8B8A8_UNORM_SRGB",
                        "#define RIBTERRA_CONTACT_COLOR_WRITE_MASK A",
                        "#define RIBTERRA_CONTACT_BLEND ZERO_ONE_MINUS_SRC_ALPHA_ADD",
                        "#define RIBTERRA_CONTACT_ACTIVE_TARGET_READS 0"):
                    require(declaration in source_text,
                            f"ground_contact missing exact pipeline declaration: {declaration}")
                require("ground_albedo_texture" not in source_text,
                        "ground_contact must not sample its active ground_albedo target")
            elif program in ("water_reflection", "atmosphere_water_reflection") and stage == "frag":
                if program == "atmosphere_water_reflection":
                    validate_atmosphere_surface_lighting_abi(dump)
                else:
                    validate_surface_lighting_abi(dump)
                validate_occluder_buffer(dump)
                validate_constant_buffer(dump, "SpriteSurface", 160,
                                         {"uv_regions": 0, "anchor_export": 80,
                                          "source_size_pivot": 96, "depth_scale": 112,
                                          "emission": 128, "response": 144})
                validate_constant_buffer(dump, "ReflectionMetadata", 32,
                                         {"reflection_response": 0, "reflection_receiver": 16})
                validate_constant_buffer(dump, "SkyMoonLighting", 192,
                                         {"moon_direction": 0, "moon_color_intensity": 16,
                                          "sun_shadow": 32, "moon_shadow": 112})
                validate_moon_source(source_dir, "material_sky_forward")
                validate_sky_shadow_source(source_dir, "water_reflection")
                source_text = (source_dir / "water_reflection.frag.hlsl").read_text(encoding="utf-8")
                require("return float4(max(source.rgb, 0.0) * alpha, alpha);" in source_text,
                        "water_reflection must output premultiplied linear radiance")
                require("RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t7" in source_text,
                        "water_reflection occluder storage buffer must remain t7")
                if program == "atmosphere_water_reflection":
                    wrapper = source.read_text(encoding="utf-8")
                    require("#define RIBTERRA_ATMOSPHERE" in wrapper and
                            "#define RIBTERRA_ATMOSPHERE_REVEAL reflection_response.z" in wrapper and
                            '#include "water_reflection.frag.hlsl"' in wrapper,
                            "atmosphere_water_reflection must use reflection reveal through the water base")
            elif program in ("material_forward", "material_sky_forward",
                             "material_shadow_forward", "atmosphere_material_forward") and stage == "frag":
                if program == "atmosphere_material_forward":
                    validate_atmosphere_surface_lighting_abi(dump)
                else:
                    validate_surface_lighting_abi(dump)
                validate_occluder_buffer(dump)
                validate_constant_buffer(dump, "SpriteSurface", 160,
                                         {"uv_regions": 0, "anchor_export": 80,
                                          "source_size_pivot": 96, "depth_scale": 112,
                                          "emission": 128, "response": 144})
                validate_constant_buffer(dump, "ForwardMetadata", 32,
                                         {"reveal": 0, "receiver": 16})
                if program == "material_sky_forward":
                    validate_constant_buffer(dump, "MoonLighting", 32,
                                             {"moon_direction": 0, "moon_color_intensity": 16})
                    validate_moon_source(source_dir, "material_sky_forward")
                if program in ("material_shadow_forward", "atmosphere_material_forward"):
                    validate_constant_buffer(dump, "SkyMoonLighting", 192,
                                             {"moon_direction": 0, "moon_color_intensity": 16,
                                              "sun_shadow": 32, "moon_shadow": 112})
                    validate_moon_source(source_dir, "material_sky_forward")
                    validate_sky_shadow_source(source_dir, "material_shadow_forward")
                if program == "atmosphere_material_forward":
                    wrapper = source.read_text(encoding="utf-8")
                    require("#define RIBTERRA_ATMOSPHERE" in wrapper and
                            "#define RIBTERRA_ATMOSPHERE_REVEAL reveal.x" in wrapper and
                            '#include "material_shadow_forward.frag.hlsl"' in wrapper,
                            "atmosphere_material_forward must use forward reveal through the shadow base")
            elif program == "sky_shadow_depth" and stage == "vert":
                validate_constant_buffer(dump, "ShadowSource", 80,
                                         {"u_min": 0, "v_min": 16, "f_min": 32,
                                          "inverse_span_enabled": 48, "bias_texel": 64})
            elif program in ("world_linear", "hdr_composite") and stage == "frag":
                buffer = "WorldAlpha" if program == "world_linear" else "Composite"
                field = "alpha_mode" if program == "world_linear" else "exposure"
                definitions = re.sub(r"(?m)^\s*; ?", "", dump.split("; Buffer Definitions:", 1)[1]
                                     .split("; Resource Bindings:", 1)[0])
                layout = re.search(rf"cbuffer\s+(?:type_)?{buffer}\b(?P<body>.*?)\}}\s*(?:type_)?{buffer};"
                                   r"\s*;\s*Offset:\s*0\s+Size:\s*(\d+)", definitions, re.S)
                require(layout is not None and int(layout.group(2)) == 16,
                        f"{buffer} DXIL size must be 16 bytes")
                require(re.search(rf"\b(?:{buffer}_)?{field};+\s*;\s*Offset:\s*0\b", layout.group("body")),
                        f"{buffer}.{field} DXIL offset must be 0")
            metadata[stage] = info
            record = {"stage": name, "entrypoint": "main", "source": source.name,
                      "source_sha256": digest(source), "file": binary.name, "sha256": digest(binary),
                      "reflection": reflection.name, "reflection_sha256": digest(reflection)}
            dependencies = source_dependencies(source_dir, source)
            declared = contract.get("includes", {}).get(stage, [])
            require([item["source"] for item in dependencies] == declared,
                    f"{program} {name} recursive include contract is incomplete")
            if dependencies:
                record["source_dependencies"] = dependencies
            records.append(record)
            stage_records.append(record)
        require(interface(metadata["vert"], "inputs") == contract["vertex_inputs"],
                f"{program} vertex inputs do not match the declared streams")
        require(interface(metadata["vert"], "outputs") == contract["linked_interface"],
                f"{program} vertex outputs do not match the declared varying interface")
        require(interface(metadata["frag"], "inputs") == contract["linked_interface"],
                f"{program} fragment inputs do not match the declared varying interface")
        fragment_outputs = contract.get("fragment_outputs", {0: "float4"})
        require(interface(metadata["frag"], "outputs") == fragment_outputs,
                f"{program} fragment outputs do not match the exact MRT contract")
        color_targets = contract.get("color_targets", [{
            "location": 0, "format": contract.get("target_format", "R8G8B8A8_UNORM"),
            "blend": "OPAQUE"}])
        require([target["location"] for target in color_targets] == list(fragment_outputs),
                f"{program} color target locations do not match fragment outputs")
        program_records[program] = {
            "vertex_streams": [dict(stream, attributes=manifest_locations(stream["attributes"]))
                               for stream in contract["vertex_streams"]],
            "interfaces": {
                "vertex_inputs": manifest_locations(contract["vertex_inputs"]),
                "vertex_fragment_link": manifest_locations(contract["linked_interface"]),
                "fragment_outputs": manifest_locations(fragment_outputs),
            },
            "bindings": {
                "vertex": ([{"kind": "uniform_buffer", "slot": 0, "space": 1}]
                           if vertex_uniforms else []),
                "fragment": [{"kind": kind, "slot": slot, "space": 2}
                             for slot in contract.get("fragment_sampler_slots",
                                                      range(contract.get("fragment_samplers", 1)))
                             for kind in ("texture", "sampler")] +
                            [{"kind": "storage_buffer", "slot": slot, "space": 2}
                             for slot in ({"ground_lighting": [4], "ground_contact_lighting": [4],
                                          "ground_sky_lighting": [4], "ground_shadow_lighting": [6],
                                          "water_ground_lighting": [9], "water_reflection": [7],
                                          "atmosphere_ground_lighting": [9],
                                          "atmosphere_material_forward": [7],
                                          "atmosphere_water_reflection": [7],
                                          "material_forward": [5], "material_sky_forward": [5],
                                          "material_shadow_forward": [7]}.get(program, []))] +
                            [{"kind": "uniform_buffer", "slot": slot, "space": 3}
                             for slot in range(contract["fragment_uniforms"])],
            },
            "color_targets": color_targets,
            "color_space": contract["color_space"],
            "alpha": contract["alpha"],
            "shaders": [record["file"] for record in stage_records],
        }
        if "sampler_contracts" in contract:
            program_records[program]["samplers"] = contract["sampler_contracts"]
        if "active_target_reads" in contract:
            program_records[program]["active_target_reads"] = contract["active_target_reads"]
        if "shadow_maps" in contract:
            program_records[program]["shadow_maps"] = contract["shadow_maps"]
        if program in ("surface_lit", "surface_hdr", "material_sprite", "ground_lighting",
                       "ground_contact_lighting", "ground_sky_lighting", "ground_shadow_lighting",
                       "material_forward", "material_sky_forward", "material_shadow_forward",
                       "water_ground_lighting", "water_reflection",
                       "atmosphere_ground_lighting", "atmosphere_material_forward",
                       "atmosphere_water_reflection"):
            atmosphere_program = program.startswith("atmosphere_")
            program_records[program]["constant_buffers"] = [{
                "stage": "fragment", "slot": 0, "space": 3,
                "size": 1024 if atmosphere_program else 384,
                "fields": {"ambient": 0, "sun_direction": 16,
                            "sun_color_intensity": 32, "counts": 48, "lights": 64},
                "array": {"field": "lights", "count": 4, "stride": 80,
                          "fields": {"anchor_radius": 0, "direction_cone": 16,
                                     "color_intensity": 32, "occluder_range_flags": 48,
                                      "height_falloff_reveal": 64}},
            }]
            if atmosphere_program:
                program_records[program]["constant_buffers"][0]["fields"].update({
                    "atmosphere_origin_time": 384, "atmosphere_depth": 400,
                    "atmosphere_density": 416, "atmosphere_far_color_noise": 432,
                    "atmosphere_morning_color": 448, "atmosphere_coastal_color": 464,
                    "atmosphere_wind_shore": 480, "atmosphere_counts_version": 496,
                    "atmosphere_water_aabbs": 512})
                program_records[program]["constant_buffers"][0]["atmosphere_array"] = {
                    "field": "atmosphere_water_aabbs", "count": 32, "stride": 16}
            if program in ("material_sprite", "material_forward", "material_sky_forward",
                           "material_shadow_forward", "water_reflection",
                           "atmosphere_material_forward", "atmosphere_water_reflection"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 1, "space": 3, "size": 160,
                    "fields": {"uv_regions": 0, "anchor_export": 80, "source_size_pivot": 96,
                               "depth_scale": 112, "emission": 128, "response": 144}})
            if program in ("material_forward", "material_sky_forward", "material_shadow_forward",
                           "atmosphere_material_forward"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 2, "space": 3, "size": 32,
                    "fields": {"reveal": 0, "receiver": 16}})
            if program in ("water_reflection", "atmosphere_water_reflection"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 2, "space": 3, "size": 32,
                    "fields": {"reflection_response": 0, "reflection_receiver": 16}})
            if program in ("ground_sky_lighting", "ground_shadow_lighting",
                           "material_sky_forward"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": (1 if program in ("ground_sky_lighting",
                                                                     "ground_shadow_lighting") else 3),
                    "space": 3, "size": 32,
                    "fields": {"moon_direction": 0, "moon_color_intensity": 16}})
            if program == "ground_shadow_lighting":
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 2,
                    "space": 3, "size": 160,
                    "fields": {"sun_shadow": 0, "moon_shadow": 80},
                    "array": {"count": 2, "stride": 80,
                              "fields": {"u_min": 0, "v_min": 16, "f_min": 32,
                                          "inverse_span_enabled": 48, "bias_texel": 64}}})
            if program in ("material_shadow_forward", "atmosphere_material_forward"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 3, "space": 3, "size": 192,
                    "fields": {"moon_direction": 0, "moon_color_intensity": 16,
                               "sun_shadow": 32, "moon_shadow": 112},
                    "array": {"field_offset": 32, "count": 2, "stride": 80,
                              "fields": {"u_min": 0, "v_min": 16, "f_min": 32,
                                         "inverse_span_enabled": 48, "bias_texel": 64}}})
            if program in ("water_ground_lighting", "atmosphere_ground_lighting"):
                program_records[program]["constant_buffers"].extend([{
                    "stage": "fragment", "slot": 1, "space": 3, "size": 32,
                    "fields": {"moon_direction": 0, "moon_color_intensity": 16}}, {
                    "stage": "fragment", "slot": 2, "space": 3, "size": 160,
                    "fields": {"sun_shadow": 0, "moon_shadow": 80},
                    "array": {"count": 2, "stride": 80,
                              "fields": {"u_min": 0, "v_min": 16, "f_min": 32,
                                         "inverse_span_enabled": 48, "bias_texel": 64}}}, {
                    "stage": "fragment", "slot": 3, "space": 3, "size": 64,
                    "fields": {"water_world_origin_time": 0, "water_flow_response": 16,
                               "water_tint": 32, "water_viewport": 48}}])
            if program in ("water_reflection", "atmosphere_water_reflection"):
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 3, "space": 3, "size": 192,
                    "fields": {"moon_direction": 0, "moon_color_intensity": 16,
                               "sun_shadow": 32, "moon_shadow": 112},
                    "array": {"field_offset": 32, "count": 2, "stride": 80,
                              "fields": {"u_min": 0, "v_min": 16, "f_min": 32,
                                         "inverse_span_enabled": 48, "bias_texel": 64}}})
            if program in ("ground_lighting", "ground_contact_lighting", "ground_sky_lighting",
                            "ground_shadow_lighting", "material_forward", "material_sky_forward",
                            "material_shadow_forward", "water_ground_lighting", "water_reflection",
                            "atmosphere_ground_lighting", "atmosphere_material_forward",
                            "atmosphere_water_reflection"):
                program_records[program]["storage_buffers"] = [{
                    "stage": "fragment", "slot": (9 if program in ("water_ground_lighting",
                                                                     "atmosphere_ground_lighting") else
                                                    7 if program in ("material_shadow_forward",
                                                                     "water_reflection",
                                                                     "atmosphere_material_forward",
                                                                     "atmosphere_water_reflection") else
                                                    5 if program in ("material_forward",
                                                                     "material_sky_forward") else
                                                    6 if program == "ground_shadow_lighting" else 4),
                    "space": 2, "stride": 64, "capacity": 256,
                    "fields": {"center_base": 0, "axis_x_extent": 16,
                               "axis_y_radius": 32, "shape_flags_id": 48},
                    "abi_verification": "DXIL named layout or exact raw-SRV record addressing and byte-count division"}]
        elif program in ("ground_gbuffer", "ground_contact_gbuffer", "water_ground_gbuffer"):
            program_records[program]["constant_buffers"] = [{
                "stage": "fragment", "slot": 0, "space": 3, "size": 48,
                "fields": {"response": 0, "flags": 16, "visibility": 32}}]
            if program == "water_ground_gbuffer":
                program_records[program]["constant_buffers"].append({
                    "stage": "fragment", "slot": 1, "space": 3, "size": 16,
                    "fields": {"water_receiver": 0}})
        elif program == "ground_contact":
            program_records[program]["constant_buffers"] = [{
                "stage": "fragment", "slot": 0, "space": 3, "size": 80,
                "fields": {"anchor_opacity": 0, "axis_u_extent": 16,
                           "axis_v_extent": 32, "uv_region": 48, "pivot_plane": 64}}]
        elif program == "sky_shadow_depth":
            program_records[program]["constant_buffers"] = [{
                "stage": "vertex", "slot": 0, "space": 1, "size": 80,
                "fields": {"u_min": 0, "v_min": 16, "f_min": 32,
                           "inverse_span_enabled": 48, "bias_texel": 64}}]
        elif program in ("world_linear", "hdr_composite"):
            program_records[program]["constant_buffers"] = [{
                "stage": "fragment", "slot": 0, "space": 3, "size": 16,
                "fields": {"alpha_mode" if program == "world_linear" else "exposure": 0},
            }]
    source_closure = [{"source": path.relative_to(source_dir).as_posix(),
                       "bytes": path.stat().st_size, "sha256": digest(path)}
                      for path in sorted(source_dir.rglob("*")) if path.is_file()]
    # Preserve the schema, vertex_stride and flat shader list consumed downstream,
    # while binding every current source/include even when it is not an entrypoint.
    return {"schema": 1, "format": "DXIL", "vertex_stride": 20,
            "source_closure": source_closure, "shaders": records, "programs": program_records}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--dxc", type=Path, required=True)
    parser.add_argument("--shadercross", type=Path, required=True)
    args = parser.parse_args()
    manifest = validate(args.source_dir, args.output_dir, args.dxc)
    manifest["toolchain"] = {
        "vcpkg_baseline": json.loads((args.source_dir.parent / "vcpkg.json").read_text(encoding="utf-8"))["builtin-baseline"],
        "shadercross_sha256": digest(args.shadercross), "dxc_sha256": digest(args.dxc),
        "shadercross_library_sha256": digest(args.shadercross.parent / "SDL3_shadercross.dll"),
        "dxcompiler_sha256": digest(args.shadercross.parent / "dxcompiler.dll"),
    }
    path = args.output_dir / "manifest.json"
    if args.write_manifest:
        path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    else:
        require(json.loads(path.read_text(encoding="utf-8")) == manifest, "Shader manifest does not match current sources and compiled assets")
    print("DXIL program interfaces, resource bindings, lighting ABI and shader asset hashes passed.")


if __name__ == "__main__":
    main()
