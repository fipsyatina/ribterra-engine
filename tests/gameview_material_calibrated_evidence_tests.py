"""Supplemental read-only reader for the retained material runner's after rows.

The frozen verifier expects relative_path in both observations. The frozen
runner records that label only in before rows. Derive the missing after labels
from their recorded absolute paths, then execute every original verifier check.
No compiled source, JSON, capture, binding, byte count or SHA is rewritten.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path

CORE_SHA256 = "365320787E37AD4EB54FAE6369A95B59BF71826971E5FDD3038DA1F05C5EFF0E"
REPOSITORY = Path(r"C:\Users\fipst\Desktop\ribterra")
AFTER_FIELDS = {"path", "bytes", "sha256", "after_bytes", "after_sha256", "unchanged"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def binding(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest().upper()}


def normalize_after(rows: list, root: Path) -> tuple[list, int]:
    """Add only a derived relative label; reject aliases and escaped paths."""
    require(type(rows) is list, "After observation must be a list")
    require(root.is_absolute() and root.resolve(strict=True) == root, "Noncanonical asset root")
    normalized = copy.deepcopy(rows)
    seen: set[str] = set()
    added = 0
    for source, row in zip(rows, normalized, strict=True):
        require(type(row) is dict and set(row) in (AFTER_FIELDS, AFTER_FIELDS | {"relative_path"}),
                "Unexpected raw after-row fields")
        require(type(row["path"]) is str, "After path must be a string")
        path = Path(row["path"])
        require(path.is_absolute() and str(path) == row["path"] and ".." not in path.parts and
                path.resolve(strict=True) == path and path.is_file(), "Unsafe or noncanonical recorded path")
        relative = path.relative_to(root)
        label = relative.as_posix()
        require(relative.parts and not relative.anchor and ":" not in label and
                root / relative == path and label.casefold() not in seen, "Escaped or duplicate after path")
        seen.add(label.casefold())
        if "relative_path" in row:
            require(type(row["relative_path"]) is str and row["relative_path"] == label,
                    "Existing after label disagrees with recorded absolute path")
        else:
            row["relative_path"] = label
            added += 1
        # This comparison protects all original fields, including their types.
        reconstructed = dict(row)
        if "relative_path" not in source:
            del reconstructed["relative_path"]
        require(reconstructed == source and set(reconstructed) == set(source), "Calibration changed raw evidence")
    return normalized, added


def negative_controls(raw: dict, roots: dict[str, Path]) -> int:
    """Exercise label-specific rejection on in-memory copies of real rows."""
    checks = 0
    for prefix, root in roots.items():
        rows = raw["asset_trees"][f"{prefix}_after"]
        require(rows, "Empty after tree cannot qualify calibration")
        for case in ("duplicate", "outside", "wrong_label", "extra_field", "traversal"):
            changed = copy.deepcopy(rows)
            if case == "duplicate":
                changed.append(copy.deepcopy(changed[0]))
            elif case == "outside":
                changed[0]["path"] = str(REPOSITORY / "tests/gameview_material_evidence_tests.py")
            elif case == "wrong_label":
                changed[0]["relative_path"] = "wrong-relative-label"
            elif case == "extra_field":
                changed[0]["invented"] = True
            else:
                changed[0]["path"] = str(root / ".." / root.name / Path(changed[0]["path"]).relative_to(root))
            try:
                normalize_after(changed, root)
            except (AssertionError, ValueError):
                checks += 1
            else:
                raise AssertionError(f"Calibration negative control accepted: {prefix}/{case}")
    return checks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "ui_kit", "ui_repeat", "material", "first", "second"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--receipt", type=Path,
                        default=REPOSITORY / ".unlazy/engine-completion-2026-10-08/material-calibrated-actual-2026-10-08.json")
    args = parser.parse_args()
    arguments = [getattr(args, name).resolve(strict=True)
                 for name in ("build", "kit", "ui_kit", "ui_repeat", "material", "first", "second")]
    build, kit, ui_kit, ui_repeat, material, first, second = arguments
    require(len(set(arguments)) == len(arguments), "Seven distinct evidence roots required")
    receipt = args.receipt.absolute()
    receipt_parent = receipt.parent.resolve(strict=True)
    require(receipt_parent == REPOSITORY / ".unlazy/engine-completion-2026-10-08" and
            not receipt.exists(), "Receipt must be fresh and outside immutable output roots")
    core_path = REPOSITORY / "tests/gameview_material_evidence_tests.py"
    own_path = Path(__file__).resolve(strict=True)
    raw_paths = [root / "native-execution.json" for root in (first, second)]
    guarded = [core_path, own_path, build / "build-report.json", *raw_paths]
    before = [binding(path) for path in guarded]
    require(before[0]["sha256"] == CORE_SHA256, "Frozen verifier identity changed")
    # Import unchanged code only after its identity is checked.
    import gameview_material_evidence_tests as core

    require(Path(core.__file__).resolve(strict=True) == core_path, "Wrong verifier module imported")
    original_load = core.load
    report = original_load(build / "build-report.json")
    require(Path(report["authoritative_repository"]) == REPOSITORY and Path(report["output_root"]) == build and
            Path(report["control_kit"]["root"]) == kit and Path(report["ui_kit"]["root"]) == ui_kit and
            Path(report["original_art"]["root"]) == REPOSITORY / "assets" and
            Path(report["material_collection"]["root"]) == material, "Expected asset roots are not build-bound")
    frozen_source = next(row for row in report["source_provenance"]
                         if row["relative_path"] == "tests/gameview_material_evidence_tests.py")
    require(frozen_source["sha256"].upper() == CORE_SHA256, "Compiled cohort verifier snapshot differs")
    roots = {"control": kit, "ui": ui_kit, "original": REPOSITORY / "assets", "material": material}
    derived: dict[str, dict] = {}
    controls = 0
    for path, root in zip(raw_paths, (first, second), strict=True):
        raw = original_load(path)
        require(Path(raw["build_root"]) == build and Path(raw["output_root"]) == root and
                Path(raw["kit_root"]) == kit and Path(raw["ui_kit_root"]) == ui_kit,
                "Raw native root linkage differs")
        controls += negative_controls(raw, roots)
        normalized = copy.deepcopy(raw)
        counts = {}
        for prefix, asset_root in roots.items():
            key = f"{prefix}_after"
            normalized["asset_trees"][key], counts[prefix] = normalize_after(raw["asset_trees"][key], asset_root)
        derived[str(path)] = {"raw": raw, "normalized": normalized, "labels_added": counts, "load_count": 0}

    def calibrated_load(path: Path) -> dict:
        key = str(Path(path).resolve(strict=True))
        if key not in derived:
            return original_load(path)
        entry = derived[key]
        require(original_load(path) == entry["raw"], "Raw native evidence changed during verification")
        entry["load_count"] += 1
        return copy.deepcopy(entry["normalized"])

    # Reader-only substitution; the original guards and callbacks are untouched.
    core.load = calibrated_load
    try:
        results, counts = core.verify_bindings(*arguments)
        witnesses = [core.verify_semantics(root, REPOSITORY / "assets", material, native,
                                          core.verify_frame_contract, core.verify_query)
                     for root, native in zip((first, second), results, strict=True)]
        core.require(witnesses[0] == witnesses[1], "Repeated source-derived semantic witnesses differ")
    finally:
        core.load = original_load
    require(all(entry["load_count"] == 1 for entry in derived.values()), "Native calibration did not execute exactly twice")
    after = [binding(path) for path in guarded]
    require(before == after, "Frozen source or raw retained evidence changed")
    evidence = {
        "schema": 1, "kind": "supplemental-read-only-material-after-path-calibration-v1",
        "accepted": True, "original_verifier": before[0], "supplemental_source": before[1],
        "compiled_cohort_source": frozen_source, "arguments": [str(path) for path in arguments],
        "bindings_before": before, "bindings_after": after, "guarded_files_unchanged": True,
        "normalization": [{"path": path, "labels_added": entry["labels_added"], "load_count": entry["load_count"]}
                          for path, entry in derived.items()],
        "in_memory_negative_controls": controls, "original_verifier_counts": counts,
        "original_semantic_witnesses": witnesses,
        "claims": {"new_calibrator_part_of_compiled_snapshot": False, "normal_main": False,
                   "neutral_albedo": False, "reference_acceptance": False, "full_engine": False,
                   "fps": None, "automatic_gate_certification": False},
    }
    with receipt.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(evidence, stream, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        stream.write("\n")
    print("Calibrated original material verification passed; " +
          ", ".join(f"{key}={value}" for key, value in counts.items()))
    print(f"Derived after labels={sum(sum(entry['labels_added'].values()) for entry in derived.values())}; "
          f"negative_controls={controls}; receipt={receipt}")


if __name__ == "__main__":
    main()
