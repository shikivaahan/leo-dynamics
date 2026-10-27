#!/usr/bin/env python3
"""Generate the machine-readable licence boundary manifest.

The manifest classifies **every** git-tracked file of the repository into
exactly one licence class and records which files are allowed to be
redistributed in the public source distribution. It is the authoritative input
of ``ci/verify_license_boundary.py``.

Design rules:

* The classification is *explicit*: every tracked file must match exactly one
  rule. A file that matches no rule is an error, not a silent default. This
  is what makes the manifest fail closed on an unknown addition.
* The MIT core is deliberately minimal: the numerical engine, its public
  headers, the Python interface, the build machinery, the third-party notices
  and the user-facing guides.
* Everything else - high-fidelity atmosphere providers, research campaigns,
  optimisation experiments and datasets, the frozen research reference
  fixtures, qualification reports, CI and release tooling - is proprietary and
  is never redistributed.

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

MIT = "mit_core"
PROPRIETARY = "proprietary"
REPOSITORY = "repository_metadata"

# Ordered rules: the first match wins. Each rule is (kind, pattern, class)
# where kind is "exact" or "prefix".
RULES: list[tuple[str, str, str]] = [
    # --- the MIT core -----------------------------------------------------
    ("exact", "LICENSE", MIT),
    ("exact", "LICENSE_SCOPE.md", MIT),
    ("exact", "THIRD_PARTY_NOTICES.md", MIT),
    ("exact", "README.dist.md", MIT),
    ("exact", "CMakeLists.txt", MIT),
    ("exact", "pyproject.toml", MIT),
    ("exact", "cmake/provision.cmake", MIT),
    # numerical engine
    ("prefix", "src/kernel/", MIT),
    ("prefix", "src/integration/", MIT),
    ("prefix", "src/analysis/", MIT),
    ("prefix", "include/leo/kernel/", MIT),
    ("prefix", "include/leo/integration/", MIT),
    ("prefix", "include/leo/analysis/", MIT),
    # the CSPICE adapter is proprietary; the ephemeris interfaces are not
    ("exact", "include/leo/environment/spice.hpp", PROPRIETARY),
    ("prefix", "include/leo/environment/", MIT),
    # Python interface
    ("prefix", "bindings/", MIT),
    ("prefix", "python/leo_dynamics/", MIT),
    # The project's own documentation set is not distributed: the guides point
    # at the research campaigns, the frozen reference datasets and the
    # qualification reports, and `docs/stage3.md` records a third-party
    # (ImperialSpaceSociety) comparative analysis. The public distribution
    # documents itself with README.dist.md only.
    ("prefix", "docs/", PROPRIETARY),
    # --- everything else is proprietary -----------------------------------
    ("prefix", "src/atmosphere/", PROPRIETARY),
    ("prefix", "src/environment/", PROPRIETARY),
    ("prefix", "include/leo/atmosphere/", PROPRIETARY),
    ("prefix", "cmake/nrlmsise00.cmake", PROPRIETARY),
    ("prefix", "cmake/nrlmsise00-tests.cmake", PROPRIETARY),
    ("prefix", "cmake/jb2008.cmake", PROPRIETARY),
    ("prefix", "docs/", PROPRIETARY),
    ("prefix", "benchmarks/", PROPRIETARY),
    ("prefix", "scripts/", PROPRIETARY),
    ("prefix", "tests/", PROPRIETARY),
    ("prefix", "examples/", PROPRIETARY),
    ("prefix", "plotting/", PROPRIETARY),
    ("prefix", "ci/", PROPRIETARY),
    ("prefix", ".github/", PROPRIETARY),
    ("prefix", "release-audit/", PROPRIETARY),
    ("prefix", "audit-evidence/", PROPRIETARY),
    ("exact", "PROPRIETARY-NOTICE.md", PROPRIETARY),
    ("exact", "RELEASE_LICENSE_AUDIT.md", PROPRIETARY),
    ("exact", "audit-progress.md", PROPRIETARY),
    ("exact", "README.md", PROPRIETARY),
    # --- repository plumbing, not distributed ------------------------------
    ("exact", ".gitattributes", REPOSITORY),
    ("exact", ".gitignore", REPOSITORY),
]

# Every file of the MIT core is redistributed in the public source
# distribution. A file that must stay private is classified ``proprietary``
# above; there is no third state, so a member of the core can never be
# silently left out of the distribution without changing its class.
SDIST_MEMBERS_ARE_ALL_MIT_CORE = True

# Content markers that must never appear inside a redistributed member.
# They are chosen to identify proprietary *content* (external reference
# implementations, provider adapters and their attribution strings), not the
# optional build hooks the public CMake project keeps for the repository's own
# provider experiments.
PROPRIETARY_MARKERS: list[str] = [
    "nrlmsise-00.c",
    "nrlmsise-00_data.c",
    "nrlmsise-00.h",
    "Brodowski",
    "JB2008.f",
    "leo_nrlmsise00_reference",
    "leo_jb2008_reference",
    "leo_jb2008_bridge",
    "leo::atmosphere::",
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


def build_manifest(root: Path | None = None) -> dict:
    files = {}
    for path in tracked_files(root):
        licence_class = classify(path)
        files[path] = {
            "class": licence_class,
            "in_sdist": licence_class == MIT,
        }
    return {
        "schema": "leo-dynamics/license-boundary/v1",
        "project": "leo-dynamics",
        "version": "0.1.0",
        "generator": "release-audit/generate_boundary.py",
        "classes": {
            MIT: {
                "licence": "MIT",
                "licence_file": "LICENSE",
                "scope_file": "LICENSE_SCOPE.md",
                "description": (
                    "The numerical engine, its public headers, the Python "
                    "interface, the build and packaging machinery, the "
                    "third-party notices and the user-facing guides."
                ),
            },
            PROPRIETARY: {
                "licence": "proprietary",
                "licence_file": "PROPRIETARY-NOTICE.md",
                "description": (
                    "High-fidelity atmosphere providers, research campaigns, "
                    "optimisation experiments and datasets, research reference "
                    "fixtures, qualification reports and the packaging, CI and "
                    "release tooling. Never redistributed."
                ),
            },
            REPOSITORY: {
                "licence": "unpublished",
                "description": "Repository plumbing; not distributed.",
            },
        },
        "rules": [
            {"kind": kind, "pattern": pattern, "class": licence_class}
            for kind, pattern, licence_class in RULES
        ],
        "sdist_members": sorted(
            path for path, entry in files.items() if entry["in_sdist"]
        ),
        "proprietary_markers": sorted(PROPRIETARY_MARKERS),
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
        + f", sdist_members={len(manifest['sdist_members'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
