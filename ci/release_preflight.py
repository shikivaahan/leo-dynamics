#!/usr/bin/env python3
"""Release preflight: refuse to publish until the project licence is settled.

The TestPyPI upload is a real redistribution event, so this gate runs before
any distribution is built. It checks the mechanical facts a licence decision
produces; it does not and cannot judge whether the grant is legally
sufficient.

Checks:

1. A `LICENSE` file exists at the repository root and is not empty.
2. `pyproject.toml` declares the project licence as a concrete PEP 639 SPDX
   string, for example `license = "MIT"`.
3. `project.license-files` lists both `LICENSE` and `THIRD_PARTY_NOTICES.md`,
   so both documents travel inside the wheel and the source distribution.
4. The version to publish equals the version in `pyproject.toml`.

Usage:

    python3 ci/release_preflight.py --project-root . --expected-version 0.1.0

Exit status is 0 when every check passes and 1 otherwise.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tomllib
from pathlib import Path

LICENCE_FILE = "LICENSE"
NOTICES_FILE = "THIRD_PARTY_NOTICES.md"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", default=".")
    parser.add_argument("--expected-version", required=True)
    arguments = parser.parse_args(argv)

    root = Path(arguments.project_root).resolve()
    pyproject = root / "pyproject.toml"
    if not pyproject.is_file():
        print(f"release preflight failed: {pyproject} does not exist", file=sys.stderr)
        return 1

    with pyproject.open("rb") as handle:
        project = tomllib.load(handle).get("project", {})

    failures: list[str] = []

    licence = root / LICENCE_FILE
    if not licence.is_file():
        failures.append(
            f"no {LICENCE_FILE} file at the repository root; the copyright "
            "holder must add one (MIT is the recommended choice) and commit it"
        )
    elif not licence.read_text(encoding="utf-8", errors="replace").strip():
        failures.append(f"{LICENCE_FILE} is empty")

    expression = project.get("license")
    if not isinstance(expression, str) or not expression.strip():
        failures.append(
            'pyproject.toml does not declare the project licence; add '
            'license = "MIT" (or the SPDX expression the owner chooses) to the '
            "[project] table"
        )

    licence_files = project.get("license-files")
    if not isinstance(licence_files, list):
        licence_files = []
    for required in (LICENCE_FILE, NOTICES_FILE):
        if required not in licence_files:
            failures.append(
                f"pyproject.toml project.license-files does not list "
                f"{required}; both documents must travel with every "
                "distribution"
            )

    if project.get("version") != arguments.expected_version:
        failures.append(
            f"pyproject.toml declares version {project.get('version')!r}, not "
            f"{arguments.expected_version!r}"
        )

    # The boundary gate proves that the tree still matches the boundary
    # manifest, that no approved distribution source carries deferred-model
    # content, and that the packaging metadata declares the MIT grant. It
    # fails closed, so an unclassified or newly added file stops the release
    # here rather than after a distribution has been built.
    boundary = subprocess.run(
        [
            sys.executable,
            str(root / "ci" / "verify_license_boundary.py"),
            "--project-root",
            str(root),
        ],
        capture_output=True,
        text=True,
    )
    if boundary.returncode != 0:
        failures.append(
            "the licence boundary gate failed; the tree does not match "
            "release-audit/license-boundary.json:\n"
            + (boundary.stderr or boundary.stdout).strip()
        )

    if failures:
        print(
            "release preflight failed: this release cannot be published yet.",
            file=sys.stderr,
        )
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "release preflight passed: mechanical licence, boundary and version "
        "checks passed; this is not publication approval or proof of legal title."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
