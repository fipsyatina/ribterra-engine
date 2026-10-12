"""Validate a fresh asset directory before replacing the previous build assets."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import nullcontext
from pathlib import Path
from asset_tool import build

project, binary, shaders, localization_check, ui_check = (Path(value).resolve() for value in sys.argv[1:])
destination = binary / "assets"
if not binary.is_relative_to((project / "build").resolve()) or destination.resolve().parent != binary:
    raise ValueError("Asset staging must stay inside the project's build directory")
preserve = os.environ.get("RIBTERRA_PRESERVE_BUILD_ARTIFACTS") == "1"
staging = (nullcontext(tempfile.mkdtemp(prefix=".ribterra-assets-retained-", dir=binary))
           if preserve else tempfile.TemporaryDirectory(prefix=".ribterra-assets-", dir=binary))
with staging as temporary:
    temporary = Path(temporary)
    candidate = temporary / "current"
    shutil.copytree(project / "assets", candidate)
    shutil.copytree(shaders, candidate / "shaders", dirs_exist_ok=True)
    manifest = build(project, candidate)
    (candidate / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n")
    subprocess.run([str(localization_check), str(candidate / "locales"), str(candidate / "fonts/NotoSans-Regular.ttf")], check=True)
    subprocess.run([str(ui_check), str(candidate)], check=True)
    previous = temporary / "previous"
    if destination.exists():
        destination.rename(previous)
    try:
        candidate.rename(destination)
    except BaseException:
        if previous.exists():
            previous.rename(destination)
        raise
print(f"Staged {len(manifest['assets'])} validated assets with content hash {manifest['content_sha256']}.")
if preserve:
    print(f"Preserved staging directory and previous assets: {temporary}")
