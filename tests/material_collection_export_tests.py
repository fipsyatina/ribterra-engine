#!/usr/bin/env python3
"""Retained CPU collection export checks; no cleanup or original asset edits."""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
BASELINE_EXPORTER = "0616a51fc385ee0ed3d9aa7028f7ae6d6c23b03438c8786d91fc55c9a8cac8f4"
BASELINE_MASTER = "6b362340248c1b8eef3a6ebe850fb01d1962485d910d9605a6d25b087b923359"
CHANNELS = ("albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask")
FILENAMES = ("albedo.rtex", "normal-material.rtex", "camera-depth.rtex", "emission-mask.rtex", "surface-mask.rtex", "caster-mask.rtex")


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def tree(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes() for path in sorted(root.rglob("*")) if path.is_file()}


def rows(files: dict[str, bytes]) -> list[dict]:
    return [{"relative_path": path, "bytes": len(data), "sha256": digest(data)} for path, data in sorted(files.items())]


def inverse(screen: tuple[float, float], depth: float) -> list[float]:
    # Independent inverse of the two camera rows plus dot(XYZ,(1,1,1)/sqrt3).
    u, v = screen
    total = depth * math.sqrt(3.0)
    z = (total - v / 14.5) / 3.0
    xy = total - z
    return [(xy + u / 29.0) / 2.0, (xy - u / 29.0) / 2.0, z]


def normal(vertices: list[list[float]]) -> tuple[float, float, float]:
    a = [vertices[1][i] - vertices[0][i] for i in range(3)]
    b = [vertices[2][i] - vertices[0][i] for i in range(3)]
    n = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    length = math.sqrt(sum(v*v for v in n))
    return tuple(v/length for v in n)


def project(vertex: list[float]) -> tuple[float, float]:
    x, y, z = vertex
    return 29.0*(x-y), 14.5*(x+y)-29.0*z


def weights(point: tuple[float, float], triangle: list[tuple[float, float]]) -> tuple[float, float, float]:
    def area(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    denominator = area(*triangle)
    return (area(point, triangle[1], triangle[2])/denominator,
            area(triangle[0], point, triangle[2])/denominator,
            area(triangle[0], triangle[1], point)/denominator)


def oct_normal(x: int, y: int) -> tuple[float, float, float]:
    a, b = x/127.5-1.0, y/127.5-1.0
    c = 1.0-abs(a)-abs(b)
    if c < 0:
        a, b = math.copysign(1.0-abs(b), a), math.copysign(1.0-abs(a), b)
    length = math.sqrt(a*a+b*b+c*c)
    return a/length, b/length, c/length


def find_sprite(catalog: dict, key: str) -> tuple[dict, dict]:
    matches = [(p, s) for p in catalog["pages"] for s in p["sprites"] if s["id"] == key]
    require(len(matches) == 1, "Original test sprite is ambiguous")
    return matches[0]


def crop_bytes(repo: Path, page: dict, sprite: dict) -> bytes:
    from PIL import Image
    x, y, width, height = sprite["rect"]
    with Image.open(repo/"assets"/page["path"]) as image:
        require(image.mode == "RGBA", "Original fixture PNG is not straight RGBA")
        return image.crop((x,y,x+width,y+height)).tobytes()


def diagnostic_master(sprite: dict, paint: bytes) -> dict:
    """Two noncoplanar authored surfaces, solely a registration/math test model."""
    width, height = sprite["rect"][2:]
    pivot = [f32(f32(sprite["pivot"][i])*size) for i, size in enumerate((width,height))]
    scale = 4.0
    screen = [(0.,0.), (float(width),0.), (float(width),float(height)), (0.,float(height))]
    depths = (0.0,3.0,1.0,2.0)
    points = [inverse(((x-pivot[0])/scale,(y-pivot[1])/scale),d) for (x,y),d in zip(screen,depths)]
    triangles = []
    for index, indices in enumerate(((0,1,2),(0,2,3))):
        triangles.append({"id": f"plane.{index}", "vertices": [points[i] for i in indices],
                          "albedo": [91,113,127,255], "roughness": .37+index*.2, "ao": .81,
                          "emission_mask": .23, "wet_mask": .61, "caster_mask": .73})
    candidates = []
    for y in range(height):
        for x in range(width):
            if paint[(y*width+x)*4+3] < 64:
                continue
            for index, indices in enumerate(((0,1,2),(0,2,3))):
                triangle = [screen[i] for i in indices]
                ws = weights((x+.5,y+.5),triangle)
                if min(ws) <= 0:
                    continue
                distances = []
                for i in range(3):
                    a,b = triangle[i],triangle[(i+1)%3]
                    distances.append(abs((b[1]-a[1])*(x+.5-a[0])-(b[0]-a[0])*(y+.5-a[1]))/math.dist(a,b))
                if min(distances) >= 9.0:
                    candidates.append((index,x,y,ws))
    require(len(candidates) >= 12, "Original crop has fewer than twelve safe diagnostic probes with alpha >=64")
    chosen = [candidates[i*(len(candidates)-1)//11] for i in range(12)]
    probes = [{"id": f"probe.{i}", "triangle_id": f"plane.{index}", "barycentric": list(ws)}
              for i,(index,_,_,ws) in enumerate(chosen)]
    return {"schema": 1, "coordinate_system": "ribterra-world-xyz-v1",
            "purpose": "diagnostic registered two-plane fixture; not a production model of this painted resident/building",
            "ids": {"material_off":"fixture.material.off","material_on":"fixture.material.on",
                    "sprite_off":"fixture.off","sprite_on":"fixture.on"},
            "emission_radiance": [.6,.3,.1,1.0],
            "export": {"export_pixels_per_reference_pixel":scale,"padding_export_pixels":0},
            "local_occluders": [], "sky_shadow_triangles": [], "triangles":triangles, "probes":probes}


def make_item(repo: Path, root: Path, catalog: dict, sources: dict, key: str, namespace: str) -> tuple[dict, dict, bytes]:
    page,sprite = find_sprite(catalog,key)
    paint = crop_bytes(repo,page,sprite)
    master = diagnostic_master(sprite,paint)
    master_path = root/"fixtures"/(namespace+".json")
    write(master_path,canonical(master))
    metadata = sources["overrides"][page["path"]]
    width,height = sprite["rect"][2:]
    item = {"namespace":namespace,"legacy_key":key,"master_path":master_path.relative_to(repo).as_posix(),
            "master_sha256":digest(master_path.read_bytes()),
            "registration":{"kind":"original-catalog-screen-v1","png_sha256":digest((repo/"assets"/page["path"]).read_bytes()),
                            "catalog_entry_sha256":digest(canonical({"page":{"path":page["path"]},"sprite":sprite})),
                            "source_size":[width,height],
                            "pivot_px":[f32(f32(sprite["pivot"][i])*size) for i,size in enumerate((width,height))],
                            "reference_width":f32(float(sprite.get("reference_width",width))),
                            "caster_alpha_policy":"painted-alpha-times-authored-mask"},
            "provenance":{"license":metadata["license"],"author":metadata["author"],"source":metadata["source"],
                          "notice_path":metadata["notice"],"notice_sha256":digest((repo/metadata["notice"]).read_bytes()),
                          "asset_source_sha256":digest(canonical(metadata))},
            "geometry_provenance":{"kind":"authored-proxy-xyz-v1","author":"Diagnostic test fixture authored in material_collection_export_tests.py"},
            "references":[{"path":"art-source/engine-calibration/material-master.json",
                           "sha256":digest((repo/"art-source/engine-calibration/material-master.json").read_bytes()),"purpose":"visual-only"}]}
    return item,master,paint


def validate_manifest(files: dict[str, bytes]) -> None:
    manifest = json.loads(files["manifest.json"])
    encoded = ""
    for row in sorted(manifest["assets"],key=lambda r:r["id"]):
        data = files[row["path"]]
        require(len(data)==row["bytes"] and digest(data)==row["sha256"],"Manifest payload binding failed")
        for field in ("id","path","kind","sha256","license","source","author","notice"):
            value=row[field]; encoded+=f"{len(value.encode('utf-8'))}:{value}"
        encoded+="\n"
        require(row["notice"] in files,"Original/generated notice is absent")
    require(manifest["schema"]==1 and digest(encoded.encode("utf-8"))==manifest["content_sha256"],"Manifest canonical ledger failed")


def validate_geometry(master: dict, registration: dict, paint: bytes, data: dict[str, bytes], depth_range: list[float]) -> dict:
    width,height=registration["source_size"]; pivot=registration["pivot_px"]; scale=master["export"]["export_pixels_per_reference_pixel"]
    projected=[list(map(project,t["vertices"])) for t in master["triangles"]]
    normals=[normal(t["vertices"]) for t in master["triangles"]]
    maximum=0.0; count=0; seen=set()
    require(data["albedo"]==paint,"Painted crop RGBA was changed/resampled")
    for pixel in range(width*height):
        alpha=paint[pixel*4+3]
        if not alpha:
            for channel,bpp in (("normal_material",4),("camera_depth",2),("emission_mask",1),("surface_mask",4),("caster_mask",1)):
                require(data[channel][pixel*bpp:(pixel+1)*bpp]==bytes(bpp),"Transparent data support is nonzero")
            continue
        screen=((pixel%width+.5-pivot[0])/scale,(pixel//width+.5-pivot[1])/scale)
        winners=[]
        for index,t in enumerate(master["triangles"]):
            ws=weights(screen,projected[index])
            if min(ws)<-1e-10:
                continue
            xyz=[sum(ws[k]*t["vertices"][k][axis] for k in range(3)) for axis in range(3)]
            winners.append((sum(xyz)/math.sqrt(3.0),-index,xyz))
        require(bool(winners),"Independent oracle found uncovered painted alpha")
        frontmost=max(row[0] for row in winners)
        _,negative_index,expected=next(row for row in winners if row[0]>=frontmost-1e-12)
        index=-negative_index; t=master["triangles"][index]
        depth=depth_range[0]+struct.unpack_from("<H",data["camera_depth"],pixel*2)[0]/65535.0*(depth_range[1]-depth_range[0])
        reconstructed=inverse(screen,depth)
        error=max(abs(a-b) for a,b in zip(expected,reconstructed)); maximum=max(maximum,error)
        require(error<=.02,"Independent XYZ reconstruction exceeds .02 tile")
        decoded=oct_normal(*data["normal_material"][pixel*4:pixel*4+2])
        cosine=max(-1.0,min(1.0,sum(a*b for a,b in zip(decoded,normals[index]))))
        require(math.degrees(math.acos(cosine))<=1.5,"Independent winding normal exceeds 1.5 degrees")
        require(data["normal_material"][pixel*4+2:pixel*4+4]==bytes((round(t["roughness"]*255),round(t["ao"]*255))),"Roughness/AO came from paint")
        require(data["emission_mask"][pixel]==round(t["emission_mask"]*255),"Emission mask differs from authored value")
        require(data["surface_mask"][pixel*4:pixel*4+4]==bytes((round(t["wet_mask"]*255),0,0,255)),"Surface mask policy differs")
        expected_caster=round(alpha*t["caster_mask"]) if registration["caster_alpha_policy"]=="painted-alpha-times-authored-mask" else round(t["caster_mask"]*255)
        require(data["caster_mask"][pixel]==expected_caster,"Caster alpha policy differs")
        count+=1; seen.add(index)
    return {"supported_alpha_samples":count,"max_xyz_error":maximum,"visible_authored_planes":sorted(seen)}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo",type=Path); parser.add_argument("output_root",type=Path)
    parser.add_argument("--baseline-root",type=Path,required=True)
    args=parser.parse_args(); repo=args.repo.resolve(); root=Path(os.path.abspath(args.output_root)); baseline=args.baseline_root.resolve()
    require(repo==Path(__file__).resolve().parent.parent,"Tests must use the authoritative current source repository")
    require(root.parent==repo/"build" and not os.path.lexists(root),"Test root must be a fresh immediate build child")
    require(shutil.disk_usage(repo/"build").free>=(384+128)*1024*1024,"Tests need 384 MiB plus their bounded retained-output budget")
    baseline_files=tree(baseline); validate_manifest(baseline_files)
    baseline_metadata=json.loads(baseline_files["art/material/calibration.json"])
    require(baseline_metadata["exporter_sha256"]==BASELINE_EXPORTER and baseline_metadata["master_sha256"]==BASELINE_MASTER,"Baseline is not the retained prechange source cohort")
    exporter=repo/"tools/export_material_master.py"; original_master=repo/"art-source/engine-calibration/material-master.json"
    require(digest(original_master.read_bytes())==BASELINE_MASTER,"Current calibration master differs from the accepted prechange input")
    os.mkdir(root)
    commands=[]; outputs=[]
    def invoke(input_path: Path, output: Path, label: str, collection: bool=True, diagnostic: str|None=None) -> None:
        argv=[sys.executable,"-B",str(exporter),str(input_path),str(output)]+(["--collection"] if collection else [])
        result=subprocess.run(argv,cwd=repo,text=True,encoding="utf-8",stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                              env=dict(os.environ,PYTHONDONTWRITEBYTECODE="1"),check=False)
        write(root/"logs"/(label+".stdout.txt"),result.stdout.encode("utf-8")); write(root/"logs"/(label+".stderr.txt"),result.stderr.encode("utf-8"))
        commands.append({"id":label,"argv":argv,"exit_code":result.returncode,"expected_diagnostic":diagnostic})
        if diagnostic is None:
            require(result.returncode==0,f"{label} failed: {result.stderr}"); outputs.append(output)
        else:
            require(result.returncode==2 and diagnostic in result.stderr,f"{label} rejection differs: {result.stderr}")
    current_default=root/"current-default"; invoke(original_master,current_default,"default",False)
    default_files=tree(current_default); validate_manifest(default_files)
    require(set(default_files)==set(baseline_files),"Default output filename set changed")
    for relative in default_files:
        if relative not in ("art/material/calibration.json","manifest.json"):
            require(default_files[relative]==baseline_files[relative],f"Default channel/world/notice bytes changed: {relative}")
    default_metadata=json.loads(default_files["art/material/calibration.json"])
    require(default_metadata["exporter_sha256"]==digest(exporter.read_bytes()),"Default exporter hash was not rebound")
    default_metadata.pop("exporter_sha256"); old_metadata=copy.deepcopy(baseline_metadata); old_metadata.pop("exporter_sha256")
    require(default_metadata==old_metadata,"Default numeric/camera/settings/probe semantics changed")
    old_manifest=json.loads(baseline_files["manifest.json"]); new_manifest=json.loads(default_files["manifest.json"])
    expected_rows=copy.deepcopy(old_manifest["assets"])
    for row in expected_rows:
        if row["path"]=="art/material/calibration.json":
            row["sha256"]=digest(default_files[row["path"]]); row["bytes"]=len(default_files[row["path"]])
    require(new_manifest["assets"]==expected_rows,"Default manifest changed beyond the correctly rebound calibration row")
    catalog=json.loads((repo/"assets/art/world.json").read_bytes()); sources=json.loads((repo/"content/asset_sources.json").read_bytes())
    fixture_rows=[make_item(repo,root,catalog,sources,key,namespace) for key,namespace in (
        ("building.house.ready","fixture.house"),("resident.man.walk.2.1","fixture.resident"))]
    collection={"schema":1,"kind":"ribterra-material-collection-v1","items":[item for item,_,_ in fixture_rows]}
    collection_path=root/"fixtures/collection.json"; write(collection_path,canonical(collection))
    def sibling(label: str) -> Path:
        return root.parent/(root.name+"-"+label)
    export_a,export_b=sibling("export-a"),sibling("export-b")
    invoke(collection_path,export_a,"repeat-a"); invoke(collection_path,export_b,"repeat-b")
    files_a,files_b=tree(export_a),tree(export_b); require(files_a==files_b,"Full collection output is not byte-repeatable")
    validate_manifest(files_a)
    report=json.loads(files_a["material-collection-report.json"])
    require(report["inputs_before"]==report["inputs_after"] and report["output_inventory_excluding_report"]==rows({p:d for p,d in files_a.items() if p!="material-collection-report.json"}),"Collection source/output ledger differs")
    require(report["output_file_count"]==len(files_a) and report["output_total_bytes"]==sum(map(len,files_a.values()))<=128*1024*1024,"Collection total byte ledger differs")
    for row in report["inputs_before"]:
        data=(repo/row["relative_path"]).read_bytes(); require(len(data)==row["bytes"] and digest(data)==row["sha256"],"Original/source input was changed")
    world=json.loads(files_a["art/world.json"]); metadata=json.loads(files_a["art/material-collection.json"])
    require(world["schema"]==2 and world["pages"]==[] and len(world["textures"])==12 and len(world["materials"])==2 and len(world["lit_sprites"])==2,"Collection descriptor cardinality differs")
    require(metadata["reference_fidelity"]=="NOT_ACCEPTED" and metadata["production_master_acceptance"]=="OPEN" and metadata["paint_colour"]=="legacy-painted-neutral-albedo-acceptance-pending","Diagnostic scope was overstated")
    require(metadata["painted_numeric_probe_min_alpha"]==64 and all(probe["albedo"][3]>=64 and probe["boundary_clearance_px"]>=8.0 for entry in metadata["items"] for probe in entry["probes"]),"Painted numeric probe alpha/clearance scope differs")
    geometry_checks=[]
    texture_rows={row["id"]:row for row in world["textures"]}
    for (item,master,paint),entry in zip(fixture_rows,metadata["items"]):
        data={channel:files_a[f"art/materials/{item['namespace']}/{filename}"] for channel,filename in zip(CHANNELS,FILENAMES)}
        require(entry["registration"]["reference_width"]==item["registration"]["reference_width"],"Reference width was dropped")
        require(entry["sprite_id"]=="dynamic."+item["legacy_key"],"Dynamic ID was slugged or remapped")
        material=next(m for m in world["materials"] if m["id"]==entry["material_id"])
        sprite=next(s for s in world["lit_sprites"] if s["id"]==entry["sprite_id"])
        require(material["family"]=="DynamicSprite25D" and material["alpha"]=="straight" and material["camera"]=="ribterra-affine-v1" and material["emission"]==master["emission_radiance"],"Material family/basis/emission differs")
        require(sprite["source_size"]==item["registration"]["source_size"] and sprite["pivot_px"]==item["registration"]["pivot_px"] and sprite["depth_range"]==entry["depth_range"] and sprite["export_pixels_per_reference_pixel"]==4.0,"Sprite source registration differs")
        formats=("rgba8_unorm_srgb","rgba8_unorm","r16_unorm","r8_unorm","rgba8_unorm","r8_unorm")
        for channel,filename,texture_format in zip(CHANNELS,FILENAMES,formats):
            texture_id=item["namespace"]+".texture."+channel; texture=texture_rows[texture_id]
            require(material["channels"][channel]=={"texture":texture_id,"rect":[0,0,*item["registration"]["source_size"]]},"Material UV/channel ownership differs")
            require(texture["path"]==f"art/materials/{item['namespace']}/{filename}" and texture["format"]==texture_format and texture["layout"]=="tight_le" and texture["mip_levels"]==1 and [texture["width"],texture["height"]]==item["registration"]["source_size"] and texture["bytes"]==len(data[channel]) and texture["sha256"]==digest(data[channel]),"Texture payload/format binding differs")
        notice=item["provenance"]["notice_path"]
        require(files_a[notice]==(repo/notice).read_bytes(),"Original notice was rewritten")
        record={key:entry[key] for key in ("master_sha256","registration","geometry_provenance")}
        require(material["source_hash"]==entry["source_hash"]==digest(canonical(record)),"Combined material source hash differs")
        geometry_checks.append({"legacy_key":item["legacy_key"],**validate_geometry(master,item["registration"],paint,data,entry["depth_range"])})
    require(fixture_rows[1][0]["registration"]["reference_width"]==222 and fixture_rows[1][0]["registration"]["source_size"][0]==139,"Resident fixture does not exercise reference-width compensation")
    # Exercise the real compatible raster API with RGB-only mutation. Original
    # production images stay untouched; these retained buffers are diagnostic.
    spec=importlib.util.spec_from_file_location("tested_material_exporter",exporter); require(spec is not None and spec.loader is not None,"Cannot load source raster API")
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    item,master,paint=fixture_rows[1]; _,parsed=module.parse_master(canonical(master))
    parsed["painted_registration"]={**item["registration"],"rgba":paint}
    direct_a=module.rasterize(parsed); changed=bytearray(paint)
    for pixel in range(len(paint)//4):
        for channel in range(3):changed[pixel*4+channel]=255-changed[pixel*4+channel]
    painted_b=copy.deepcopy(parsed); painted_b["painted_registration"]["rgba"]=bytes(changed)
    direct_b=module.rasterize(painted_b)
    require(direct_a["albedo"]!=direct_b["albedo"] and all(direct_a[c]==direct_b[c] for c in CHANNELS[1:]),"RGB paint altered XYZ/material channels")
    # Reparse the authored modification, keeping projection unchanged by moving
    # one shared camera ray along XYZ equally; normals must change geometrically.
    changed_master=copy.deepcopy(master)
    changed_master["triangles"][0]["vertices"][1]=[v+.4 for v in changed_master["triangles"][0]["vertices"][1]]
    _,geometric=module.parse_master(canonical(changed_master)); geometric["painted_registration"]={**item["registration"],"rgba":paint}
    direct_geometry=module.rasterize(geometric)
    require(direct_geometry["albedo"]==direct_a["albedo"] and direct_geometry["normal_material"]!=direct_a["normal_material"],"Authored XYZ mutation did not independently alter winding normals")
    for label,raster in (("direct-paint-a",direct_a),("direct-paint-b",direct_b),("direct-geometry",direct_geometry)):
        for channel,filename in zip(CHANNELS,FILENAMES):write(root/label/filename,raster[channel])
        write(root/label/"depth-range.json",canonical(raster["depth_range"]))
    validate_geometry(changed_master,item["registration"],paint,{c:direct_geometry[c] for c in CHANNELS},list(direct_geometry["depth_range"]))
    fractional=[]
    shifted=copy.deepcopy(master)
    for triangle in shifted["triangles"]:
        for vertex in triangle["vertices"]:
            vertex[0]-=100; vertex[1]+=100
    for alpha in (1,63,128,255):
        _,bad=module.parse_master(canonical(shifted)); test_paint=bytearray(len(paint)); test_paint[3]=alpha
        bad["painted_registration"]={**item["registration"],"rgba":bytes(test_paint)}
        try:module.rasterize(bad)
        except module.ExportError as error:
            require("no authored XYZ geometry" in str(error),"Fractional alpha rejection had another cause"); fractional.append(alpha)
        else:raise AssertionError("Nonzero fractional alpha escaped geometry validation")
    negatives=[]
    def negative(label: str, mutate, diagnostic: str, master_change=None) -> None:
        candidate=copy.deepcopy(collection)
        if master_change is not None:
            new_master=copy.deepcopy(fixture_rows[0][1]); master_change(new_master)
            path=root/"fixtures"/(label+"-master.json"); write(path,canonical(new_master))
            candidate["items"][0]["master_path"]=path.relative_to(repo).as_posix(); candidate["items"][0]["master_sha256"]=digest(path.read_bytes())
        mutate(candidate)
        path=root/"fixtures"/(label+"-collection.json"); write(path,canonical(candidate)); output=sibling("negative-"+label)
        invoke(path,output,"negative-"+label,diagnostic=diagnostic)
        require(not os.path.lexists(output),f"Rejected {label} created output"); negatives.append(label)
    negative("unknown-item",lambda c:c["items"][0].update(extra=1),"collection item fields")
    negative("duplicate-namespace",lambda c:c["items"][1].update(namespace=c["items"][0]["namespace"]),"Duplicate or invalid namespace")
    negative("duplicate-legacy",lambda c:c["items"][1].update(legacy_key=c["items"][0]["legacy_key"]),"Duplicate or invalid legacy_key")
    negative("namespace-case",lambda c:c["items"][0].update(namespace="Fixture.house"),"invalid namespace")
    negative("namespace-path",lambda c:c["items"][0].update(namespace="../house"),"invalid namespace")
    negative("unsorted",lambda c:c["items"].reverse(),"sorted by namespace")
    negative("master-escape",lambda c:c["items"][0].update(master_path="../outside.json"),"confined repository-relative")
    negative("master-hash",lambda c:c["items"][0].update(master_sha256="0"*64),"Declared input hash mismatch")
    negative("png-hash",lambda c:c["items"][0]["registration"].update(png_sha256="0"*64),"Declared input hash mismatch")
    negative("catalog-hash",lambda c:c["items"][0]["registration"].update(catalog_entry_sha256="0"*64),"catalog entry hash mismatch")
    negative("extent",lambda c:c["items"][0]["registration"].update(source_size=[1,1]),"extent/pivot/reference width")
    negative("pivot",lambda c:c["items"][0]["registration"].update(pivot_px=[0,0]),"extent/pivot/reference width")
    negative("reference-width",lambda c:c["items"][1]["registration"].update(reference_width=139),"extent/pivot/reference width")
    negative("caster-policy",lambda c:c["items"][0]["registration"].update(caster_alpha_policy="brightness"),"Unsupported caster alpha policy")
    negative("source-provenance",lambda c:c["items"][0]["provenance"].update(license="Invented"),"provenance differs")
    negative("unknown-provenance",lambda c:c["items"][0]["provenance"].update(extra="ignored"),"provenance fields")
    negative("unknown-geometry-metadata",lambda c:c["items"][0]["geometry_provenance"].update(verified=True),"geometry_provenance fields")
    negative("notice-hash",lambda c:c["items"][0]["provenance"].update(notice_sha256="0"*64),"Declared input hash mismatch")
    negative("reference-hash",lambda c:c["items"][0]["references"][0].update(sha256="0"*64),"Declared input hash mismatch")
    negative("reference-claim",lambda c:c["items"][0]["references"][0].update(purpose="geometry-reconstruction"),"visual-only scope")
    negative("unknown-registration",lambda c:c["items"][0]["registration"].update(alpha_mode="magenta"),"registration fields")
    negative("unsupported-emitter",lambda c:None,"Unsupported collection master metadata",lambda m:m.update(local_emitters=[]))
    negative("unsupported-contact",lambda c:None,"Unsupported collection master metadata",lambda m:m.update(contact_footprints=[]))
    negative("unknown-triangle",lambda c:None,"master.triangles[0] fields",lambda m:m["triangles"][0].update(snow_mask=.5))
    negative("invalid-normal",lambda c:None,"degenerate",lambda m:m["triangles"][0].update(vertices=[[0,0,0]]*3))
    def move_outside(m):
        for triangle in m["triangles"]:
            for vertex in triangle["vertices"]:
                vertex[0]-=100; vertex[1]+=100
    negative("uncovered-alpha",lambda c:None,"no authored XYZ geometry",move_outside)
    # A retained junction/symlink aliases only our diagnostic fixtures; it is
    # never removed or followed by writes, and must be rejected as an input.
    junction=root/"retained-reparse-fixtures"
    if os.name=="nt":
        result=subprocess.run(["cmd.exe","/d","/c","mklink","/J",str(junction),str(root/"fixtures")],capture_output=True,check=False)
        write(root/"logs/junction.stdout.txt",result.stdout); write(root/"logs/junction.stderr.txt",result.stderr)
        require(result.returncode==0,"Could not create retained reparse negative; no result is inferred")
    else:os.symlink(root/"fixtures",junction,target_is_directory=True)
    linked_master=(junction/"fixture.house.json").relative_to(repo).as_posix()
    negative("reparse-input",lambda c:c["items"][0].update(master_path=linked_master),"Reparse/symlink path is forbidden")
    existing=sibling("existing"); os.mkdir(existing); sentinel=b"preserve-existing-material-collection\n"; write(existing/"sentinel.txt",sentinel)
    invoke(collection_path,existing,"existing-output",diagnostic="fresh immediate repository build child")
    require(tree(existing)=={"sentinel.txt":sentinel},"Existing output was changed"); negatives.append("existing-output")
    nested=root/"nested-output"; invoke(collection_path,nested,"nested-output",diagnostic="fresh immediate repository build child")
    require(not nested.exists(),"Nested forbidden output was created"); negatives.append("nested-output")
    require(tree(baseline)==baseline_files,"Retained baseline changed")
    for row in report["inputs_after"]:
        data=(repo/row["relative_path"]).read_bytes(); require(len(data)==row["bytes"] and digest(data)==row["sha256"],"Bound source/original changed after negative controls")
    evidence={"schema":1,"complete":True,"scope":"CPU diagnostic authored screen registration; not production geometry/art/native acceptance",
              "baseline_root":str(baseline),"baseline_inventory":rows(baseline_files),"default_compatibility":True,
              "collection_repeat_exact":True,"collection_inventory":rows(files_a),"geometry_controls":geometry_checks,
              "paint_rgb_only_geometry_exact":True,"authored_geometry_normal_changed":True,"fractional_uncovered_alpha_rejected":fractional,
              "resident_reference_width":222,"negative_controls":negatives,"commands":commands,
              "exporter_sha256":digest(exporter.read_bytes()),"test_sha256":digest(Path(__file__).read_bytes()),
              "collection_outputs":[str(p) for p in outputs if p.parent==repo/"build"],"retained_reparse_path":str(junction),
              "original_images_changed":False,"production_eight_masters":"OPEN","reference_fidelity":"NOT_ACCEPTED","native":"OPEN"}
    write(root/"material-collection-test-report.json",canonical(evidence))
    require(shutil.disk_usage(repo/"build").free>=384*1024*1024,"Tests ended below 384 MiB headroom")
    print(json.dumps({"complete":True,"negative_controls":len(negatives),"report":str(root/"material-collection-test-report.json")},ensure_ascii=False))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
