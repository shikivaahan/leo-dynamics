#!/usr/bin/env python3
"""Exercise the real audit trace as strict JSON and an exact principal-axis orbit."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="leo-trace-json-") as directory:
        kernel=Path(directory)/"empty.tm"
        # A real, valid SPICE metakernel. No ephemeris is needed by the torque-free
        # principal-axis scenario, so this regression needs no external kernels.
        kernel.write_text("KPL/MK\n\n\\begindata\n\n\\begintext\n")
        done=subprocess.run([str(binary),"--spk",str(kernel),"--lsk",str(kernel),
                             "--scenario","principal","--method","trace","--duration","90"],
                            check=True,capture_output=True,text=True)
        payload=json.loads(done.stdout)
    records=[dict(zip(payload["steps_fields"],row,strict=True)) for row in payload["steps"]]
    assert sum(not r["accepted"] for r in records)==payload["rejected"]
    assert payload["trace_events"]["states"]==sum(r["accepted"] for r in records)
    start,final=payload["samples"][0],payload["samples"][-1]
    assert start[11]==start[12]==0
    c,s=math.cos(start[13]*final[0]/2),math.sin(start[13]*final[0]/2)
    w,x,y,z=start[7:11]
    expected=[c*w-s*z,c*x+s*y,c*y-s*x,c*z+s*w]
    error=min(math.hypot(*(a-sign*b for a,b in zip(final[7:11],expected))) for sign in (-1,1))
    assert error<1e-9, error
    assert math.hypot(*(a-b for a,b in zip(start[11:14],final[11:14])))<1e-14
    print("Strict JSON trace accepted; step acceptance counts and independent exact-spin identity hold")


if __name__=="__main__":main()
