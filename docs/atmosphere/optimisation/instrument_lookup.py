#!/usr/bin/env python3
"""Time actual cache searches in a private diagnostic SPICE translation unit."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    original = source
    def replace(anchor, replacement):
        nonlocal source
        if source.count(anchor) != 1:
            raise RuntimeError('diagnostic anchor is not unique: '+anchor)
        source = source.replace(anchor, replacement, 1)
    replace('SpiceReuseStatistics statistics;', '''SpiceReuseStatistics statistics;
std::array<std::uint64_t,4> lookup_measurements{};
void record_lookup(std::size_t kind,std::chrono::steady_clock::time_point start) {
    lookup_measurements[kind*2]+=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    ++lookup_measurements[kind*2+1];
}''')
    replace('        bool hit=false;', '        bool hit=false;\n        const auto lookup_start=std::chrono::steady_clock::now();')
    replace('        if(hit) {', '        if(reuse) record_lookup(0,lookup_start);\n        if(hit) {')
    anchor='    const auto bits=reuse ? std::bit_cast<std::uint64_t>(epoch.et_seconds):0;'
    replace(anchor,'    const auto lookup_start=std::chrono::steady_clock::now();\n'+anchor)
    replace('            REUSE_COUNT(orientation_hits);', '            record_lookup(1,lookup_start);\n            REUSE_COUNT(orientation_hits);')
    replace('    REUSE_COUNT(orientation_misses);', '    if(reuse) record_lookup(1,lookup_start);\n    REUSE_COUNT(orientation_misses);')
    replace('void SpiceEphemerisProvider::reset_reuse_statistics() {', '''extern "C" void atm_opt_lookup_measurements(std::uint64_t* result) {
    std::lock_guard lock{spice_mutex};
    for(std::size_t i=0;i<lookup_measurements.size();++i) result[i]=lookup_measurements[i];
}
void SpiceEphemerisProvider::reset_reuse_statistics() {''')
    replace('    statistics={};', '    statistics={};\n    lookup_measurements.fill(0);')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(source)
    args.out.with_suffix('.json').write_text(json.dumps({
        'source':str(args.source),'source_sha256':hashlib.sha256(original.encode()).hexdigest(),
        'generated_sha256':hashlib.sha256(source.encode()).hexdigest(),
        'scope':'cache search only; acquisition of the existing SPICE mutex excluded; steady_clock end-read overhead retained; diagnostic binary only'},indent=2)+'\n')


if __name__ == '__main__':
    main()
