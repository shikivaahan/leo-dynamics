#!/usr/bin/env python3
"""Verify release artefacts before they are uploaded to an index.

The publishing job runs this over every artefact downloaded from the build
jobs. It answers one question: are these files the ones this exact commit was
supposed to produce?

Checks:

1. Every distribution file is covered by exactly one provenance record, and
   that record names this commit and this workflow run.
2. Every recorded SHA-256 still matches the file on disk.
3. The wheel set is exactly the declared support matrix: one wheel per
   CPython version per platform, named `leo_dynamics`, at the released
   version, with no gaps, no duplicates and no unexpected files.
4. Exactly one source distribution is present, carrying the project licence
   and the third-party notices, and carrying none of the build output,
   environment or bulk-evidence directories.
5. Every wheel carries the project licence and the third-party notices inside
   its `.dist-info/licenses` directory and declares a licence expression. A
   wheel that does not is not distributable.

Usage:

    python3 ci/verify_release_artifacts.py \\
        --artifacts artifacts \\
        --expected-commit "$GITHUB_SHA" \\
        --expected-run-id "$GITHUB_RUN_ID" \\
        --expected-version 0.1.0
"""

from __future__ import annotations

import argparse
import email.parser
import hashlib
import json
import re
import sys
import tarfile
import zipfile
from collections import Counter
from pathlib import Path

from packaging.utils import InvalidWheelFilename, parse_wheel_filename

DISTRIBUTION_NAME = "leo_dynamics"

# The conventional source-distribution filename: `{name}-{version}.tar.gz`.
SDIST_NAME = re.compile(r"^(?P<name>[^-]+)-(?P<version>[^-]+)\.tar\.gz$")

# CPython versions the project supports.
SUPPORTED_PYTHONS = ("cp310", "cp311", "cp312", "cp313")

# Platform key -> the tag fragments a wheel for that platform carries.
PLATFORMS = {
    "linux_x86_64": ("manylinux", "x86_64"),
    "win_amd64": ("win_amd64",),
    "macosx_arm64": ("macosx", "arm64"),
    "macosx_x86_64": ("macosx", "x86_64"),
}

LICENCE_FILE = "LICENSE"
NOTICES_FILE = "THIRD_PARTY_NOTICES.md"

# Nothing matching these may appear in the source distribution.
FORBIDDEN_SDIST_PREFIXES = (
    "build",
    "dist",
    "outputs",
    ".venv",
    ".pytest_cache",
    "docs/atmosphere/optimisation",
    "docs/atmosphere/profiling",
    "docs/atmosphere/evidence",
    "docs/multithreading/evidence",
)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def platform_key(tag: str) -> str | None:
    for key, fragments in PLATFORMS.items():
        if all(fragment in tag for fragment in fragments):
            return key
    return None


def wheel_metadata(archive: zipfile.ZipFile) -> dict[str, str]:
    names = [name for name in archive.namelist() if name.endswith(".dist-info/METADATA")]
    if not names:
        return {}
    message = email.parser.BytesParser().parsebytes(archive.read(names[0]))
    return {key: value for key, value in message.items()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--expected-run-id", required=True)
    parser.add_argument("--expected-version", required=True)
    arguments = parser.parse_args(argv)

    root = Path(arguments.artifacts).resolve()
    failures: list[str] = []
    passes: list[str] = []

    def check(condition: bool, subject: str, expectation: str) -> None:
        """Record one expectation about `subject`."""
        if condition:
            passes.append(f"{subject}: {expectation}")
        else:
            failures.append(f"{subject}: expected {expectation}")

    if not root.is_dir():
        print(f"artifact verification failed: {root} is not a directory", file=sys.stderr)
        return 1

    # 1. Provenance coverage: every distribution file, exactly once.
    records = sorted(root.rglob("provenance.json"))
    check(bool(records), "provenance records", "at least one record was found")
    covered: dict[Path, str] = {}
    for record_path in records:
        record = json.loads(record_path.read_text(encoding="utf-8"))
        check(
            record.get("commit") == arguments.expected_commit,
            record_path.parent.name,
            f"to be built from commit {arguments.expected_commit}",
        )
        check(
            str(record.get("workflow_run_id")) == arguments.expected_run_id,
            record_path.parent.name,
            f"to be built by workflow run {arguments.expected_run_id}",
        )
        for name, digest in (record.get("files") or {}).items():
            # The name is relative to the directory the record was written in,
            # not to the artefact root: the two differ once the artefacts are
            # unpacked into one directory.
            path = (record_path.parent / name).resolve()
            check(
                path not in covered,
                name,
                "to be covered by exactly one provenance record",
            )
            covered[path] = digest

    distributions = sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.name.endswith((".whl", ".tar.gz"))
    )
    for path in distributions:
        check(path.resolve() in covered, path.name, "to have a provenance record")
    for path, digest in covered.items():
        # A recorded file that is not there is a failure, not something to skip:
        # the provenance says this artefact was built, so its absence means the
        # set is incomplete or was tampered with.
        if not path.is_file():
            check(False, path.name, "to be present as its provenance record states")
            continue
        check(
            sha256_of(path) == digest,
            path.name,
            "to match its recorded SHA-256",
        )

    # 2. The wheel matrix.
    wheels = [path for path in distributions if path.suffix == ".whl"]
    slots: Counter[tuple[str, str]] = Counter()
    for wheel in wheels:
        try:
            name, version, _build, tags = parse_wheel_filename(wheel.name)
        except InvalidWheelFilename:
            failures.append(f"{wheel.name} is not a valid wheel filename")
            continue
        check(
            str(name).replace("_", "-") == DISTRIBUTION_NAME.replace("_", "-"),
            wheel.name,
            f"to be the {DISTRIBUTION_NAME} distribution",
        )
        check(
            str(version) == arguments.expected_version,
            wheel.name,
            f"to be version {arguments.expected_version}",
        )
        interpreters = {tag.interpreter for tag in tags}
        abis = {tag.abi for tag in tags}
        check(
            len(interpreters) == 1 and len(abis) == 1,
            wheel.name,
            "to carry one interpreter and one ABI",
        )
        python_tag = next(iter(interpreters), "")
        check(
            python_tag in SUPPORTED_PYTHONS,
            wheel.name,
            f"to target a supported CPython (got {python_tag})",
        )
        check(
            abis == {python_tag},
            wheel.name,
            f"to use the version-specific ABI {python_tag}",
        )
        keys = {
            platform_key(tag.platform)
            for tag in tags
            if platform_key(tag.platform) is not None
        }
        check(
            len(keys) == 1,
            wheel.name,
            "to target exactly one supported platform",
        )
        if keys:
            slots[(next(iter(keys)), python_tag)] += 1

        with zipfile.ZipFile(wheel) as archive:
            licences = {
                Path(name).name
                for name in archive.namelist()
                if ".dist-info/licenses/" in name
            }
            check(
                LICENCE_FILE in licences,
                wheel.name,
                f"to carry {LICENCE_FILE}",
            )
            check(
                NOTICES_FILE in licences,
                wheel.name,
                f"to carry {NOTICES_FILE}",
            )
            metadata = wheel_metadata(archive)
            check(
                bool(metadata.get("License-Expression")),
                wheel.name,
                "to declare a licence expression",
            )
            check(
                metadata.get("Version") == arguments.expected_version,
                wheel.name,
                f"to declare version {arguments.expected_version} in its metadata",
            )

    expected_slots = {
        (platform, python) for platform in PLATFORMS for python in SUPPORTED_PYTHONS
    }
    for slot in sorted(expected_slots):
        count = slots.get(slot, 0)
        check(
            count == 1,
            f"wheel slot {slot[0]} {slot[1]}",
            f"to hold exactly one wheel (found {count})",
        )
    for slot, count in sorted(slots.items()):
        if slot not in expected_slots:
            check(False, f"wheel slot {slot}", "to be a supported slot")
    check(
        len(wheels) == len(expected_slots),
        "the wheel set",
        f"to hold exactly {len(expected_slots)} wheels (found {len(wheels)})",
    )

    # 3. The source distribution.
    sdists = [path for path in distributions if path.name.endswith(".tar.gz")]
    check(
        len(sdists) == 1,
        "the source distributions",
        f"to hold exactly one file (found {len(sdists)})",
    )
    for sdist in sdists:
        # The filename itself must name the distribution and the released
        # version: `{distribution}-{version}.tar.gz`.
        match = SDIST_NAME.match(sdist.name)
        if match is None:
            check(
                False,
                sdist.name,
                f"to be named {DISTRIBUTION_NAME}-<version>.tar.gz",
            )
        else:
            check(
                match.group("name").replace("_", "-")
                == DISTRIBUTION_NAME.replace("_", "-"),
                sdist.name,
                f"to be the {DISTRIBUTION_NAME} distribution",
            )
            check(
                match.group("version") == arguments.expected_version,
                sdist.name,
                f"to be version {arguments.expected_version}",
            )
        with tarfile.open(sdist) as archive:
            members = archive.getnames()
            # The metadata inside the archive must agree with the filename:
            # `PKG-INFO` at the root of the single top-level directory.
            pkg_info = next(
                (
                    name
                    for name in members
                    if name.endswith("/PKG-INFO") and name.count("/") == 1
                ),
                None,
            )
            if pkg_info is None:
                check(False, sdist.name, "to carry a PKG-INFO at its root")
            else:
                handle = archive.extractfile(pkg_info)
                metadata = email.parser.BytesParser().parsebytes(handle.read())
                check(
                    (metadata.get("Name") or "").replace("_", "-")
                    == DISTRIBUTION_NAME.replace("_", "-"),
                    sdist.name,
                    f"to declare Name {DISTRIBUTION_NAME} in PKG-INFO",
                )
                check(
                    metadata.get("Version") == arguments.expected_version,
                    sdist.name,
                    f"to declare Version {arguments.expected_version} in PKG-INFO",
                )
        roots = {name.split("/", 1)[-1] for name in members}
        check(
            LICENCE_FILE in roots,
            sdist.name,
            f"to carry {LICENCE_FILE}",
        )
        check(
            NOTICES_FILE in roots,
            sdist.name,
            f"to carry {NOTICES_FILE}",
        )
        for name in members:
            relative = name.split("/", 1)[-1] if "/" in name else name
            for prefix in FORBIDDEN_SDIST_PREFIXES:
                if relative.startswith(prefix):
                    check(
                        False,
                        sdist.name,
                        f"to carry no {prefix} content (found {relative})",
                    )

    for line in passes:
        print(f"artifact verification: ok: {line}")
    if failures:
        print("", file=sys.stderr)
        print("artifact verification failed: these artefacts must not be uploaded.", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        f"artifact verification: every artefact is the one commit "
        f"{arguments.expected_commit} was supposed to produce; the set may be uploaded."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
