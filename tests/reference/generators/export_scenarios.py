#!/usr/bin/env python3
"""Explicitly export versioned scenario inputs; normal regression never calls this."""
import argparse
import hashlib
import json
from pathlib import Path
from scenarios import build_scenarios


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path(__file__).resolve().parents[1]/'scenarios')
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    documents=build_scenarios()
    for name,document in documents.items():
        path=args.out/(name+'.json')
        path.write_text(json.dumps(document,indent=2,allow_nan=False)+'\n')
        print(name,hashlib.sha256(path.read_bytes()).hexdigest())


if __name__=='__main__':
    main()
