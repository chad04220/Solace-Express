# Final Plane header and object freshness audit

Result: PASS. No stale or unproven `Plane` ABI consumer was found.

Cutoff: 2026-10-10 21:36:00 UTC, after the approved guidance/header application. The source header retains its copied 21:14 mtime, so this audit deliberately compares object compilation against the application cutoff rather than against that header mtime.

## solace-new-aircraft-build
- Existing compiled objects: 466. Plane consumers: 229.
- Stale consumers: 0. Unmapped objects: 0. Unresolved local includes: 0.
- Earliest consumer compiler-start timestamp: 2026-10-10T21:36:45.894517+00:00.
- Older independent objects: 34; their full local include closures exclude `aircraft.h`.
- Metadata warning: path checksum mismatch at byte 155148: expected path ID 574, found 540. The damaged database exposes only 51 header consumers; that count alone is insufficient. All 229 consumers were independently located from the complete source/include graph and checked against object and Ninja v7 compilation timestamps.

## solace-new-aircraft-windows
- Existing compiled objects: 492. Plane consumers: 251.
- Stale consumers: 0. Unmapped objects: 0. Unresolved local includes: 0.
- Earliest consumer compiler-start timestamp: 2026-10-10T21:36:47.362499+00:00.
- Older independent objects: 236; their full local include closures exclude `aircraft.h`.
- Dependency database parses completely; its consumer set agrees with the independent source/include audit.

## solace-new-aircraft-sanitize
- Existing compiled objects: 430. Plane consumers: 211.
- Stale consumers: 0. Unmapped objects: 0. Unresolved local includes: 0.
- Earliest consumer compiler-start timestamp: 2026-10-10T21:36:14.490893+00:00.
- Older independent objects: 142; their full local include closures exclude `aircraft.h`.
- Dependency database parses completely; its consumer set agrees with the independent source/include audit.

## Method and limits
- Read original metadata as bytes without opening it through Ninja. Parsed immutable in-memory data and checked that file size/mtime did not change during the read. Earlier diagnostic Ninja calls used temporary copies only.
- Audited every on-disk `.o`/`.obj` against the current compiler edges, recursively followed local and generated includes, and conservatively retained conditionally included headers. No PCH, unity builds, or forced-include flags were found.
- Checked object completion times and compiler-start times using Ninja v7 command records, cross-checked against object mtime minus compile duration. All relevant starts clear the cutoff.
- Sanitizer snapshot was taken after its build exited successfully at approximately22:04 UTC. Tests may continue, but no compiler was running.
- No builds, cleaning, metadata repairs, file touches, or production edits were performed. The Release dependency database warning remains a build-cache maintenance issue; it does not invalidate the independently verified current binaries. Avoid querying writable original Ninja metadata while a build is active.
- This audit establishes source/header freshness, not test success or native Windows graphics behavior. Aggregate test results remain separate.

Source hashes:
- `src/aircraft.h`: 469135a8858fbc43b889beacfa8d5f6bebb14b94a16e6a01331547d0f44b1b00
- `src/aircraft.cpp`: db56dcabc289c318c50b495d91b072b952e0797ab30d640c89ea1cebac9911c2

Per-object evidence: `final-header-freshness-independent.json`. Reproducible read-only auditor: `final-header-freshness-audit.py`.
