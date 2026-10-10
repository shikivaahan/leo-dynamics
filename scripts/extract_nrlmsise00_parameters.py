#!/usr/bin/env python3
"""Extract the fixed-coefficient inventory of the pinned NRLMSISE-00 data file.

The NRLMSISE-00 coefficients are fixed published constants that live in the upstream
``nrlmsise-00_data.c``. They are not vendored into this repository, so this tool
reports their shape and provenance without reproducing their values: symbol, nested
declared shape, explicit versus implied-zero initialiser counts, source line span and
the SHA-256 of the exact source block that contains them.

Usage:
    scripts/extract_nrlmsise00_parameters.py /path/to/nrlmsise-00_data.c [--check]

``--check`` additionally verifies the file against the pinned SHA-256 and exits
non-zero on mismatch. Output is JSON on stdout.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys

PINNED_DATA_SHA256 = "d0b3022f3c3e7ffdf703cc0e0d02339dfee01c0bf1520b29c68d5f4afd8784d5"

_DECL = re.compile(r"^double\s+([A-Za-z_][A-Za-z_0-9]*)((?:\[[^\]]*\])*)\s*=\s*\{")
_NUMBER = re.compile(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[Ee][+-]?\d+)?")


def strip_comments(text: str) -> str:
    """Blank out C comments while preserving every byte offset and line break."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if out[k] != "\n":
                    out[k] = " "
            i = end
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            for k in range(i, end):
                out[k] = " "
            i = end
        else:
            i += 1
    return "".join(out)


def count_explicit(body: str, depth: int) -> int:
    """Count explicit numeric initialisers at brace depth ``depth`` inside ``body``.

    ``body`` is the text between the outermost braces of the declaration. Trailing
    commas are not values, so only complete numeric tokens are counted.
    """
    level = 0
    count = 0
    token: list[str] = []
    for ch in body:
        if ch == "{":
            level += 1
            if token:
                count += 1 if level == depth and _NUMBER.fullmatch("".join(token)) else 0
                token.clear()
            continue
        if ch == "}":
            if token:
                count += 1 if level == depth and _NUMBER.fullmatch("".join(token)) else 0
                token.clear()
            level -= 1
            continue
        if ch in " \t\r\n,":
            if token:
                count += 1 if level == depth and _NUMBER.fullmatch("".join(token)) else 0
                token.clear()
            continue
        token.append(ch)
    if token:
        count += 1 if level == depth and _NUMBER.fullmatch("".join(token)) else 0
    return count


def row_counts(body: str, rows: int) -> list[int]:
    """Explicit initialiser count for each top-level row of a nested array."""
    counts: list[int] = []
    level = 0
    start = 0
    segments: list[str] = []
    for idx, ch in enumerate(body):
        if ch == "{":
            if level == 0:
                start = idx + 1
            level += 1
        elif ch == "}":
            level -= 1
            if level == 1:
                segments.append(body[start:idx])
            elif level == 0:
                segments.append(body[start:idx])
    if len(segments) != rows:
        return []
    return [count_explicit(segment, 0) for segment in segments]


def extract(path: str) -> dict:
    raw = open(path, "rb").read()
    file_sha = hashlib.sha256(raw).hexdigest()
    text = raw.decode("utf-8", errors="replace")
    clean = strip_comments(text)
    lines = clean.splitlines()

    arrays = []
    i = 0
    while i < len(lines):
        match = _DECL.match(lines[i])
        if not match:
            i += 1
            continue
        symbol, dims_raw = match.group(1), match.group(2)
        dims = [int(d) for d in re.findall(r"\[(\d+)\]", dims_raw)]
        # Locate the matching closing brace of the initialiser list.
        depth = 0
        start_line = i
        end_line = i
        started = False
        for j in range(i, len(lines)):
            depth += lines[j].count("{") - lines[j].count("}")
            started = True
            end_line = j
            if started and depth == 0:
                break
        total = 1
        for d in dims:
            total *= d

        body = "\n".join(lines[start_line:end_line + 1])
        body = body[body.index("{") + 1:]
        if body.rstrip().endswith("}"):
            body = body.rstrip()[:-1]

        if len(dims) == 1:
            explicit = count_explicit(body, 0)
            per_row = None
        else:
            per_row = row_counts(body, dims[0])
            explicit = sum(per_row) if per_row else count_explicit(body, 1)

        block = "\n".join(text.splitlines()[start_line:end_line + 1]) + "\n"
        arrays.append({
            "symbol": symbol,
            "declared_shape": dims,
            "slots": total,
            "explicit_initializers": explicit,
            "implied_zero_initializers": total - explicit,
            "fully_initialized": explicit == total,
            "explicit_per_row": per_row,
            "source_file": path.split("/")[-1],
            "first_line": start_line + 1,
            "last_line": end_line + 1,
            "block_sha256": hashlib.sha256(block.encode()).hexdigest(),
        })
        i = end_line + 1

    return {
        "schema": "leo.atmosphere.nrlmsise00.coefficients/1",
        "source_file": path.split("/")[-1],
        "source_sha256": file_sha,
        "pinned_source_sha256": PINNED_DATA_SHA256,
        "pinned_source_sha256_matches": file_sha == PINNED_DATA_SHA256,
        "counting_rules": [
            "C comments are removed before counting so that numbers inside comments are never counted.",
            "Only complete numeric tokens at the row level are counted; trailing commas are not values.",
            "explicit_initializers is the number of values written in the source.",
            "implied_zero_initializers is the C-initialisation remainder of the declared slots; those entries are zero by language semantics and are part of the published behaviour.",
            "block_sha256 hashes the exact source text of the declaration block, comments included, so any upstream drift is detectable.",
        ],
        "value_policy": "Values are intentionally not reproduced. The pinned source file, verified by source_sha256, is the authoritative copy.",
        "arrays": arrays,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_file", help="path to the pinned nrlmsise-00_data.c")
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero unless the file matches the pinned SHA-256")
    args = parser.parse_args()

    report = extract(args.data_file)
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 1 if args.check and not report["pinned_source_sha256_matches"] else 0


if __name__ == "__main__":
    raise SystemExit(main())