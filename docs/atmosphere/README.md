# Atmospheric reference baselines

Direct reference evaluations of Exponential, NRLMSISE-00 and official JB2008. Atmospheric caching and surrogate evaluation are not implemented or authorised.

- [DESIGN.md](DESIGN.md): acquisition/force boundaries and physical limitations.
- [DECISIONS.md](DECISIONS.md): chronological decisions, alternatives and consequences.
- [PARAMETERS.md](PARAMETERS.md): every selected parameter, model constant and convention.
- [INPUTS.md](INPUTS.md): time, coordinate, weather and provenance conventions.
- [NRLMSISE00.md](NRLMSISE00.md), [JB2008.md](JB2008.md), [EXPERIMENTS.md](EXPERIMENTS.md): model backends and experiment procedure.
- [VALIDATION.md](VALIDATION.md): independent fixtures, boundary/metamorphic checks and exercised builds.
- [BASELINE.md](BASELINE.md): frozen direct measurements and reproducibility commands.
- `evidence/`: machine-readable parameters, provenance and measured records; `direct-v1/` is the frozen baseline.

## Acquire and build

Everything external is acquired explicitly, never by CMake:

```sh
python3 docs/atmosphere/acquire.py --destination /tmp/leo-atmosphere-deps \
  --groups nrlmsise00 jb2008 weather kernels --acknowledge-external-terms

cmake -S docs/atmosphere -B build-atmosphere -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLEO_ENABLE_NRLMSISE00=ON -DLEO_ENABLE_JB2008=ON \
  -DLEO_NRLMSISE00_ROOT=/tmp/leo-atmosphere-deps \
  -DLEO_JB2008_SOURCE=/tmp/leo-atmosphere-deps/JB2008.f \
  -DCSPICE_ROOT=/path/to/cspice -DCMAKE_PREFIX_PATH='/path/to/eigen;/path/to/boost'
cmake --build build-atmosphere --parallel
```

JB2008 additionally needs GNU Fortran (`-std=legacy`, fixed form). Both empirical backends are optional; the existing lightweight kernel build requires neither C empirical source nor Fortran. JB2008 upstream code and restricted input/reference files are externally acquired, not redistributed; inspect applicable upstream terms before acquisition/use.
