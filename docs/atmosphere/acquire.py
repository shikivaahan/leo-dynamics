#!/usr/bin/env python3
"""Explicit acquisition outside CMake. No upstream rights are inferred from download."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile


def verified(content, expected, name):
    actual = hashlib.sha256(content).hexdigest()
    if actual != expected:
        raise RuntimeError(f"{name}: expected SHA-256 {expected}, got {actual}; "
                           "obtain the archived baseline asset, do not silently re-pin")
    return content


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--groups", nargs="+", choices=("nrlmsise00", "jb2008", "weather", "kernels"), required=True)
    parser.add_argument("--acknowledge-external-terms", action="store_true",
                        help="Caller has inspected applicable upstream use terms; not a redistribution grant")
    args = parser.parse_args()
    if not args.acknowledge_external_terms:
        parser.error("Inspect upstream terms, then pass --acknowledge-external-terms explicitly")
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "evidence/upstream-assets.json").read_text())
    destination = args.destination.resolve()
    repo = root.parent.parent
    if destination == repo or repo in destination.parents:
        parser.error("External assets must be acquired outside the repository")
    destination.mkdir(parents=True, exist_ok=True)
    for asset in manifest["assets"]:
        if asset["group"] not in args.groups:
            continue
        target = destination / asset["filename"]
        if target.exists():
            content = verified(target.read_bytes(), asset["sha256"], str(target))
        else:
            with urllib.request.urlopen(asset["url"], timeout=180) as response:
                content = verified(response.read(), asset["sha256"], asset["filename"])
            with target.open("xb") as output:
                output.write(content)
        if "extract" in asset:
            entry = asset["extract"]
            with zipfile.ZipFile(io.BytesIO(content)) as archive:
                extracted = verified(archive.read(entry["member"]), entry["sha256"], entry["filename"])
            path = destination / entry["filename"]
            if path.exists():
                verified(path.read_bytes(), entry["sha256"], str(path))
            else:
                with path.open("xb") as output:
                    output.write(extracted)
        if asset["filename"] == "jb2008validate.zip":
            with zipfile.ZipFile(io.BytesIO(content)) as archive:
                for info in archive.infolist():
                    if info.filename.startswith("Validation/expected output/") and info.filename.endswith(".DAT"):
                        path = destination / "jb-validation" / Path(info.filename).name
                        path.parent.mkdir(exist_ok=True)
                        value = archive.read(info)
                        if path.exists():
                            verified(path.read_bytes(), hashlib.sha256(value).hexdigest(), str(path))
                        else:
                            with path.open("xb") as output:
                                output.write(value)
        print(f"{asset['filename']} {asset['sha256']}")


if __name__ == "__main__":
    main()
