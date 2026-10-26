#!/usr/bin/env python3
"""Record which commit a set of release artefacts was built from.

The publishing job downloads artefacts produced by other jobs and needs to
prove that they are the ones this run was supposed to produce. This script
writes that proof next to the artefacts: the commit, the workflow run id, and
the SHA-256 of every distribution file. File names are relative to the
directory that holds them, so the record stays valid wherever the artefact
directory is later unpacked.

Usage:

    python3 ci/write_release_provenance.py \\
        --output wheelhouse/provenance.json \\
        --commit "$GITHUB_SHA" \\
        --workflow-run-id "$GITHUB_RUN_ID" \\
        --directory wheelhouse
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

DISTRIBUTION_SUFFIXES = (".whl", ".tar.gz")


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--workflow-run-id", required=True)
    parser.add_argument("--directory", required=True)
    arguments = parser.parse_args()

    directory = Path(arguments.directory)
    files = {
        str(path.relative_to(directory)): sha256_of(path)
        for path in sorted(directory.rglob("*"))
        if path.is_file() and path.name.endswith(DISTRIBUTION_SUFFIXES)
    }
    if not files:
        raise SystemExit(f"no distribution files found under {directory}")

    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(
            {
                "commit": arguments.commit,
                "workflow_run_id": arguments.workflow_run_id,
                "files": files,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )
    print(f"recorded {len(files)} distribution file(s) in {output}")
    for name, digest in files.items():
        print(f"  {digest}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
