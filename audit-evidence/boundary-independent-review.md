# Independent release-boundary review

**Date:** 2026-10-26. **Final result:** the specific false acceptances found during this review are **fixed in the final reviewed checker**. Two complete synthetic controls pass; **all 19 negative archive cases are rejected**, including the final ABI-policy case. Both manifest-tamper cases are rejected. The current source/canonical-manifest check passes.

This is an independent structural/provenance review, **not legal clearance or permission to publish**. No source, packaging, workflow, licence or test file owned by the release worker was edited. No build, native test, workflow, installation or publication was run. Only isolated synthetic archive scenarios were exercised; their fake native payloads were never executed or imported.

## Reviewed identities

SHA-256 identifies the code actually loaded for the final retest. The verifier was hashed again after the scenarios and was unchanged.

| File | Final reviewed SHA-256 |
|---|---|
| `ci/verify_license_boundary.py` | `e91055ab0079ed14c5ca41d2e0ca9b87623038be5703e2146fefc0107eeb8f65` |
| `release-audit/generate_boundary.py` | `77e731eed461991b721d0fd0790d53212e667936f46c71d8f18f3cf40830ab1b` |
| `release-audit/license-boundary.json` | `24806398adb458a92dcfd9051103ee8dd54929044d4f16cbc577f2027a9bb5f6` |
| `pyproject.toml` | `678e5de70ec1682d7902cb945d0368beb87042848fb5e62045307868f7150e2c` |
| `CMakeLists.txt` | `355d0a111e9226991e414a48d2fde16353246934bf2f402a008fa066063e4784` |
| `LICENSE` | `c692219b6fe64b0621d0e6e1cb09e6e4f135e856e5d5215f6a31e23099fb29ce` |
| `LICENSE_SCOPE.md` | `7567cbbbf9ae53b58cbb85e2738773aeac85b4170c79c7306eff6014a67b466c` |
| `PROPRIETARY-NOTICE.md` | `c9bf6855729a4fa72a7c386b6f372a44a77dddd0e1c2b372ec564d6871844b54` |
| `README.dist.md` | `265d4a1c394aad8d87305f3282ba401cd04c0e9dd7447cd20cb9d7e27d8b5a8a` |
| `THIRD_PARTY_NOTICES.md` | `5634a26d2d94c5551c13b1459df7d26d62454521053c435498dd0e0a2af67466` |

Initial verifier: `cd6d59bd4da65c3901e9210b5029eab1116f7599359db61490944a519a0fa64b`.
An intermediate hardened snapshot, `d4a8d30543033e4a370a63310e568dd516031e11a8f8bff74ad10377563d7529`, still accepted an `abi3.so` module; the final hash above removes that exception and rejects it.

The review used frozen copies of the 52 approved public source files for archive comparisons, avoiding races with concurrent documentation edits. Results apply to these snapshots; later byte changes require the release worker's final archive check again.

## Initial findings versus final retest

All issues below were sent directly to the release worker and Main. **The initial column is historical, not a list of current vulnerabilities.**

| Scenario | Initial behavior | Final independent retest |
|---|---|---|
| Alter only top-level `sdist_members` to admit a private path | Source gate accepted; complete synthetic sdist could then admit private member | **Rejected:** complete canonical-manifest comparison; command path uses regenerated canonical data |
| Empty `proprietary_markers` without changing `files` | Accepted | **Rejected:** complete manifest mismatch |
| Replace allowed `src/kernel/gravity.cpp` archive bytes with actual private research-scenario JSON | Accepted in otherwise complete sdist | **Rejected:** bytes differ from approved public source |
| Replace wheel `__init__.py` with that research payload | Accepted | **Rejected:** mapped-source byte comparison |
| Add `.dist-info/licenses/private.json` containing research data | Accepted | **Rejected:** exact declared-licence allowlist |
| ZIP symlink using an allowed licence-member name | Accepted | **Rejected:** ZIP Unix-mode/type validation |
| Unsafe ZIP directory `../escape/` | Accepted because directories were skipped first | **Rejected:** name safety precedes directory handling |
| Split full sdist across two unrelated top-level directories | Accepted | **Rejected:** exact expected source root |
| Foreign `.dist-info` name or missing mandatory public package/licence files | Accepted | **Rejected:** exact metadata directory and required member set |
| METADATA/PKG-INFO extra private header or private `Summary` with otherwise valid public readme | Accepted, including in first hardened version | **Rejected:** header set and values constrained to public `pyproject.toml` |
| Entire WHEEL file replaced by private research JSON; RECORD hashes recomputed correctly | Accepted, including in first hardened version | **Rejected:** WHEEL schema, generator, tags and empty-body checks |
| `_core.abi3.so` in a version-specific wheel | Accepted through intermediate snapshot | **Rejected** in final reviewed checker |
| Tar symlink, duplicate member, traversal; extra wheel package module | Rejected | **Still rejected** |

**Controls:** a complete 52-file public sdist plus valid PKG-INFO, and a complete wheel with correct metadata, declared licence files and RECORD hashes, both pass. All final wheel adversarial cases have coherent RECORD hashes, so their rejection is not merely an incidental checksum failure. The native payload is deliberately synthetic ELF-marked data: accepting that control demonstrates structural acceptance only, not an actual native build.

Machine-readable scenarios, failure messages, identities and parameter comparison: `audit-evidence/boundary-independent-review.json`.

## Packaging, Git-index and native-boundary observations

- **52 public source files.** The reviewed canonical manifest and licence-scope documents agree on the minimal engine/interface/build/notices/public-readme boundary. Research docs, examples, plotting helpers, tests/fixtures, empirical atmosphere providers and the CSPICE adapter are outside it. The three provider-independent environment headers are public; `spice.hpp` and `src/environment/spice.cpp` are private. No unresolved path-scope contradiction was found in this reviewed set.
- **Backend include is not an allowlist.** Pinned scikit-build-core 0.11.6 `build/sdist.py` invokes `each_unignored_file`; `_file_processor.py:70-115` walks the working tree with `followlinks=True`, and an explicit include wins before exclude/ignore rules. The final anchored includes and exhaustive private exclusions therefore require the **post-build member gate**; a source-only check is not a proof of archive contents. The release worker reports an observed 52-file-plus-PKG-INFO sdist, separately recorded in its archive evidence.
- **Index rather than HEAD.** The generator uses `git ls-files`, so newly staged files participate, staged removals disappear, and untracked files do not participate. Source-byte checks read the working tree. This is coherent for a clean release checkout but must not be described as an immutable-commit attestation in a dirty working tree. A staged rename changes the manifest and is rejected until reviewed/regenerated. Regenerating rules that grant a whole public directory can classify a new file under that prefix; the tool cannot determine semantic ownership merely from its new name.
- **Native public builds now enforce the boundary.** Initial CMake only rejected absent private sources, leaving full-checkout packaging overrides possible. The reviewed final `if(SKBUILD)` guard rejects CSPICE/NRLMSISE00/JB2008, examples, testing and plotting, and requires pinned dependency provisioning. This was **statically reviewed**, not configured or built by this reviewer. Non-SKBUILD private native experiments remain available.
- **Binary provenance remains a distinct requirement.** The checker can reject known strings, unapproved members and non-native magic; it cannot prove that arbitrary machine code contains no private implementation or numerical material. Source/build provenance, consumed headers and actual linkage evidence remain necessary. No archive extractor was run on an unsafe member.

## Concrete public numerical-data provenance

The public exponential-atmosphere implementation does contain a fixed parameter table. All **40 altitude/density pairs** in `src/kernel/aerodynamics.cpp:9-21` exactly equal those in upstream `src/Constants.jl:100-142` at cited commit `58ab4d4964f9c20b7070efae48f8916e8a99784e`, after converting km to m. Comparison used decimal arithmetic, not approximate floating-point matching. Public `include/leo/kernel/aerodynamics.hpp:15-17` already acknowledges Julia's knots. The upstream labels the table “1976 US Standard Atmosphere.”

This is a concrete inherited **numeric-table** finding; it was missed by the earlier whitespace-line snippet scan because the representation changed. It does **not** establish infringement, database rights, ultimate source or legal ownership. It must not be conflated with the owner's private campaign datasets, nor silently removed from the approved basic engine.

The worker corrected `README.dist.md` to disclose the 40 anchors and qualify the no-external-dataset claim. The updated notices include precautionary upstream attribution. The earlier blanket “no atmosphere reference data bundled” wording is therefore **fixed**, not an outstanding public-file leakage allegation.

Source hashes: public aerodynamic source `cb1f73ce7e957bddd6720fea255c3620364c61caabb0918ca941c3455bc53a2f`; upstream Constants.jl blob `59e072f830235ffd0ce05a19182b2064d48daff6d84b9fb7d64f0fba03e39cdf`. No private source or table dump is included in the evidence.

## Actual dependency/build evidence reviewed, not rerun

The release worker's logs now provide exact observed environments:

| Environment | Observed installed distributions |
|---|---|
| Build requirements, Python 3.12.14 | cmake **4.4.4**, ninja **1.13.2**, scikit-build-core **0.11.6**, pybind11 **3.1.0**, packaging **26.3**, pathspec **1.1.1** |
| Build driver, Python 3.13.15 | build **1.6.1**, pyproject-hooks **1.3.3**, packaging **26.3**, pip **26.2.2** |
| Installed runtime/test, Python 3.12.14 | NumPy **2.5.4**, pytest **9.1.1**, pluggy **1.7.0**, iniconfig **2.3.1**, Pygments **2.21.0**, packaging **26.3**, pip **26.2.2**, leo-dynamics **0.1.0** |

Evidence: `resolved-build-requirements.log`, `resolved-build-licenses.log`, `build-driver-versions.log`, `resolved-driver-licenses.log`, `final-installed-dependencies.log`, `resolved-test-licenses.log`, under `release-audit/`.

I independently opened the installed licence files for the newly identified build transitives: **pathspec 1.1.1: MPL-2.0**; **packaging 26.3: Apache-2.0 OR BSD-2-Clause**; **pyproject-hooks 1.3.3: MIT**. Their use as tools does not, merely by generating a distribution, license the generated project under their licences; redistribution of the tools themselves is a different question. Pytest/pluggy/iniconfig/pip are MIT; Pygments is BSD-2-Clause; the observed NumPy metadata expression is BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0. These are not additional vendored project runtime libraries.

`consumed-header-verification.json` reports **170/170 Eigen headers** and **1066/1066 Boost headers** matched to the pinned archives, with zero missing/mismatching headers. This is worker-produced evidence I inspected, not a comparison I reran. It closes the earlier consumed-header uncertainty **for the recorded Linux build**, not for unobserved platforms.

`final-direct-needed.log` observes dynamic libstdc++, libgcc_s, libc, libm and the Linux loader in the actual built extension; `final-archive-gate.log` records the worker's final archive gate passing; `archive-identities.json` records the checked artefact identities. I did not rerun their builds, native tests or installed-wheel tests, and do not claim those checks as independently exercised.

Conditional dependencies on other Python versions/platforms, all-platform wheel contents, legal title/assignment, the ultimate rights in the inherited parameter table and current GitHub billing-plan availability remain outside what these Linux-environment observations establish.

## Integration conclusion

**No unresolved actionable archive false acceptance remains among the scenarios exercised against the final reviewed hash.** Initial findings must be described as fixed. Preserve the residual distinctions: path/byte enforcement is not semantic rights clearance; marker scans are not compiled-provenance proof; index-based checks are not HEAD attestations; Linux dependency observations are not all-platform observations. The final release worker remains responsible for checking its final rebuilt archive bytes after any further source/notices change.
