#!/bin/sh
# Portable entrypoint for the Stage 4 LEO dynamics benchmark suite.
#
# Usage:
#   benchmarks/run.sh --source <repo root> --build <build dir> --output <csv> \
#                    --label <configuration id> --repetitions 7 --warmup 1 [options]
#
# Runs benchmarks/runner.py with the first available Python 3 interpreter.
# Override the interpreter with PYTHON=/path/to/python3.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

find_python() {
  if [ -n "${PYTHON:-}" ]; then
    printf '%s\n' "$PYTHON"
    return 0
  fi
  for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
      if "$candidate" -c 'import sys; sys.exit(0 if sys.version_info[:2] >= (3, 8) else 1)' \
        >/dev/null 2>&1; then
        command -v "$candidate"
        return 0
      fi
    fi
  done
  return 1
}

if ! python_bin=$(find_python); then
  echo "benchmarks/run.sh: no Python 3.8+ interpreter found (set PYTHON=...)" >&2
  exit 1
fi

exec "$python_bin" "$script_dir/runner.py" "$@"