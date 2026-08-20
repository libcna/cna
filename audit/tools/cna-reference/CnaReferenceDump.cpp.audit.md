# Audit: tools/cna-reference/CnaReferenceDump.cpp

## Metadata
- Source file: `tools/cna-reference/CnaReferenceDump.cpp` (740 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-cna-reference` shard
- File type: C++ CLI tool (differential-testing reference-value dumper)
- XNA/FNA relevance: directly exercises many `Microsoft::Xna::Framework::Graphics` enums/state
  presets/`PackedVector` types/`Viewport`, specifically to diff against real FNA
- Main related tests: N/A (standalone comparison tool, not a GTest)

## Purpose
Dumps CNA's own real values for every enum, state-object preset, `PackedVector` pack/unpack case,
and `Viewport.Project`/`Unproject` round-trip case as JSON, structured to be diffed key-for-key
against the equivalent real-FNA dump (`tools/fna-reference/*.cs`) via
`scripts/compare-fna-reference.py`.

## Executive Verdict
Correct, and independently confirmed valuable by its own paired README: this exact tool already
found and helped fix one genuine, real production bug (`IndexElementSize`'s numeric values not
matching real FNA — `plans/plan_graphics.md` Task 921, since fixed) via an actual, non-hypothetical
comparison run against a real FNA build. A concrete demonstration that this kind of differential
tooling catches real defects, not just theoretical value.

## Checklist Results
- Every enum dump (`DumpEnums()`, lines 79-274) uses `static_cast<int>` consistently, matching the
  comparison script's own documented value-equality contract.
- `SurfaceFormat`'s 6 `*EXT`-suffixed entries (lines 199-209) are explicitly annotated as "real
  members added to FNA's own SurfaceFormat enum in newer FNA releases... confirmed against the
  locally-built FNA.dll, version 26.5.0.0" — a precise, version-pinned claim rather than a vague
  assertion, correctly distinguishing these from CNA's own NOXNA extensions despite sharing the
  `EXT` naming convention.
- `PrimitiveType.PointListEXT` (line 175) is explicitly and correctly noted as a genuine CNA-only
  extension included deliberately, since the comparison is one-directional (every FNA key must
  exist on the CNA side; extra CNA-only keys are not failures) per the paired README.
- Every `PackedVector` case in `DumpPackedVector()` uses input values chosen to exercise
  boundary/representative cases (e.g. `Byte4`'s `{255,0,0,255}`/`{100,150,200,128}`,
  `HalfVector4`'s `{-1,-1,-1,-1}`), not just an all-zero/trivial case.

## Detailed Findings
None.

## Cross-File Observations
Uses `JsonWriter.hpp` (audited alongside this file) — the `double`-precision handling there
(`max_digits10`) is specifically what makes this file's large packed-integer values and
sub-millimeter float differences round-trip exactly through the JSON output, per that file's own
documented history of a real truncation bug this tool's own development surfaced.

## Missing or Weak Tests
This file IS the comparison-generation half of a manual, non-`ctest`-registered developer workflow
(per the paired README, since it needs `mono`/`xbuild` and a locally-built `FNA.dll` not guaranteed
on every build machine) — not flagged as a gap given that this is a documented, deliberate scope
decision, not an oversight.

## Positive Findings
The paired README's "Status" section is an exemplary piece of self-reporting: it documents not just
that the tool works, but the actual bugs found in *both* the tool itself (float-precision
truncation, several missing state-preset properties, 7 missing `SurfaceFormat` enum members in an
earlier draft) and in production code (`IndexElementSize`), with the production bug's resolution
explicitly confirmed re-verified after the fact ("Task 921 has since fixed this — CNA's
`IndexElementSize` now uses... re-running this comparison today would find no divergence here").

## Final Assessment
No findings.
