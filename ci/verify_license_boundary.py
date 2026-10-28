#!/usr/bin/env python3
"""Fail closed on boundary drift, unsafe archives, changed public bytes or leaks.

Run from the checkout before any artifact upload. The canonical manifest is
regenerated from reviewed rules; built members are compared to the approved
checkout bytes. Binary scans supplement, but cannot prove, compiled provenance.

The contract has two independent halves, and this gate enforces both:

* the **licence** of every tracked file, and
* the **approved distribution** the wheel and the source distribution carry.

A file outside the approved distribution is not redistributed - a packaging
decision that never changes the licence of the material. Material that must
never be packaged at all (the deferred external reference implementations and
data, and the deferred provider adapters) is refused by content markers.
"""

from __future__ import annotations

import argparse
import ast
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
LICENCE_FILES = ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md")
SCAN_CHUNK = 1 << 20

# The wheel repair tools cibuildwheel 4.3.1 installs: auditwheel on Linux,
# delocate on macOS and delvewheel on Windows. cibuildwheel pins them in its
# own per-interpreter constraint files, so the pinned cibuildwheel release
# fixes their versions:
#   cibuildwheel/resources/constraints.in -> delocate, delvewheel
DELOCATE_VERSION = "0.13.0"
DELVEWHEEL_VERSION = "1.13.2"

# delocate records itself as a second generator in the WHEEL file of every
# wheel it repairs.
DELOCATE_GENERATOR = f"delocate {DELOCATE_VERSION}"

# The patch delvewheel inserts into the package __init__ so that the vendored
# runtime is found at import time. The text is fixed by the tool version; the
# rest of the module must still be the approved source, byte for byte. The
# text is the output of delvewheel (MIT), which THIRD_PARTY_NOTICES.md
# records among the build tooling; nothing of the tool is shipped in it.
DELVEWHEEL_PATCH = (
    "# start delvewheel patch\n"
    "def _delvewheel_patch_{version}():\n"
    "    import os\n"
    "    if os.path.isdir(libs_dir := os.path.abspath(os.path.join("
    "os.path.dirname(__file__), os.pardir, 'leo_dynamics.libs'))):\n"
    "        os.add_dll_directory(libs_dir)\n"
    "\n"
    "\n"
    "_delvewheel_patch_{version}()\n"
    "del _delvewheel_patch_{version}\n"
    "# end delvewheel patch\n"
).format(version=DELVEWHEEL_VERSION.replace(".", "_"))

# The runtime delvewheel vendors on Windows: the Microsoft Visual C++ runtime,
# which the tool names after the first sixteen bytes of its SHA-256 digest.
# The approved digest is pinned here; a different runtime is a different
# artefact and has to be reviewed before it is released.
APPROVED_RUNTIME_LIBRARIES = {
    "leo_dynamics.libs/msvcp140-a4c2229bdc2a2a630acdc095b4d86008.dll": (
        "a4c2229bdc2a2a630acdc095b4d86008e5c3e3bc7773174354f3da4f5beb9cde"
    ),
}

# The repair log delvewheel writes next to the metadata: the tool version and,
# when diagnostics are on, the command line it ran with.
DELVEWHEEL_LOG_LIMIT = 4096


def _load_generator(root):
    spec = importlib.util.spec_from_file_location("leo_boundary_generator", root / GENERATOR_RELATIVE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _read_manifest(root):
    return json.loads((root / MANIFEST_RELATIVE).read_text())


def _markers(manifest):
    return list(manifest["deferred_external_markers"]) + list(manifest["deferred_model_markers"])


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


def _normalise_newlines(payload):
    """Return `payload` with CRLF and CR line endings rewritten as LF."""
    return payload.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def _delvewheel_patched(source):
    """Return the approved source with the delvewheel patch inserted.

    delvewheel inserts the patch after the last ``from __future__ import``
    statement, separated from it by three line endings, and strips the
    whitespace around the insertion point. This reproduces that exactly, so
    the gate can accept the repaired module and nothing else.
    """
    text = source.decode("utf-8")
    anchor = 0
    for child in ast.iter_child_nodes(ast.parse(text)):
        if isinstance(child, ast.ImportFrom) and child.module == "__future__":
            anchor = child.end_lineno
    if not anchor:
        raise ValueError("the approved source has no __future__ import to anchor the patch")
    lines = text.splitlines(keepends=True)
    head = "".join(lines[:anchor]).rstrip()
    tail = "".join(lines[anchor:]).lstrip()
    return (head + "\n\n\n" + DELVEWHEEL_PATCH + "\n" + tail).encode("utf-8")


def _delvewheel_log(payload):
    """Check the repair log delvewheel writes next to the metadata."""
    failures = []
    if len(payload) > DELVEWHEEL_LOG_LIMIT:
        failures.append("the delvewheel repair log is unexpectedly large")
        return failures
    lines = payload.decode("utf-8", "replace").splitlines()
    if not lines or lines[0] != f"Version: {DELVEWHEEL_VERSION}":
        failures.append("the delvewheel repair log does not declare the pinned delvewheel version")
    for line in lines[1:]:
        if not line.startswith("Arguments: "):
            failures.append("the delvewheel repair log carries an unexpected entry")
    return failures


def check_source(root, manifest, generator):
    canonical = generator.build_manifest(root)
    failures = []
    if canonical != manifest:
        failures.append("the complete licence boundary manifest is stale or altered")
    markers = _markers(canonical)
    # Only the approved distribution is scanned for excluded content: outside
    # it, the research material legitimately discusses the deferred models and
    # their external sources.
    for path in canonical["approved_sources"]:
        source = root / path
        if source.is_symlink() or not source.is_file():
            failures.append(f"approved source is not a regular file: {path}")
            continue
        with source.open("rb") as handle:
            hits = _scan_stream(markers, handle)
        if hits:
            failures.append(f"{path}: excluded content markers {hits}")
    # The approved distribution carries the project's own material only.
    for path in canonical["approved_sources"]:
        entry = canonical["files"].get(path)
        if entry is None:
            failures.append(f"{path}: approved source is not a tracked file")
        elif entry["class"] != generator.MIT:
            failures.append(f"{path}: approved source is not the project's own material")
    project = tomllib.loads((root / "pyproject.toml").read_text())["project"]
    if project.get("license") != "MIT":
        failures.append("project license must be MIT")
    if project.get("readme", {}).get("file") != "README.dist.md":
        failures.append("distribution readme must be README.dist.md")
    if set(project.get("license-files", [])) != set(LICENCE_FILES):
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
    description = message.get_payload(decode=True)
    # The approved readme is read as UTF-8 on every platform: the default
    # encoding of a Windows host is not UTF-8, and the readme carries
    # characters outside ASCII. Line endings are compared as equivalent, so a
    # CRLF checkout cannot fail the comparison either.
    approved = _normalise_newlines((root / "README.dist.md").read_bytes()).decode("utf-8")
    if isinstance(description, bytes):
        description = _normalise_newlines(description).decode("utf-8", "replace")
    if not isinstance(description, str) or description.rstrip("\n") != approved.rstrip("\n"):
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
        actual = list(map(str, message.get_all(key, [])))
        if key == "Generator":
            # A repaired macOS wheel carries a second generator line, written
            # by delocate. No other generator is accepted, and only a macOS
            # wheel may carry that line.
            allowed = [[values[0]]]
            if "macosx" in platform:
                allowed.append([values[0], DELOCATE_GENERATOR])
            if actual not in allowed:
                failures.append("WHEEL Generator differs from approved build/filename")
            continue
        if Counter(actual) != Counter(values):
            failures.append(f"WHEEL {key} differs from approved build/filename")
    if message.get_payload(decode=True).strip():
        failures.append("WHEEL must have no body")
    return failures


def _inspect(path, manifest, root, wheel):
    failures, inventory, contents = [], [], {}
    prefix = "leo_dynamics-0.1.0"
    expected = set(manifest["approved_sources"])
    markers = _markers(manifest)
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
            hits = _scan_stream(markers, io.BytesIO(payload))
            if hits:
                failures.append(f"{relative}: excluded content markers {hits}")
            mapped = None
            if wheel:
                if relative in WHEEL_PACKAGE_FILES:
                    mapped = "python/" + relative
                elif WHEEL_MODULE_PATTERN.fullmatch(relative):
                    if not payload.startswith((b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe")):
                        failures.append(f"extension is not a recognised native binary: {relative}")
                elif relative.startswith(prefix + ".dist-info/licenses/"):
                    licence = relative.removeprefix(prefix + ".dist-info/licenses/")
                    if licence not in LICENCE_FILES:
                        failures.append(f"unexpected licence member {relative}")
                    else:
                        mapped = licence
                elif relative in APPROVED_RUNTIME_LIBRARIES:
                    digest = hashlib.sha256(payload).hexdigest()
                    if digest != APPROVED_RUNTIME_LIBRARIES[relative]:
                        failures.append(f"{relative}: runtime library is not the approved build")
                    if not relative.endswith(f"-{digest[:32]}.dll"):
                        failures.append(f"{relative}: runtime library is not named after its own digest")
                    if not payload.startswith(b"MZ"):
                        failures.append(f"{relative}: runtime library is not a Windows image")
                elif relative == prefix + ".dist-info/DELVEWHEEL":
                    failures.extend(_delvewheel_log(payload))
                elif relative not in {prefix + ".dist-info/" + child for child in ("METADATA", "WHEEL", "RECORD")}:
                    failures.append(f"unexpected member {relative}")
            elif relative in expected:
                mapped = relative
            elif relative != "PKG-INFO":
                failures.append(f"{relative}: not an approved distribution member")
            if mapped is not None:
                original = root / mapped
                if original.is_symlink() or not original.is_file():
                    failures.append(f"{relative}: approved public source is missing: {mapped}")
                else:
                    approved = original.read_bytes()
                    if relative == "leo_dynamics/__init__.py":
                        # The repaired Windows wheel carries the module
                        # delvewheel patched; everything else in it must still
                        # be the approved source.
                        try:
                            patched = _delvewheel_patched(approved)
                        except (SyntaxError, UnicodeDecodeError, ValueError) as error:
                            failures.append(f"{relative}: the approved source cannot be patched: {error}")
                            patched = None
                        if payload != approved and not (
                            patched is not None
                            and _normalise_newlines(payload) == _normalise_newlines(patched)
                        ):
                            failures.append(f"{relative}: bytes differ from approved public source {mapped}")
                    elif payload != approved:
                        failures.append(f"{relative}: bytes differ from approved public source {mapped}")
        if wheel:
            required = WHEEL_PACKAGE_FILES | {prefix + ".dist-info/" + child for child in ("METADATA", "WHEEL", "RECORD")} | {prefix + ".dist-info/licenses/" + name for name in LICENCE_FILES}
            modules = [n for n in contents if WHEEL_MODULE_PATTERN.fullmatch(n)]
            if len(modules) != 1:
                failures.append("wheel must carry exactly one _core native module")
            missing = required - contents.keys()
            metadata_name = prefix + ".dist-info/METADATA"
            record_name = prefix + ".dist-info/RECORD"
            wheel_metadata_name = prefix + ".dist-info/WHEEL"
            if wheel_metadata_name in contents:
                failures.extend(_wheel_metadata(contents[wheel_metadata_name], path))
            # A vendored runtime and the repair log that describes it belong
            # together: one without the other is not a wheel this gate knows.
            vendored = [name for name in contents if name in APPROVED_RUNTIME_LIBRARIES]
            if bool(vendored) != (prefix + ".dist-info/DELVEWHEEL" in contents):
                failures.append("the vendored runtime and the delvewheel repair log must both be present")
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
    report = {"schema": "leo-dynamics/license-boundary-report/v2", "passed": not failures, "failures": failures, "evidence": evidence}
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
