"""Validate the *installed* leo_dynamics distribution, then run the Python suite.

This is the entry point used by the wheel CI (``cibuildwheel`` test command) and
by anyone who installs a locally built wheel. It exists because a passing test
run can be silently meaningless: ``tests/python/scenario_fixtures.py`` falls back
to the development build directories when ``leo_dynamics`` is not importable, so
a suite run in the source tree can prove nothing about the wheel.

The driver therefore refuses to continue unless all of the following hold:

* ``leo_dynamics`` imports from the installed distribution (site-packages), not
  from ``<source>/python`` or any ``build-*/python`` staging directory;
* no ``sys.path`` entry and no working directory lives inside the source tree;
* the installed metadata version matches ``leo_dynamics.__version__``;
* installed requirements are consistent (``pip check`` where pip is available).

Only then does it run ``tests/python`` from a temporary directory outside the
source tree with ``PYTHONPATH`` removed.

Usage:

    python ci/run_installed_wheel_tests.py [SOURCE_ROOT] [extra pytest args...]
"""

from __future__ import annotations

import importlib.metadata
import importlib.util
import os
import subprocess
import sys
import sysconfig
import tempfile
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"installed-distribution check failed: {message}")


def scrub_development_paths(source: Path) -> None:
    """Drop development path overrides so only the installation can satisfy imports."""
    for name in ("PYTHONPATH", "PYTHONHOME"):
        if os.environ.pop(name, None) is not None:
            print(f"removed {name} from the environment before importing")
    # A development PYTHONPATH usually points at <source>/python or a
    # build-*/python staging directory; remove those entries instead of trusting
    # them, so the origin assertion below is about the wheel and nothing else.
    # The script directory goes too: this file is not a source root.
    here = str(Path(__file__).resolve().parent)
    stale = [
        entry
        for entry in {here, *sys.path}
        if entry and Path(entry).resolve().is_relative_to(source)
    ]
    for entry in sorted(stale):
        sys.path.remove(entry)
        print(f"removed source-tree path entry {entry}")


def assert_installed(project: Path) -> Path:
    """Import leo_dynamics and prove it resolves to the installed distribution."""
    source = Path(project).resolve()
    purelib = Path(sysconfig.get_paths()["purelib"]).resolve()

    scrub_development_paths(source)

    try:
        import leo_dynamics
    except ImportError as error:
        fail(f"leo_dynamics is not importable ({error}); is the wheel installed?")

    location = Path(leo_dynamics.__file__).resolve()
    if not location.is_relative_to(purelib):
        fail(f"leo_dynamics resolved to {location}, which is outside {purelib}")
    if location.is_relative_to(source):
        fail(f"leo_dynamics resolved inside the source tree: {location}")

    for entry in sys.path:
        resolved = Path(entry or Path.cwd()).resolve()
        if resolved.is_relative_to(source):
            fail(f"sys.path still contains source-tree entry {entry}")
    if Path.cwd().resolve().is_relative_to(source):
        fail(f"working directory is inside the source tree: {Path.cwd()}")

    try:
        metadata_version = importlib.metadata.version("leo-dynamics")
    except importlib.metadata.PackageNotFoundError:
        fail("installed distribution metadata for 'leo-dynamics' is missing")
    if metadata_version != leo_dynamics.__version__:
        fail(f"metadata version {metadata_version} != __version__ {leo_dynamics.__version__}")

    print(f"installed distribution: {location}")
    print(f"version: {leo_dynamics.__version__}")
    assert_payload()
    return location


def assert_payload() -> None:
    """Check the installed file list: facade, extension, typing, and no debris."""
    installed = {
        str(entry)
        for entry in importlib.metadata.files("leo-dynamics") or []
    }
    package_files = sorted(name for name in installed if name.startswith("leo_dynamics/"))
    print("installed payload:", package_files)

    for required in ("leo_dynamics/__init__.py", "leo_dynamics/__init__.pyi",
                     "leo_dynamics/py.typed"):
        if required not in installed:
            fail(f"the installed distribution is missing {required}")
    if not any(name.startswith("leo_dynamics/_core.") and name.endswith((".so", ".pyd", ".dylib"))
               for name in installed):
        fail(f"the installed distribution has no compiled _core extension: {package_files}")

    debris = [name for name in package_files
              if name.startswith(("leo_dynamics/outputs/", "leo_dynamics/build"))]
    if debris:
        fail(f"the installed distribution carries build debris: {debris}")


def check_requirements() -> None:
    """Fail unless pip reports a consistent environment in this interpreter."""
    if importlib.util.find_spec("pip") is None:
        fail("pip is not installed; install it to check dependency consistency")
    result = subprocess.run([sys.executable, "-m", "pip", "check"])
    if result.returncode != 0:
        fail("pip check reported an inconsistent environment")


def smoke() -> None:
    """Touch the compiled extension once; the suite covers every behaviour."""
    import numpy as np

    import leo_dynamics as leo

    earth = leo.Earth()
    radius = 7.0e6
    speed = (earth.mu_m3_s2 / radius) ** 0.5
    trajectory = leo.propagate(
        initial=leo.State(position_m=[radius, 0.0, 0.0], velocity_m_s=[0.0, speed, 0.0]),
        spacecraft=leo.Spacecraft(mass_kg=500.0, inertia_kg_m2=np.diag([12.0, 14.0, 18.0])),
        model=leo.DynamicsModel(gravity="j2"),
        times_s=np.arange(0.0, 600.0, 60.0),
    )
    if trajectory.states.shape != (10, 13):
        fail(f"unexpected state shape {trajectory.states.shape}")
    print(f"installed-wheel smoke: {earth!r}; j2 propagation returned {trajectory.states.shape}")


def main(argv: list[str]) -> int:
    arguments = list(argv[1:])
    project = Path(arguments.pop(0) if arguments else Path(__file__).resolve().parent.parent)
    tests = project / "tests" / "python"
    if not tests.is_dir():
        fail(f"no Python suite at {tests}")

    assert_installed(project)
    check_requirements()
    smoke()

    if importlib.util.find_spec("pytest") is None:
        fail("pytest is not installed; install it to run tests/python")

    environment = dict(os.environ)
    # A development PYTHONPATH would silently re-point the suite at the tree.
    environment.pop("PYTHONPATH", None)
    environment.pop("PYTHONHOME", None)

    with tempfile.TemporaryDirectory(prefix="leo-wheel-tests-") as scratch:
        result = subprocess.run(
            [sys.executable, "-m", "pytest", str(tests), "-q", "-p", "no:cacheprovider",
             *arguments],
            cwd=scratch,
            env=environment,
        )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))