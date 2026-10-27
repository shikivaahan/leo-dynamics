#!/usr/bin/env python3
"""Fail closed on boundary drift, unsafe archives, changed public bytes or leaks.

Run from the private checkout before any artifact upload. The canonical manifest
is regenerated from reviewed rules; built members are compared to the approved
checkout bytes. Binary scans supplement, but cannot prove, compiled provenance.
"""
from __future__ import annotations

import argparse
import csv
import email.policy
import hashlib
import importlib.util
import io
import json
import re
import stat
import subprocess
import sys
import tarfile
import tomllib
import zipfile
from collections import Counter
from email.parser import BytesParser
from pathlib import Path

MANIFEST_RELATIVE = Path("release-audit/license-boundary.json")
GENERATOR_RELATIVE = Path("release-audit/generate_boundary.py")
WHEEL_PACKAGE_FILES = {"leo_dynamics/__init__.py", "leo_dynamics/__init__.pyi", "leo_dynamics/py.typed"}
WHEEL_MODULE_PATTERN = re.compile(r"^leo_dynamics/_core\.(?:cpython-[0-9]+-[A-Za-z0-9_-]+\.so|cp[0-9]+-[A-Za-z0-9_-]+\.pyd)$")
SCAN_CHUNK = 1 << 20


def _load_generator(root):
    spec = importlib.util.spec_from_file_location("leo_boundary_generator", root / GENERATOR_RELATIVE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _read_manifest(root):
    return json.loads((root / MANIFEST_RELATIVE).read_text())


def _scan_stream(markers, stream):
    encoded = [(marker, marker.encode()) for marker in markers]
    overlap = max((len(value) for _, value in encoded), default=1) - 1
    hits, previous = set(), b""
    while chunk := stream.read(SCAN_CHUNK):
        block = previous + chunk
        hits.update(marker for marker, value in encoded if value in block)
        previous = block[-overlap:] if overlap else b""
    return sorted(hits)


def _unsafe_name(name):
    # Do not normalise away empty/dot components before validating them.
    parts = name.rstrip("/").split("/")
    if not name or name.startswith(("/", "\\")) or "\\" in name or ":" in name or any(p in ("", ".", "..") for p in parts):
        return f"unsafe member name {name!r}"
    return None


def check_source(root, manifest, generator):
    canonical = generator.build_manifest(root)
    failures = []
    if canonical != manifest:
        failures.append("the complete licence boundary manifest is stale or altered")
    for path, entry in canonical["files"].items():
        if entry["class"] != "mit_core":
            continue
        source = root / path
        if source.is_symlink() or not source.is_file():
            failures.append(f"approved source is not a regular file: {path}")
            continue
        with source.open("rb") as handle:
            hits = _scan_stream(canonical["proprietary_markers"], handle)
        if hits:
            failures.append(f"{path}: proprietary markers {hits}")
    project = tomllib.loads((root / "pyproject.toml").read_text())["project"]
    if project.get("license") != "MIT":
        failures.append("project license must be MIT")
    if project.get("readme", {}).get("file") != "README.dist.md":
        failures.append("distribution readme must be README.dist.md")
    if set(project.get("license-files", [])) != {"LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"}:
        failures.append("project license-files differs from the approved exact list")
    return failures, {"canonical_manifest_matches": canonical == manifest}


def _metadata(payload, root):
    failures = []
    message = BytesParser(policy=email.policy.default).parsebytes(payload)
    project = tomllib.loads((root / "pyproject.toml").read_text())["project"]
    expected = {
        "Metadata-Version": ["2.4"], "Name": [project["name"]],
        "Version": [project["version"]], "Summary": [project["description"]],
        "Keywords": [",".join(project["keywords"])],
        "Author": [", ".join(item["name"] for item in project["authors"])],
        "License-Expression": [project["license"]],
        "License-File": project["license-files"],
        "Classifier": project["classifiers"],
        "Project-URL": [f"{key}, {value}" for key, value in project["urls"].items()],
        "Requires-Python": [",".join(sorted(project["requires-python"].split(",")))],
        "Requires-Dist": project["dependencies"],
        "Description-Content-Type": [project["readme"]["content-type"]],
    }
    actual_names = {name.lower() for name in message.keys()}
    if actual_names != {name.lower() for name in expected}:
        failures.append("metadata contains unknown or missing headers")
    for key, values in expected.items():
        actual = list(map(str, message.get_all(key, [])))
        if key == "Requires-Python":
            actual = [",".join(sorted(value.split(","))) for value in actual]
        if Counter(actual) != Counter(values):
            failures.append(f"metadata {key} differs from approved pyproject values")
    description = message.get_payload(decode=True).decode("utf-8")
    if not isinstance(description, str) or description.rstrip("\n") != (root / "README.dist.md").read_text().rstrip("\n"):
        failures.append("metadata description differs from the approved public readme")
    return failures


def _wheel_metadata(payload, path):
    message = BytesParser(policy=email.policy.default).parsebytes(payload)
    parts = path.name.removesuffix(".whl").rsplit("-", 3)
    if len(parts) != 4:
        return ["wheel filename has no interpreter/ABI/platform tags"]
    interpreter, abi, platform = parts[-3:]
    tags = {f"{i}-{a}-{p}" for i in interpreter.split(".")
            for a in abi.split(".") for p in platform.split(".")}
    expected = {
        "Wheel-Version": ["1.0"], "Generator": ["scikit-build-core 0.11.6"],
        "Root-Is-Purelib": ["false"], "Tag": sorted(tags),
    }
    failures = []
    if {name.lower() for name in message.keys()} != {name.lower() for name in expected}:
        failures.append("WHEEL has unknown or missing headers")
    for key, values in expected.items():
        if Counter(map(str, message.get_all(key, []))) != Counter(values):
            failures.append(f"WHEEL {key} differs from approved build/filename")
    if message.get_payload(decode=True).strip():
        failures.append("WHEEL must have no body")
    return failures


def _inspect(path, manifest, root, wheel):
    failures, inventory, contents = [], [], {}
    prefix = "leo_dynamics-0.1.0"
    expected = set(manifest["sdist_members"])
    with (zipfile.ZipFile(path) if wheel else tarfile.open(path, "r:gz")) as archive:
        entries = archive.infolist() if wheel else archive.getmembers()
        seen = set()
        for entry in entries:
            name = entry.filename if wheel else entry.name
            reason = _unsafe_name(name)
            if reason:
                failures.append(reason)
                continue
            if name in seen:
                failures.append(f"duplicate member {name!r}")
                continue
            seen.add(name)
            directory = entry.is_dir() if wheel else entry.isdir()
            if wheel:
                mode = entry.external_attr >> 16
                if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                    failures.append(f"not a regular file: {name}")
                    continue
            elif not (entry.isfile() or directory):
                failures.append(f"not a regular file: {name}")
                continue
            if not wheel and name.split("/", 1)[0] != prefix:
                failures.append(f"unexpected source archive root: {name}")
            if directory:
                continue
            if not wheel and "/" not in name:
                failures.append(f"member at the archive root: {name}")
                continue
            relative = name if wheel else name.split("/", 1)[1]
            with (archive.open(entry) if wheel else archive.extractfile(entry)) as handle:
                payload = handle.read()
            contents[relative] = payload
            inventory.append({"path": relative, "size": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
            hits = _scan_stream(manifest["proprietary_markers"], io.BytesIO(payload))
            if hits:
                failures.append(f"{relative}: proprietary markers {hits}")
            mapped = None
            if wheel:
                if relative in WHEEL_PACKAGE_FILES:
                    mapped = "python/" + relative
                elif WHEEL_MODULE_PATTERN.fullmatch(relative):
                    if not payload.startswith((b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe")):
                        failures.append(f"extension is not a recognised native binary: {relative}")
                elif relative.startswith(prefix + ".dist-info/licenses/"):
                    licence = relative.removeprefix(prefix + ".dist-info/licenses/")
                    if licence not in {"LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"}:
                        failures.append(f"unexpected licence member {relative}")
                    else:
                        mapped = licence
                elif relative not in {prefix + ".dist-info/" + child for child in ("METADATA", "WHEEL", "RECORD")}:
                    failures.append(f"unexpected member {relative}")
            elif relative in expected:
                mapped = relative
            elif relative != "PKG-INFO":
                failures.append(f"{relative}: not a member of the MIT core")
            if mapped is not None:
                original = root / mapped
                if original.is_symlink() or not original.is_file() or payload != original.read_bytes():
                    failures.append(f"{relative}: bytes differ from approved public source {mapped}")
        if wheel:
            required = WHEEL_PACKAGE_FILES | {prefix + ".dist-info/" + child for child in ("METADATA", "WHEEL", "RECORD", "licenses/LICENSE", "licenses/LICENSE_SCOPE.md", "licenses/THIRD_PARTY_NOTICES.md")}
            modules = [n for n in contents if WHEEL_MODULE_PATTERN.fullmatch(n)]
            if len(modules) != 1:
                failures.append("wheel must carry exactly one _core native module")
            missing = required - contents.keys()
            metadata_name = prefix + ".dist-info/METADATA"
            record_name = prefix + ".dist-info/RECORD"
            wheel_metadata_name = prefix + ".dist-info/WHEEL"
            if wheel_metadata_name in contents:
                failures.extend(_wheel_metadata(contents[wheel_metadata_name], path))
            if record_name in contents:
                import base64
                rows = list(csv.reader(io.StringIO(contents[record_name].decode())))
                if Counter(row[0] for row in rows) != Counter(contents.keys()):
                    failures.append("RECORD does not enumerate every member exactly once")
                for row in rows:
                    if len(row) != 3 or row[0] not in contents:
                        failures.append("malformed RECORD row")
                        continue
                    member, digest, size = row
                    if member == record_name:
                        if digest or size:
                            failures.append("RECORD self-row must be empty")
                    else:
                        value = contents[member]
                        computed = "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(value).digest()).rstrip(b"=").decode()
                        if digest != computed or size != str(len(value)):
                            failures.append(f"RECORD digest/size mismatch: {member}")
        else:
            missing = (expected | {"PKG-INFO"}) - contents.keys()
            metadata_name = "PKG-INFO"
        if missing:
            failures.append("incomplete artifact; missing " + ", ".join(sorted(missing)))
        if metadata_name in contents:
            failures.extend(_metadata(contents[metadata_name], root))
    return failures, {"member_count": len(inventory), "members": inventory}, list(contents)


def _check_wheel(path, manifest, root=None):
    return _inspect(path, manifest, root or Path(__file__).resolve().parents[1], True)


def _check_sdist(path, manifest, root=None):
    return _inspect(path, manifest, root or Path(__file__).resolve().parents[1], False)


def check_artefacts(root, manifest, artifacts):
    candidates = [artifacts] if artifacts.is_file() else sorted([*artifacts.rglob("*.whl"), *artifacts.rglob("*.tar.gz")])
    failures, evidence = [], {}
    if not candidates:
        failures.append("no distribution found")
    for candidate in candidates:
        try:
            errors, details, _ = _inspect(candidate, manifest, root, candidate.suffix == ".whl")
        except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile, UnicodeError) as error:
            errors, details = [f"invalid archive: {error}"], {}
        failures.extend(f"{candidate}: {error}" for error in errors)
        evidence[str(candidate)] = details
    return failures, evidence


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", default=".")
    parser.add_argument("--artifacts")
    parser.add_argument("--json-report")
    args = parser.parse_args(argv)
    root = Path(args.project_root).resolve()
    generator = _load_generator(root)
    manifest = _read_manifest(root)
    failures, evidence = check_source(root, manifest, generator)
    # Do not trust even a partially altered manifest as an archive allowlist.
    canonical = generator.build_manifest(root)
    if args.artifacts:
        errors, details = check_artefacts(root, canonical, Path(args.artifacts).resolve())
        failures.extend(errors)
        evidence["artifacts"] = details
    report = {"schema": "leo-dynamics/license-boundary-report/v1", "passed": not failures, "failures": failures, "evidence": evidence}
    if args.json_report:
        destination = Path(args.json_report)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n")
    for failure in failures:
        print(f"boundary gate FAILED: {failure}", file=sys.stderr)
    if not failures:
        print("licence boundary gate passed")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
