"""Wrong DXIL register spaces/slots must fail even when resource counts match."""
from pathlib import Path
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from shader_contract import PROGRAMS, validate as validate_shader_contract

source, compiled, dxc, compiler = map(Path, sys.argv[1:])
proof_root_value = os.environ.get("RIBTERRA_PROOF_ROOT")
proof_root = Path(proof_root_value) if proof_root_value else None
minimum_free_bytes = 384 * 1024 * 1024
maximum_proof_bytes = 128 * 1024 * 1024


def check_proof_budget(phase):
    if proof_root is None:
        return
    if not proof_root.is_dir():
        raise RuntimeError(f"RIBTERRA_PROOF_ROOT must be an existing directory: {proof_root}")
    try:
        free_bytes = shutil.disk_usage(proof_root).free
        proof_bytes = 0
        for directory, _, names in os.walk(proof_root, followlinks=False):
            for name in names:
                path = Path(directory) / name
                metadata = path.lstat()
                if stat.S_ISREG(metadata.st_mode):
                    proof_bytes += metadata.st_size
                    if proof_bytes > maximum_proof_bytes:
                        raise RuntimeError(
                            f"Isolated proof root exceeds 128 MiB during {phase}: "
                            f"{proof_bytes} bytes in {proof_root}")
    except OSError as error:
        raise RuntimeError(f"Unable to inspect isolated proof budget during {phase}: {error}") from error
    if free_bytes < minimum_free_bytes:
        raise RuntimeError(
            f"Isolated proof volume has less than 384 MiB free during {phase}: "
            f"{free_bytes} bytes at {proof_root}")


def checked_copytree(source_path, destination_path):
    check_proof_budget(f"before copytree to {destination_path}")
    try:
        return shutil.copytree(source_path, destination_path)
    finally:
        check_proof_budget(f"after copytree to {destination_path}")


def checked_compile(arguments, **kwargs):
    check_proof_budget("before shader compilation")
    try:
        return subprocess.run(arguments, **kwargs)
    finally:
        check_proof_budget("after shader compilation")


def checked_validate(*args, **kwargs):
    check_proof_budget("before shader contract validation")
    try:
        return validate_shader_contract(*args, **kwargs)
    finally:
        check_proof_budget("after shader contract validation")


check_proof_budget("startup")
mutations = (("sprite", "vert", "vertex", "b0, space1", "b1, space1"),
             ("sprite", "vert", "vertex", "b0, space1", "b0, space0"),
             ("sprite", "frag", "fragment", "t0, space2", "t1, space2"),
             ("sprite", "frag", "fragment", "s0, space2", "s0, space0"),
             ("sprite", "frag", "fragment", "SV_Target0", "SV_Target1"),
             ("surface_lit", "frag", "fragment", "b0, space3", "b1, space3"),
             ("surface_lit", "frag", "fragment",
              "float4 anchor_radius;\n    float4 direction_cone;",
              "float4 direction_cone;\n    float4 anchor_radius;"),
             ("surface_lit", "frag", "fragment", "LightGpu lights[4]", "LightGpu lights[3]"),
             ("world_linear", "frag", "fragment", "b0, space3", "b1, space3"),
             ("hdr_composite", "frag", "fragment", "b0, space3", "b1, space3"),
             ("material_sprite", "frag", "fragment", "b1, space3", "b2, space3"),
             ("material_sprite", "frag", "fragment", "t4, space2", "t5, space2"),
             ("ground_gbuffer", "frag", "fragment", "SV_Target3", "SV_Target4"),
             ("ground_gbuffer", "frag", "fragment", "b0, space3", "b1, space3"),
             ("ground_gbuffer", "frag", "fragment",
              "float4 response;\n    uint4 flags;", "uint4 flags;\n    float4 response;"),
             ("ground_gbuffer", "frag", "fragment",
              "RIBTERRA_GROUND_WORLD_TAG(material_flags) 1.0",
              "RIBTERRA_GROUND_WORLD_TAG(material_flags) 2.0"),
             ("ground_lighting", "frag", "fragment", "t3, space2", "t5, space2"),
             ("ground_lighting", "frag", "fragment",
              "RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t4",
              "RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t5"),
             ("ground_lighting", "frag", "fragment", "SV_Target1", "SV_Target2"),
             ("ground_lighting", "frag", "fragment",
              "RIBTERRA_GROUND_WORLD_VALID(world_tag) (!((world_tag) <= 0.0))",
              "RIBTERRA_GROUND_WORLD_VALID(world_tag) ((world_tag) > 0.0)"),
             ("material_forward", "frag", "fragment", "b2, space3", "b3, space3"),
             ("material_forward", "frag", "fragment",
              "RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t5",
              "RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t6"),
             ("material_forward", "frag", "fragment", "float4 reveal;", "float4 padding;\n    float4 reveal;"),
             ("material_forward", "frag", "fragment", "SV_Target1", "SV_Target2"),
             ("ground_contact", "frag", "fragment", "t2, space2", "t3, space2"),
             ("ground_contact", "frag", "fragment",
              "float4 anchor_opacity;\n    float4 axis_u_extent;",
              "float4 axis_u_extent;\n    float4 anchor_opacity;"),
             ("ground_contact", "frag", "fragment", "SV_Target0", "SV_Target1"),
             ("ground_contact", "frag", "fragment",
              "RIBTERRA_CONTACT_TARGET_FORMAT R8G8B8A8_UNORM_SRGB",
              "RIBTERRA_CONTACT_TARGET_FORMAT R8G8B8A8_UNORM"),
             ("ground_contact", "frag", "fragment",
              "RIBTERRA_CONTACT_COLOR_WRITE_MASK A", "RIBTERRA_CONTACT_COLOR_WRITE_MASK RGBA"),
             ("ground_contact", "frag", "fragment",
              "RIBTERRA_CONTACT_BLEND ZERO_ONE_MINUS_SRC_ALPHA_ADD",
              "RIBTERRA_CONTACT_BLEND SRC_ALPHA_ONE_MINUS_SRC_ALPHA_ADD"),
             ("ground_contact", "frag", "fragment",
              "RIBTERRA_CONTACT_ACTIVE_TARGET_READS 0", "RIBTERRA_CONTACT_ACTIVE_TARGET_READS 1"),
             ("ground_contact", "frag", "fragment",
              "Texture2D<float4> surface_world_texture : register(t0, space2);",
              "Texture2D<float4> ground_albedo_texture : register(t0, space2);\n"
              "#define surface_world_texture ground_albedo_texture"),
             ("ground_contact_gbuffer", "frag", "fragment", "material_flags.y == 0u", "material_flags.x == 0u"),
             ("ground_contact_lighting", "frag", "fragment",
              "world_tag == 1.0 || world_tag == 2.0", "world_tag > 0.0"),
             ("ground_sky_lighting", "frag", "fragment",
              "RIBTERRA_MOON_LIGHTING_REGISTER b1", "RIBTERRA_MOON_LIGHTING_REGISTER b2"),
             ("ground_sky_lighting", "frag", "fragment",
              "float4 moon_direction; float4 moon_color_intensity;",
              "float4 moon_color_intensity; float4 moon_direction;"),
             ("ground_sky_lighting", "frag", "fragment",
              "float4 moon_direction; float4 moon_color_intensity;",
              "float4 moon_direction; float4 moon_color_intensity; float4 moon_padding;"),
             ("material_sky_forward", "frag", "fragment",
              "RIBTERRA_MOON_LIGHTING_REGISTER b3", "RIBTERRA_MOON_LIGHTING_REGISTER b4"),
             ("ground_shadow_lighting", "frag", "fragment",
              "RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t4",
              "RIBTERRA_SUN_SHADOW_TEXTURE_REGISTER t7"),
             ("ground_shadow_lighting", "frag", "fragment",
              "RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b2",
              "RIBTERRA_SKY_SHADOW_BUFFER_REGISTER b3"),
             ("ground_shadow_lighting", "frag", "fragment",
              "RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t6",
              "RIBTERRA_GROUND_LOCAL_OCCLUDER_REGISTER t7"),
             ("material_shadow_forward", "frag", "fragment",
              "RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t6",
              "RIBTERRA_MOON_SHADOW_TEXTURE_REGISTER t8"),
             ("material_shadow_forward", "frag", "fragment",
              "RIBTERRA_MOON_LIGHTING_REGISTER b3",
              "RIBTERRA_MOON_LIGHTING_REGISTER b4"),
             ("material_shadow_forward", "frag", "fragment",
              "float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow;",
              "float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu moon_shadow; ShadowSourceGpu sun_shadow;"),
             ("material_shadow_forward", "frag", "fragment",
              "float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow;",
              "float4 moon_direction; float4 moon_color_intensity; ShadowSourceGpu sun_shadow; ShadowSourceGpu moon_shadow; float4 forbidden_padding;"),
             ("material_shadow_forward", "frag", "fragment",
              "RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t7",
              "RIBTERRA_FORWARD_LOCAL_OCCLUDER_REGISTER t8"),
             ("sky_shadow_depth", "vert", "vertex",
              "float4 u_min;\n    float4 v_min;",
              "float4 v_min;\n    float4 u_min;"),
             ("sky_shadow_depth", "vert", "vertex",
              "float4 bias_texel;\n};",
              "float4 bias_texel;\n    float4 forbidden_padding;\n};"))
# Preserve even failing generated shader fixtures; no TemporaryDirectory cleanup.
retained = compiled.parent / "shader-negative-retained"
if proof_root is not None:
    try:
        retained.resolve().relative_to(proof_root.resolve())
    except ValueError as error:
        raise RuntimeError(
            f"Retained shader fixtures must stay inside RIBTERRA_PROOF_ROOT: {retained}") from error
retained.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="run-", dir=retained))
check_proof_budget("after retained fixture root creation")
checked_validate(source, compiled, dxc)  # A broken baseline must never make all negatives pass.
for index, (program, stage, stage_name, original, replacement) in enumerate(mutations):
    case = root / f"{index:02d}-{program}-{stage}"
    local_source, output = case / "source", case / "compiled"
    checked_copytree(source, local_source)
    checked_copytree(compiled, output)
    file = local_source / f"{program}.{stage}.hlsl"
    original_text = file.read_text(encoding="utf-8")
    if original_text.count(original) != 1:
        raise AssertionError(f"Mutation must target exactly one declaration: {original}")
    file.write_text(original_text.replace(original, replacement), encoding="utf-8")
    for kind, suffix in (("DXIL", "dxil"), ("JSON", "json")):
        checked_compile([str(compiler), str(file), "-s", "HLSL", "-d", kind, "-t", stage_name,
                         "-I", str(local_source), "-o", str(output / f"{program}.{stage}.{suffix}")],
                        check=True, capture_output=True, text=True, timeout=20)
    try:
        checked_validate(local_source, output, dxc)
    except ValueError:
        pass
    else:
        raise AssertionError(f"Invalid shader binding/semantic passed: {original} -> {replacement}")

# Include hashes are part of the program asset contract even when a stale DXIL
# file remains structurally valid. Mutating an included source must change the
# generated manifest and therefore fail comparison with the retained baseline.
baseline = checked_validate(source, compiled, dxc)
include_cases = (("ground_gbuffer.vert.hlsl", "surface_lit.vert.hlsl"),
                 ("ground_lighting.vert.hlsl", "hdr_composite.vert.hlsl"),
                 ("ground_lighting.frag.hlsl", "local_occlusion.hlsli"),
                 ("material_forward.frag.hlsl", "local_occlusion.hlsli"),
                 ("material_forward.frag.hlsl", "material_sprite.frag.hlsl"),
                 ("ground_contact.vert.hlsl", "hdr_composite.vert.hlsl"),
                 ("ground_contact_gbuffer.frag.hlsl", "ground_gbuffer.frag.hlsl"),
                 ("ground_contact_gbuffer.vert.hlsl", "surface_lit.vert.hlsl"),
                 ("ground_contact_lighting.frag.hlsl", "local_occlusion.hlsli"),
                 ("ground_contact_lighting.vert.hlsl", "hdr_composite.vert.hlsl"),
                 ("ground_sky_lighting.vert.hlsl", "ground_lighting.vert.hlsl"),
                 ("ground_sky_lighting.frag.hlsl", "ground_contact_lighting.frag.hlsl"),
                 ("material_sky_forward.vert.hlsl", "material_forward.vert.hlsl"),
                 ("material_sky_forward.frag.hlsl", "material_forward.frag.hlsl"),
                 ("ground_shadow_lighting.vert.hlsl", "ground_sky_lighting.vert.hlsl"),
                 ("ground_shadow_lighting.frag.hlsl", "sky_shadow.hlsli"),
                 ("ground_shadow_lighting.frag.hlsl", "ground_lighting.frag.hlsl"),
                 ("material_shadow_forward.vert.hlsl", "material_sky_forward.vert.hlsl"),
                 ("material_shadow_forward.frag.hlsl", "sky_shadow.hlsli"),
                 ("material_shadow_forward.frag.hlsl", "material_sprite.frag.hlsl"))
for index, (program_source, dependency_name) in enumerate(include_cases, start=len(mutations)):
    case = root / f"{index:02d}-include-{dependency_name}"
    local_source = case / "source"
    checked_copytree(source, local_source)
    dependency = local_source / dependency_name
    dependency.write_text(dependency.read_text(encoding="utf-8") + "\n// negative include hash mutation\n",
                          encoding="utf-8")
    mutated = checked_validate(local_source, compiled, dxc)
    if mutated == baseline:
        raise AssertionError(f"Included source hash mutation was not represented: {program_source}")
# A real two-level chain: material_forward -> material_sprite -> new leaf.
# Both successive manifests must carry the leaf hash, even with stale binaries.
case = root / f"{len(mutations) + len(include_cases):02d}-two-level-include"
local_source = case / "source"
checked_copytree(source, local_source)
middle = local_source / "material_sprite.frag.hlsl"
middle.write_text(middle.read_text(encoding="utf-8") + '\n#include "negative_leaf.hlsli"\n', encoding="utf-8")
leaf = local_source / "negative_leaf.hlsli"
leaf.write_text("// retained two-level include baseline\n", encoding="utf-8")
original_includes = {name: PROGRAMS[name]["includes"] for name in
                     ("material_sprite", "material_forward", "material_sky_forward",
                      "material_shadow_forward")}
try:
    # Declare the synthetic closure only for this local test. Production still
    # rejects unexpected includes instead of blessing stale compiled sources.
    PROGRAMS["material_sprite"]["includes"] = dict(original_includes["material_sprite"], frag=["negative_leaf.hlsli"])
    PROGRAMS["material_forward"]["includes"] = dict(
        original_includes["material_forward"],
        frag=["local_occlusion.hlsli", "material_sprite.frag.hlsl", "negative_leaf.hlsli"])
    PROGRAMS["material_sky_forward"]["includes"] = dict(
        original_includes["material_sky_forward"],
        frag=["material_forward.frag.hlsl", "local_occlusion.hlsli",
              "material_sprite.frag.hlsl", "negative_leaf.hlsli"])
    PROGRAMS["material_shadow_forward"]["includes"] = dict(
        original_includes["material_shadow_forward"],
        frag=["sky_shadow.hlsli", "material_sky_forward.frag.hlsl",
              "material_forward.frag.hlsl", "local_occlusion.hlsli",
              "material_sprite.frag.hlsl", "negative_leaf.hlsli"])
    nested_before = checked_validate(local_source, compiled, dxc)
    leaf.write_text("// retained two-level include mutation\n", encoding="utf-8")
    nested_after = checked_validate(local_source, compiled, dxc)
finally:
    for name, includes in original_includes.items():
        PROGRAMS[name]["includes"] = includes
forward_before = next(item for item in nested_before["shaders"] if item["file"] == "material_forward.frag.dxil")
forward_after = next(item for item in nested_after["shaders"] if item["file"] == "material_forward.frag.dxil")
if forward_before == forward_after:
    raise AssertionError("A two-level included leaf did not invalidate the forward shader source ledger")
sky_before = next(item for item in nested_before["shaders"] if item["file"] == "material_sky_forward.frag.dxil")
sky_after = next(item for item in nested_after["shaders"] if item["file"] == "material_sky_forward.frag.dxil")
if sky_before == sky_after:
    raise AssertionError("A two-level included leaf did not invalidate the sky-forward shader source ledger")
shadow_before = next(item for item in nested_before["shaders"]
                     if item["file"] == "material_shadow_forward.frag.dxil")
shadow_after = next(item for item in nested_after["shaders"]
                    if item["file"] == "material_shadow_forward.frag.dxil")
if shadow_before == shadow_after:
    raise AssertionError("A two-level included leaf did not invalidate the shadow-forward shader source ledger")

# Accepted HLSL syntax must not silently bypass the bounded quoted-include
# source ledger. Keep these source-only fixtures, including rejected forms.
unsupported_includes = (('angle', '#include <negative_include_leaf.hlsli>\n'),
                        ('macro', '#define NEGATIVE_INCLUDE_LEAF "negative_include_leaf.hlsli"\n'
                         '# include NEGATIVE_INCLUDE_LEAF\n'))
for index, (name, directive) in enumerate(
        unsupported_includes, start=len(mutations) + len(include_cases) + 1):
    local_source = root / f"{index:02d}-noncanonical-{name}" / "source"
    checked_copytree(source, local_source)
    leaf = local_source / "negative_include_leaf.hlsli"
    leaf.write_text("// retained noncanonical include fixture\n", encoding="utf-8")
    wrapper = local_source / "material_forward.frag.hlsl"
    wrapper.write_text(wrapper.read_text(encoding="utf-8") + "\n" + directive, encoding="utf-8")
    try:
        checked_validate(local_source, compiled, dxc)
    except ValueError as error:
        if "Noncanonical shader include" not in str(error):
            raise AssertionError(f"Unexpected rejection reason for {name} include: {error}") from error
    else:
        raise AssertionError(f"Noncanonical {name} include bypassed the source ledger")

# The owner component is a behavioral ABI contract even though changing the
# swizzle leaves the reflected stride untouched. Keep the rejected source so
# this cannot regress into owner-blind or wrong-owner self-occlusion.
case = root / f"{len(mutations) + len(include_cases) + 3:02d}-owner-field"
local_source = case / "source"
checked_copytree(source, local_source)
helper = local_source / "local_occlusion.hlsli"
original_text = helper.read_text(encoding="utf-8")
original = "occluder.shape_flags_id.w == receiver_owner"
if original_text.count(original) != 1:
    raise AssertionError("Owner-field mutation must target exactly one comparison")
helper.write_text(original_text.replace(original, "occluder.shape_flags_id.z == receiver_owner"),
                  encoding="utf-8")
try:
    checked_validate(local_source, compiled, dxc)
except ValueError as error:
    if "owner exclusion" not in str(error):
        raise AssertionError(f"Unexpected owner-field rejection reason: {error}") from error
else:
    raise AssertionError("Wrong OccluderGpu owner component passed")

# Capsule ABI leaves axis_y_radius.xy zero, so consuming it instead of deriving
# the perpendicular from axis X must fail even though the reflected ABI matches.
case = root / f"{len(mutations) + len(include_cases) + 4:02d}-capsule-axis"
local_source = case / "source"
checked_copytree(source, local_source)
helper = local_source / "local_occlusion.hlsli"
original_text = helper.read_text(encoding="utf-8")
original = "const float2 axis_y = float2(-axis_x.y, axis_x.x);"
if original_text.count(original) != 1:
    raise AssertionError("Capsule-axis mutation must target exactly one derivation")
helper.write_text(original_text.replace(
    original, "const float2 axis_y = occluder.axis_y_radius.xy;"), encoding="utf-8")
try:
    checked_validate(local_source, compiled, dxc)
except ValueError as error:
    if "Capsule occlusion" not in str(error):
        raise AssertionError(f"Unexpected capsule-axis rejection reason: {error}") from error
else:
    raise AssertionError("Capsule shader consumed the ABI-zero authored axis Y")

# An inserted member changes the StructuredBuffer element layout while leaving
# all consumed fields valid. Recompile one consumer and require the
# reflected 64-byte/0-16-32-48 contract to reject it.
case = root / f"{len(mutations) + len(include_cases) + 5:02d}-occluder-stride"
local_source, output = case / "source", case / "compiled"
checked_copytree(source, local_source)
checked_copytree(compiled, output)
helper = local_source / "local_occlusion.hlsli"
original_text = helper.read_text(encoding="utf-8")
original = "float4 axis_y_radius;\n    uint4 shape_flags_id;"
if original_text.count(original) != 1:
    raise AssertionError("Occluder stride mutation must target exactly one member")
helper.write_text(original_text.replace(
    original, "float4 axis_y_radius;\n    float4 padding;\n    uint4 shape_flags_id;"), encoding="utf-8")
entry = local_source / "ground_lighting.frag.hlsl"
for kind, suffix in (("DXIL", "dxil"), ("JSON", "json")):
    checked_compile([str(compiler), str(entry), "-s", "HLSL", "-d", kind, "-t", "fragment",
                     "-I", str(local_source), "-o", str(output / f"ground_lighting.frag.{suffix}")],
                    check=True, capture_output=True, text=True, timeout=20)
try:
    checked_validate(local_source, output, dxc)
except ValueError:
    pass
else:
    raise AssertionError("Wrong OccluderGpu StructuredBuffer stride passed")
for name, original, replacement, diagnostic in (
        ("ray-parameter-epsilon", "return last > first;", "return last - first > local_occlusion_epsilon;", "intervals must separate"),
        ("unstable-circle-roots", "const float closest_t = -dot(origin, direction) / a;",
         "const float closest_t = 0.0;", "stable closest-point formulation")):
    case = root / name
    local_source = case / "source"
    checked_copytree(source, local_source)
    helper = local_source / "local_occlusion.hlsli"
    original_text = helper.read_text(encoding="utf-8")
    if original not in original_text:
        raise AssertionError(f"Missing numerical regression mutation target: {name}")
    helper.write_text(original_text.replace(original, replacement), encoding="utf-8")
    try:
        checked_validate(local_source, compiled, dxc)
    except ValueError as error:
        if diagnostic not in str(error):
            raise AssertionError(f"Unexpected numerical regression rejection: {error}") from error
    else:
        raise AssertionError(f"Numerical occlusion regression passed: {name}")
for case_name, program, dependency, original, replacement in (
        ("ground-contact-ao", "ground_sky_lighting", "ground_lighting.frag.hlsl",
         "moon_diffuse * moon_direct_visibility;",
         "moon_diffuse * moon_direct_visibility * saturate(albedo_ao.a);"),
        ("material-authored-ao", "material_sky_forward", "material_sprite.frag.hlsl",
         "moon_diffuse * moon_direct_visibility;",
         "moon_diffuse * moon_direct_visibility * authored_ao;"),
        ("ground-sun-alias", "ground_sky_lighting", "ground_lighting.frag.hlsl",
         "safe_normalize(moon_direction.xyz, float3(0.0, 0.0, 1.0))",
         "safe_normalize(sun_direction.xyz, float3(0.0, 0.0, 1.0))")):
    case = root / f"moon-independent-{case_name}"
    local_source = case / "source"
    checked_copytree(source, local_source)
    shader = local_source / dependency
    original_text = shader.read_text(encoding="utf-8")
    if original_text.count(original) != 1:
        raise AssertionError(f"Moon independence mutation must target exactly one expression: {program}")
    shader.write_text(original_text.replace(original, replacement), encoding="utf-8")
    try:
        checked_validate(local_source, compiled, dxc)
    except ValueError as error:
        if "moon direct light depends" not in str(error):
            raise AssertionError(f"Unexpected moon independence rejection: {error}") from error
    else:
        raise AssertionError(f"Moon direct light accepted forbidden ambient/contact dependence: {program}")

# These mutations remain valid HLSL and are freshly compiled. Reflection alone
# cannot distinguish a sun lookup wired to the moon record or an out-of-range
# PCF tap treated as shadow, so the source contract must reject both.
shadow_semantic_cases = (
    ("ground-own-source", "ground_shadow_lighting", "ground_shadow_lighting.frag.hlsl",
     "sky_shadow_visibility(position, sun_shadow, sun_shadow_texture, sun_shadow_sampler)",
     "sky_shadow_visibility(position, moon_shadow, sun_shadow_texture, sun_shadow_sampler)",
     "own shadow record and map"),
    ("material-own-source", "material_shadow_forward", "material_shadow_forward.frag.hlsl",
     "sky_shadow_visibility(position, moon_shadow, moon_shadow_texture, moon_shadow_sampler)",
     "sky_shadow_visibility(position, sun_shadow, moon_shadow_texture, moon_shadow_sampler)",
     "own shadow record and map"),
    ("outside-tap-visible", "ground_shadow_lighting", "sky_shadow.hlsli",
     "visibility += 1.0;\n                continue;",
     "visibility += 0.0;\n                continue;",
     "out-of-range 3x3 PCF tap"),
    ("shared-layout", "ground_shadow_lighting", "sky_shadow.hlsli",
     "float4 u_min;\n    float4 v_min;",
     "float4 v_min;\n    float4 u_min;",
     "exact ABI order"),
)
for case_name, program, dependency, original, replacement, diagnostic in shadow_semantic_cases:
    case = root / f"sky-shadow-semantic-{case_name}"
    local_source, output = case / "source", case / "compiled"
    checked_copytree(source, local_source)
    checked_copytree(compiled, output)
    shader = local_source / dependency
    original_text = shader.read_text(encoding="utf-8")
    if original_text.count(original) != 1:
        raise AssertionError(f"Sky shadow semantic mutation must target exactly once: {case_name}")
    shader.write_text(original_text.replace(original, replacement), encoding="utf-8")
    entry = local_source / f"{program}.frag.hlsl"
    for kind, suffix in (("DXIL", "dxil"), ("JSON", "json")):
        checked_compile([str(compiler), str(entry), "-s", "HLSL", "-d", kind, "-t", "fragment",
                         "-I", str(local_source), "-o", str(output / f"{program}.frag.{suffix}")],
                        check=True, capture_output=True, text=True, timeout=20)
    try:
        checked_validate(local_source, output, dxc)
    except ValueError as error:
        if diagnostic not in str(error):
            raise AssertionError(f"Unexpected sky shadow semantic rejection: {error}") from error
    else:
        raise AssertionError(f"Sky shadow semantic mutation passed: {case_name}")
print(f"{len(mutations)} wrong shader register/space/output/ABI mutations rejected and "
      f"{len(include_cases)} direct plus 1 two-level include hash mutations detected; "
      f"{len(unsupported_includes)} noncanonical include forms plus owner/capsule-axis/stride, "
      f"2 numerical, 3 moon-independence and {len(shadow_semantic_cases)} compiled shadow-semantic "
      f"mutations rejected. "
      f"Retained fixtures: {root}")
