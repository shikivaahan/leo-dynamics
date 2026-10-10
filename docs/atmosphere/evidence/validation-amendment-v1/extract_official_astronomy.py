#!/usr/bin/env python3
"""Extract the official SUNPOS and THETA subroutines, byte-identically.

The reference driver JB08DRVAUTO.f owns both the official solar-position routine
and the official sidereal routine, but it cannot simply be linked alongside a
probe main: it carries its own MAIN, and linking it drags in DTCVAL and SOLFSMY,
which in turn need files this investigation never opens.

Rather than satisfy those unresolved symbols with dummy routines - which would
be a project-authored substitute sitting in a link that reads as official code -
this slices the two subroutines out of the driver text verbatim and writes them
to a scratch file.  The extracted fragment is byte-identical to the driver's own
lines, and its SHA-256 is recorded so the extraction can be checked.

The driver itself is taken from the hash-pinned distribution: either an already
extracted copy, or the pinned archive, which is verified before use.

Usage:
    extract_official_astronomy.py --deps DIR --out FILE [--from-zip]

Writes the scratch Fortran to --out and prints a JSON provenance record.
"""

import argparse
import hashlib
import io
import json
import os
import re
import sys
import zipfile

# SHA-256 of JB08DRVAUTO.f as pinned in docs/atmosphere/evidence.
DRIVER_SHA256 = "061516b02b8af490b49f5745bddc44f53fd9b0ab738565753f9230e8ea00467f"

DRIVER_MEMBER = "JB2008 Code/JB08DRVAUTO.f"


def locate_driver(deps, from_zip):
    """Return (path, bytes) for a hash-verified copy of the official driver."""
    if not from_zip:
        for candidate in (
            os.path.join(deps, "JB08DRVAUTO.f"),
            os.path.join(deps, "JB2008 Code", "JB08DRVAUTO.f"),
        ):
            if os.path.isfile(candidate):
                with open(candidate, "rb") as handle:
                    return candidate, handle.read()
        archive = os.path.join(deps, "jb2008.zip")
        if os.path.isfile(archive):
            from_zip = True
        else:
            raise SystemExit(
                "official driver not found under %s; pass --from-zip to read it "
                "from jb2008.zip" % deps
            )
    if from_zip:
        archive = os.path.join(deps, "jb2008.zip")
        if not os.path.isfile(archive):
            raise SystemExit("missing pinned archive: %s" % archive)
        with zipfile.ZipFile(archive) as zf:
            raw = zf.read(DRIVER_MEMBER)
        return archive + "::" + DRIVER_MEMBER, raw
    raise SystemExit("unreachable")


def slice_subroutine(text, start_pattern, end_pattern, name):
    """Return the exact source lines of one subroutine, verbatim."""
    lines = text.split("\n")
    start = end = None
    for i, line in enumerate(lines):
        if start is None and re.search(start_pattern, line):
            start = i
            continue
        if start is not None and re.search(end_pattern, line):
            end = i
            break
    if start is None:
        raise SystemExit("could not find the start of %s" % name)
    if end is None:
        raise SystemExit("could not find the end of %s" % name)
    return "\n".join(lines[start:end + 1]) + "\n", start + 1, end + 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--deps", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--from-zip", action="store_true")
    args = ap.parse_args()

    origin, raw = locate_driver(args.deps, args.from_zip)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != DRIVER_SHA256:
        raise SystemExit(
            "JB08DRVAUTO.f SHA-256 mismatch: expected %s got %s; refusing to "
            "extract subroutines from an unpinned source" % (DRIVER_SHA256, digest)
        )

    text = raw.decode("latin-1")
    sunpos, s1, s2 = slice_subroutine(
        text, r"^\s*SUBROUTINE\s+SUNPOS", r"^\s*END\s+SUBROUTINE\s+SUNPOS", "SUNPOS"
    )
    theta, t1, t2 = slice_subroutine(
        text, r"^\s*REAL\*8\s+FUNCTION\s+THETA", r"^\s*END\s+FUNCTION\s+THETA", "THETA"
    )

    # The two subroutines are not adjacent in the driver, so the concatenated
    # fragment is not itself a substring.  Each slice must be byte-identical to
    # the driver on its own; that is the check that matters.
    for name, block in (("SUNPOS", sunpos), ("THETA", theta)):
        if block not in text:
            raise SystemExit("extracted %s is not byte-identical to the driver" % name)

    fragment = sunpos + theta
    with open(args.out, "w", newline="") as handle:
        handle.write(fragment)

    print(json.dumps({
        "driver_origin": origin,
        "driver_sha256": digest,
        "driver_sha256_matches_pin": True,
        "sunpos_lines": [s1, s2],
        "theta_lines": [t1, t2],
        "fragment_sha256": hashlib.sha256(fragment.encode("latin-1")).hexdigest(),
        "each_slice_verbatim_substring_of_driver": True,
        "output": args.out,
        "note": (
            "No symbol is stubbed. The two subroutines are the driver's own "
            "bytes, sliced out so that no dummy routine is ever linked beside "
            "code presented as official."
        ),
    }, indent=2))


if __name__ == "__main__":
    main()