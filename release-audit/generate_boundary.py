#!/usr/bin/env python3
"""Generate the machine-readable licence and distribution boundary manifest.

The manifest records two independent facts about every git-tracked file of the
repository:

* the **licence** of its content, and
* whether it is a member of the **approved distribution** (the packaging
  allowlist).

They are independent on purpose. Every original file of the project - source,
research, documentation, tests and tooling - is MIT licensed, whether or not
it ships. Excluding a file from a wheel or a source distribution is a
packaging decision; it never changes the licence of the material.

No file of the repository carries a restricted classification: every tracked
file is the project's own material under the MIT grant. Third-party software
that is used as a reference oracle is separately installed and separately
licensed, and its terms are recorded in ``THIRD_PARTY_NOTICES.md``; running
such software does not place its licence on the numerical outputs this
project produces.

The manifest is the authoritative input of ``ci/verify_license_boundary.py``.

Design rules:

* The classification is *explicit*: every tracked file must match exactly one
  rule. A file that matches no rule is an error, not a silent default, so a
  new top-level area stops the release instead of being classified by
  accident.
* The distribution allowlist is a separate, explicit list of approved core
  sources. It is the minimal basic engine interface; the deferred
  atmospheric models, their external sources and the research material stay
  out of every distribution.
* Content markers identify material that must never appear inside a
  distributed member: the external reference implementations and data that
  the deferred models consume, and the deferred provider adapters themselves.

Usage:

    python3 release-audit/generate_boundary.py            # write the manifest
    python3 release-audit/generate_boundary.py --check     # verify it is current
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = REPO_ROOT / "release-audit" / "license-boundary.json"

# Licensing classes. These describe the content of a file, never whether it
# is redistributed. Every tracked file of this repository is the project's own
# material; no tracked file carries a restricted classification.
MIT = "mit"

# Ordered rules: the first match wins. Each rule is (kind, pattern, class)
# where kind is "exact" or "prefix".
RULES: list[tuple[str, str, str]] = [
    # --- the project's own material ---------------------------------------
    ("exact", "LICENSE", MIT),
    ("exact", "LICENSE_SCOPE.md", MIT),
    ("exact", "THIRD_PARTY_NOTICES.md", MIT),
    ("exact", "README.dist.md", MIT),
    ("exact", "README.md", MIT),
    ("exact", "CMakeLists.txt", MIT),
    ("exact", "pyproject.toml", MIT),
    ("exact", "RELEASE_LICENSE_AUDIT.md", MIT),
    ("exact", "audit-progress.md", MIT),
    ("exact", ".gitattributes", MIT),
    ("exact", ".gitignore", MIT),
    # numerical engine
    ("prefix", "src/", MIT),
    ("prefix", "include/", MIT),
    # Python interface
    ("prefix", "bindings/", MIT),
    ("prefix", "python/", MIT),
    # build and packaging
    ("prefix", "cmake/", MIT),
    # project documentation set
    ("prefix", "docs/", MIT),
    # research campaigns, optimisation experiments and datasets
    ("prefix", "benchmarks/", MIT),
    ("prefix", "scripts/", MIT),
    ("prefix", "plotting/", MIT),
    ("prefix", "examples/", MIT),
    # test suites and frozen research reference fixtures
    ("prefix", "tests/", MIT),
    # packaging, CI and release tooling
    ("prefix", "ci/", MIT),
    ("prefix", ".github/", MIT),
    ("prefix", "release-audit/", MIT),
    ("prefix", "audit-evidence/", MIT),
]

# The approved distribution allowlist: the minimal basic engine interface.
# This is packaging policy, not a licence scope. It mirrors the include list
# of ``[tool.scikit-build.sdist]`` in pyproject.toml and the wheel payload.
APPROVED_SOURCES: list[tuple[str, str]] = [
    ("exact", "LICENSE"),
    ("exact", "LICENSE_SCOPE.md"),
    ("exact", "THIRD_PARTY_NOTICES.md"),
    ("exact", "README.dist.md"),
    ("exact", "CMakeLists.txt"),
    ("exact", "pyproject.toml"),
    ("exact", "cmake/provision.cmake"),
    ("prefix", "bindings/"),
    ("prefix", "python/leo_dynamics/"),
    ("prefix", "src/kernel/"),
    ("prefix", "src/integration/"),
    ("prefix", "src/analysis/"),
    ("prefix", "include/leo/kernel/"),
    ("prefix", "include/leo/integration/"),
    ("prefix", "include/leo/analysis/"),
    ("exact", "include/leo/environment/ephemeris.hpp"),
    ("exact", "include/leo/environment/interpolated.hpp"),
    ("exact", "include/leo/environment/source.hpp"),
]

# Content that identifies the external reference implementations and data the
# deferred models consume. It is never vendored here and must never appear
# inside a distributed member.
DEFERRED_EXTERNAL_MARKERS: list[str] = [
    "Brodowski",
    "JB08DRVAUTO.f",
    "SpaceWeather-All",
    "naif0012.tls",
]

# Content that identifies the deferred provider adapters themselves. They are
# the author's own work and therefore MIT licensed, but they are not part of
# the approved distribution: the models they wrap are not redistributed.
DEFERRED_MODEL_MARKERS: list[str] = [
    "leo::atmosphere::",
    "leo_nrlmsise00_reference",
    "leo_jb2008_reference",
    "leo_jb2008_bridge",
    "SpiceEphemerisProvider",
]


def tracked_files(root: Path | None = None) -> list[str]:
    base = (root or REPO_ROOT).resolve()
    result = subprocess.run(
        ["git", "-C", str(base), "ls-files", "-z"],
        check=True,
        capture_output=True,
    )
    return [entry for entry in result.stdout.decode().split("\0") if entry]


def classify(path: str) -> str:
    for kind, pattern, licence_class in RULES:
        if kind == "exact" and path == pattern:
            return licence_class
        if kind == "prefix" and path.startswith(pattern):
            return licence_class
    raise SystemExit(
        f"boundary error: {path!r} matches no rule. Classify it explicitly; "
        "an unclassified file must never fall through to a default."
    )


def is_approved_source(path: str) -> bool:
    for kind, pattern in APPROVED_SOURCES:
        if kind == "exact" and path == pattern:
            return True
        if kind == "prefix" and path.startswith(pattern):
            return True
    return False


def build_manifest(root: Path | None = None) -> dict:
    files = {}
    approved = []
    for path in tracked_files(root):
        licence_class = classify(path)
        distributed = is_approved_source(path)
        files[path] = {
            "class": licence_class,
            "in_distribution": distributed,
        }
        if distributed:
            approved.append(path)
    return {
        "schema": "leo-dynamics/license-boundary/v2",
        "project": "leo-dynamics",
        "version": "0.1.0",
        "generator": "release-audit/generate_boundary.py",
        "classes": {
            MIT: {
                "licence": "MIT",
                "licence_file": "LICENSE",
                "description": (
                    "Original material of this project: numerical engine, "
                    "public headers, Python interface, research campaigns and "
                    "datasets, documentation set, test suites and frozen "
                    "reference fixtures, examples, plotting helpers and the "
                    "packaging, CI and release tooling. The MIT grant covers "
                    "all of it, whether or not a file is redistributed."
                ),
            },
        },
        "rules": [
            {"kind": kind, "pattern": pattern, "class": licence_class}
            for kind, pattern, licence_class in RULES
        ],
        "approved_sources": sorted(approved),
        "deferred_external_markers": sorted(DEFERRED_EXTERNAL_MARKERS),
        "deferred_model_markers": sorted(DEFERRED_MODEL_MARKERS),
        "files": dict(sorted(files.items())),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed manifest matches the current tree; do not write",
    )
    arguments = parser.parse_args(argv)

    manifest = build_manifest()
    serialised = json.dumps(manifest, indent=2, sort_keys=True) + "\n"

    if arguments.check:
        if not MANIFEST_PATH.is_file():
            print(f"boundary check failed: {MANIFEST_PATH} does not exist", file=sys.stderr)
            return 1
        current = MANIFEST_PATH.read_text(encoding="utf-8")
        if current != serialised:
            print(
                "boundary check failed: the committed manifest is stale; "
                "review the classification and regenerate it with "
                "python3 release-audit/generate_boundary.py",
                file=sys.stderr,
            )
            return 1
        print("boundary manifest is current")
        return 0

    MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST_PATH.write_text(serialised, encoding="utf-8")
    counts: dict[str, int] = {}
    for entry in manifest["files"].values():
        counts[entry["class"]] = counts.get(entry["class"], 0) + 1
    print(
        "wrote "
        + str(MANIFEST_PATH.relative_to(REPO_ROOT))
        + ": "
        + ", ".join(f"{key}={value}" for key, value in sorted(counts.items()))
        + f", approved_sources={len(manifest['approved_sources'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
