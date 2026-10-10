# Solace Express v3.43.0 release audit

Exact source: `91212bf4a131c02d2da41efbdb09248a09bc3599`. The newer living-islands development merge is not part of this published release.

## Method

Run stock suites, inspect code paths, then challenge them with independent player-reachable reproductions, differential mesh tests, input/UI flows, actual rendered evidence where supported, and release-asset integrity checks. Production source remains unchanged. Diagnostic harnesses are separate and their adaptations will be identified. Findings must distinguish reproduced failures, source-only concerns, visual preferences, performance opportunities and untested behavior.

## Coverage areas

- Career, contracts, progression, finance, airline and save/resume
- Flight, load/envelope, weather, autopilot, landing, emergency and tower behavior
- Renderer, procedural geometry, cloak, materials, caches and startup
- UI, controller/keyboard focus, settings, Free Flight, loading and audio
- World streaming/cache, terrain, collision, traffic, breakup and packaging

## Limits

This Linux VM uses software rendering. Native Windows operation, physical controllers and RTX 3070 frame rate require hardware testing. A cross-build or a software screenshot does not validate target performance. No finite audit proves the absence of all defects.
