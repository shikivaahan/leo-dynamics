#!/usr/bin/env python3
"""Generate measurement-only adapter copies; never write production sources."""
import argparse
import hashlib
import json
from pathlib import Path


def generate(root: Path, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for model in ('nrlmsise00', 'jb2008'):
        source = root / 'src/atmosphere' / (model + '.cpp')
        text = source.read_text()
        hashes[str(source.relative_to(root))] = hashlib.sha256(source.read_bytes()).hexdigest()
        anchor = '        std::lock_guard lock{native_mutex};'
        if text.count(anchor) != 1:
            raise RuntimeError(f'{source}: expected exactly one native lock anchor')
        text = '#include "native_diagnostics.hpp"\n' + text.replace(
            anchor, f'        mt_diagnostics::NativeLock lock{{native_mutex, mt_diagnostics::{model}}};')
        (out / (model + '.cpp')).write_text(text)
    (out / 'source-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    generate(args.root, args.out)
