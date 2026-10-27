# Final history secret scan — 2026-10-27

**PASS: no confirmed secrets in all 89 reachable commits/all local refs scanned.**

Gitleaks 8.28.0 scanned 3,122,722,417 bytes, with archive and encoded-content recursion depth 3, redaction enabled and inline allow comments ignored. Its release archive checksum matches the upstream release checksums. Exact command and triage: `final-history-secret-scan.json`.

The scanner returned 162 generic-api-key candidates (exit 1). All were false positives: 160 SHA-256 source/dependency content digests independently checked against their historical JSON lines; one numerical-acceptance dictionary key; one historical report quoting that same key. No credential was identified, no revocation/removal/history rewriting is needed, and no secret values are reproduced here.

The supplementary read-only contextual review found no genuine candidate among current audit logs, authentication-related strings, private-key/provider-token patterns or archive inventories. A whole local directory scan including ignored builds/vendor trees timed out after 300 seconds; those directories are not committed or published. Current release changes receive a separate staged scan before push. Scanner coverage is not a proof about unreachable objects or every possible secret format.

Owner authorisation governs publication of existing source/history/research; personal author email and local paths are not treated as credentials. Research data and Git history are preserved.
