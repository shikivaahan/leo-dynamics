#!/usr/bin/env python3
"""Regression tests for the licence and distribution boundary gate.

The gate is the mechanism that keeps a public distribution limited to the
approved core sources, so its refusal behaviour is tested directly: a leaked
path, a link member, a traversal name, a duplicate member, an unexpected
wheel member and deferred-model content inside a member must each be refused.

The licence classification and the distribution allowlist are independent. The
tests cover both: the research material and the reference fixtures are MIT
licensed but not distributed.

The tests build synthetic archives in a temporary directory and run the real
gate as a subprocess against the real repository, so they exercise the shipped
behaviour and not a copy of it.

    python3 tests/license_boundary_tests.py
"""

from __future__ import annotations

import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "ci" / "verify_license_boundary.py"
DIST_INFO = "leo_dynamics-0.1.0.dist-info"


def run_gate(artefacts: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(GATE),
            "--project-root",
            str(ROOT),
            "--artifacts",
            str(artefacts),
        ],
        capture_output=True,
        text=True,
    )


class SdistRefusalTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self._temporary.name)
        self.addCleanup(self._temporary.cleanup)

    def _sdist(self, name: str = "leo_dynamics-0.1.0.tar.gz") -> Path:
        path = self.directory / name
        return path

    @staticmethod
    def _add(tar: tarfile.TarFile, member: str, payload: bytes = b"") -> None:
        info = tarfile.TarInfo(member)
        info.size = len(payload)
        tar.addfile(info, io.BytesIO(payload))

    def _build(self, name: str, members) -> Path:
        path = self._sdist(name)
        with tarfile.open(path, "w:gz") as tar:
            members(tar)
        return path

    def test_unapproved_path_is_refused(self) -> None:
        path = self._build(
            "leak.tar.gz",
            lambda tar: self._add(tar, "leo_dynamics-0.1.0/src/atmosphere/nrlmsise00.cpp"),
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not an approved distribution member", result.stderr)

    def test_unapproved_reference_fixture_is_refused(self) -> None:
        path = self._build(
            "fixture.tar.gz",
            lambda tar: self._add(
                tar, "leo_dynamics-0.1.0/tests/reference/golden/tudat/manifest.json", b"{}"
            ),
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not an approved distribution member", result.stderr)

    def test_link_member_is_refused(self) -> None:
        def members(tar: tarfile.TarFile) -> None:
            info = tarfile.TarInfo("leo_dynamics-0.1.0/src/kernel/state.cpp")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            tar.addfile(info)

        path = self._build("link.tar.gz", members)
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("not a regular file", result.stderr)

    def test_traversal_name_is_refused(self) -> None:
        path = self._build(
            "traversal.tar.gz",
            lambda tar: self._add(tar, "leo_dynamics-0.1.0/../escape.cpp"),
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("unsafe member name", result.stderr)

    def test_duplicate_member_is_refused(self) -> None:
        def members(tar: tarfile.TarFile) -> None:
            self._add(tar, "leo_dynamics-0.1.0/src/kernel/state.cpp", b"a")
            self._add(tar, "leo_dynamics-0.1.0/src/kernel/state.cpp", b"b")

        path = self._build("duplicate.tar.gz", members)
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("duplicate member", result.stderr)

    def test_deferred_content_in_member_is_refused(self) -> None:
        path = self._build(
            "content.tar.gz",
            lambda tar: self._add(
                tar,
                "leo_dynamics-0.1.0/src/kernel/state.cpp",
                b"// adapted from the NRLMSISE-00 C translation by Brodowski\n",
            ),
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("excluded content markers", result.stderr)

    def test_root_level_member_is_refused(self) -> None:
        path = self._build(
            "root.tar.gz",
            lambda tar: self._add(tar, "escape.cpp"),
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("at the archive root", result.stderr)


class WheelRefusalTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self._temporary.name)
        self.addCleanup(self._temporary.cleanup)

    def _wheel(self, name: str, members: dict[str, bytes]) -> Path:
        path = self.directory / name
        with zipfile.ZipFile(path, "w") as archive:
            for member, payload in members.items():
                archive.writestr(member, payload)
        return path

    def test_unexpected_library_is_refused(self) -> None:
        path = self._wheel(
            "leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl",
            {
                "leo_dynamics/__init__.py": b"",
                "leo_dynamics/_core.cpython-312-x86_64-linux-gnu.so": b"\x7fELF",
                "leo_dynamics/libhelper.so.1": b"\x7fELF",
            },
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("unexpected member", result.stderr)

    def test_loose_metadata_directory_is_refused(self) -> None:
        path = self._wheel(
            "leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl",
            {
                "leo_dynamics/__init__.py": b"",
                "leo_dynamics-0.1.0/METADATA": b"Name: leo-dynamics\n",
            },
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("unexpected member", result.stderr)

    def test_marker_free_renamed_payload_is_refused(self) -> None:
        path = self._wheel(
            "leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl",
            {"leo_dynamics/__init__.py": b"research payload narrative"},
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("bytes differ from approved", result.stderr)

    def test_arbitrary_licence_payload_is_refused(self) -> None:
        path = self._wheel(
            "leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl",
            {f"{DIST_INFO}/licenses/RESEARCH.md": b"research payload"},
        )
        result = run_gate(path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unexpected licence member", result.stderr)


class CanonicalManifestTests(unittest.TestCase):
    def test_licence_classification_is_independent_of_distribution(self) -> None:
        import importlib.util
        spec = importlib.util.spec_from_file_location("boundary_test", GATE)
        gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate)
        generator = gate._load_generator(ROOT)
        manifest = generator.build_manifest(ROOT)
        # The research material is MIT licensed even though it is not shipped.
        research = manifest["files"]["docs/atmosphere/JB2008.md"]
        self.assertEqual(research["class"], "mit")
        self.assertFalse(research["in_distribution"])
        # The core engine is MIT licensed and shipped.
        engine = manifest["files"]["src/kernel/gravity.cpp"]
        self.assertEqual(engine["class"], "mit")
        self.assertTrue(engine["in_distribution"])
        # Every tracked file is the project's own material under the MIT
        # grant; no tracked file carries a restricted classification.
        for path, entry in manifest["files"].items():
            self.assertEqual(entry["class"], generator.MIT, path)
        # The reference fixtures are generated by this project and are MIT
        # licensed, but they are excluded by the packaging allowlist.
        fixture = manifest["files"]["tests/reference/golden/tudat/manifest.json"]
        self.assertEqual(fixture["class"], "mit")
        self.assertFalse(fixture["in_distribution"])

    def test_top_level_allowlist_and_marker_tampering_is_refused(self) -> None:
        import importlib.util
        import copy
        spec = importlib.util.spec_from_file_location("boundary_test", GATE)
        gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate)
        generator = gate._load_generator(ROOT)
        canonical = generator.build_manifest(ROOT)
        for key, value in (
            ("approved_sources", canonical["approved_sources"] + ["docs/atmosphere/JB2008.md"]),
            ("deferred_external_markers", []),
            ("deferred_model_markers", []),
        ):
            with self.subTest(key=key):
                altered = copy.deepcopy(canonical)
                altered[key] = value
                failures, _ = gate.check_source(ROOT, altered, generator)
                self.assertTrue(any("complete licence boundary manifest" in item for item in failures))


class ArchiveCompletenessTests(unittest.TestCase):
    def setUp(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("archive_gate_test", GATE)
        self.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gate)
        self.manifest = self.gate._load_generator(ROOT).build_manifest(ROOT)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def metadata(self):
        import tomllib
        p = tomllib.loads((ROOT / "pyproject.toml").read_text())["project"]
        headers = [
            ("Metadata-Version", "2.4"), ("Name", p["name"]),
            ("Version", p["version"]), ("Summary", p["description"]),
            ("Keywords", ",".join(p["keywords"])),
            ("Author", ", ".join(a["name"] for a in p["authors"])),
            ("License-Expression", p["license"]),
            ("Requires-Python", ",".join(sorted(p["requires-python"].split(",")))),
            ("Description-Content-Type", p["readme"]["content-type"]),
        ]
        headers += [("License-File", v) for v in p["license-files"]]
        headers += [("Classifier", v) for v in p["classifiers"]]
        headers += [("Project-URL", f"{k}, {v}") for k, v in p["urls"].items()]
        headers += [("Requires-Dist", v) for v in p["dependencies"]]
        return ("\n".join(f"{key}: {value}" for key, value in headers)
                + "\n\n" + (ROOT / "README.dist.md").read_text()).encode()

    def wheel(self, alter=None):
        import hashlib
        import base64
        members = {name: (ROOT / ("python/" + name)).read_bytes()
                   for name in self.gate.WHEEL_PACKAGE_FILES}
        members["leo_dynamics/_core.cpython-312-x86_64-linux-gnu.so"] = b"\x7fELFsynthetic-test-only"
        members[f"{DIST_INFO}/METADATA"] = self.metadata()
        members[f"{DIST_INFO}/WHEEL"] = b"Wheel-Version: 1.0\nGenerator: scikit-build-core 0.11.6\nRoot-Is-Purelib: false\nTag: cp312-cp312-linux_x86_64\n\n"
        for name in ("LICENSE", "LICENSE_SCOPE.md", "THIRD_PARTY_NOTICES.md"):
            members[f"{DIST_INFO}/licenses/{name}"] = (ROOT / name).read_bytes()
        if alter:
            alter(members)
        record = f"{DIST_INFO}/RECORD"
        rows = [
            f"{name},sha256={base64.urlsafe_b64encode(hashlib.sha256(value).digest()).rstrip(b'=').decode()},{len(value)}"
            for name, value in members.items()
        ]
        members[record] = ("\n".join(rows + [f"{record},,"]) + "\n").encode()
        path = self.directory / "leo_dynamics-0.1.0-cp312-cp312-linux_x86_64.whl"
        with zipfile.ZipFile(path, "w") as archive:
            for name, value in members.items():
                archive.writestr(name, value)
        return path

    def test_complete_control_then_wrong_metadata_missing_payload_and_licence(self):
        path = self.wheel()
        self.assertEqual(self.gate._check_wheel(path, self.manifest)[0], [])
        for name in ("leo_dynamics/__init__.py", f"{DIST_INFO}/licenses/LICENSE"):
            path = self.wheel(lambda members: members.pop(name))
            self.assertTrue(self.gate._check_wheel(path, self.manifest)[0])
        path = self.wheel(lambda members: members.update({
            f"{DIST_INFO}/METADATA": b"Name: other\nVersion: 9\nLicense-Expression: GPL-3.0\n\nresearch payload"
        }))
        failures = self.gate._check_wheel(path, self.manifest)[0]
        self.assertTrue(any("metadata Name" in failure for failure in failures))
        self.assertTrue(any("metadata Version" in failure for failure in failures))
        self.assertTrue(any("License-Expression" in failure for failure in failures))

    def test_generated_metadata_cannot_carry_research_payloads(self):
        for field in ("Summary", "X-Research-Result"):
            path = self.wheel(lambda members: members.update({
                f"{DIST_INFO}/METADATA":
                    f"{field}: research payload\n".encode() + self.metadata()
            }))
            self.assertTrue(self.gate._check_wheel(path, self.manifest)[0])
        path = self.wheel(lambda members: members.update({
            f"{DIST_INFO}/WHEEL":
                (ROOT / "tests/reference/scenarios/case_c_short.json").read_bytes()
        }))
        self.assertTrue(self.gate._check_wheel(path, self.manifest)[0])

    def test_zip_symlink_and_unsafe_directory(self):
        import stat
        for name, mode in ((f"{DIST_INFO}/licenses/LICENSE", stat.S_IFLNK | 0o777),
                           ("../escape/", stat.S_IFDIR | 0o755)):
            path = self.wheel()
            with zipfile.ZipFile(path, "a") as archive:
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = mode << 16
                archive.writestr(info, b"/etc/passwd")
            self.assertTrue(self.gate._check_wheel(path, self.manifest)[0])

    def test_complete_sdist_then_renamed_research_and_multiple_roots(self):
        for mutation in (None, "research", "root"):
            path = self.directory / "source.tar.gz"
            with tarfile.open(path, "w:gz") as archive:
                for name in self.manifest["approved_sources"] + ["PKG-INFO"]:
                    value = self.metadata() if name == "PKG-INFO" else (ROOT / name).read_bytes()
                    prefix = "leo_dynamics-0.1.0"
                    if name == "src/kernel/gravity.cpp":
                        if mutation == "research":
                            value = (ROOT / "tests/reference/scenarios/case_c_short.json").read_bytes()
                        if mutation == "root":
                            prefix = "wrong_project-9.9.9"
                    SdistRefusalTests._add(archive, prefix + "/" + name, value)
            failures = self.gate._check_sdist(path, self.manifest)[0]
            if mutation is None:
                self.assertEqual(failures, [])
            else:
                self.assertTrue(failures)


if __name__ == "__main__":
    unittest.main()
