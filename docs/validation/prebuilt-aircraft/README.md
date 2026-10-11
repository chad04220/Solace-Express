# Portable aircraft review evidence

This bounded evidence set records the completed review of original release `72d0af0` against the corrected prebuilt-aircraft producer contract. The tested build digest is `25b0ebe6af771a254ed9011d43a42b59cb21625ece22a53bcc84340c04be062c`.

- [Review result and limits](report.md)
- [Runtime metrics and test conditions](runtime-validation.json)
- [44 native image/pose comparisons](native-parity.json)
- [Cold, local-cache and packaged GPU uploads](gpu-upload-parity.json)
- [Five repeated load-only trials per path](load-benchmarks.json)
- [Five representative fallback/precedence cases](runtime-paths.json)
- [Atlas original startup-state finding and resolution](atlas-startup.json)
- [Source, builder-program and material invariants](source-invariants.json)
- [Adapter binary and harness provenance](adapter-provenance.json)
- [Canonical production summary](production.json)
- [All-30 payload parity](payload-parity.json)
- [Source manifest](source-manifest.json) and [release manifest](release-manifest.json)
- [Raw report provenance hashes](evidence-index.json)
- [Reproduction instructions and source-only harness](reproduce/README.md)

The JSON files contain portable summaries and hashes, not references to inaccessible absolute workspace paths. `evidence-index.json` names raw reports relative to the original external review workspace; those names describe provenance rather than files committed here. Large native PNGs, GPU readbacks, cache bodies, the ZIP, compiled objects and executables are intentionally excluded from Git. Reproduction creates them outside the repository.

Results establish exact fresh-bake, transport and tested rendering parity on Mesa llvmpipe. They do not establish Windows GPU behavior, RTX 3070 performance, every camera/animation state, a minimum frame rate, or complete game-startup latency. The [review handoff](../../CLAUDE_PREBUILT_AIRCRAFT_REVIEW.md) records remaining acceptance work.
