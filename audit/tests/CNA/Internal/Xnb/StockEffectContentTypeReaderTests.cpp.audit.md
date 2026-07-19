# Audit: tests/CNA/Internal/Xnb/StockEffectContentTypeReaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/StockEffectContentTypeReaderTests.cpp` (234 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests all 5 stock-effect `.xnb` readers (backs `.xnb`-based loading of
  `Microsoft::Xna::Framework::Graphics::{BasicEffect,AlphaTestEffect,DualTextureEffect,
  EnvironmentMapEffect,SkinnedEffect}`), Task XNB-32
- Main related tests: shares the `BlenderDefaultCube.xnb` fixture with `ModelContentTypeReaderTests.cpp`

## Purpose
Tests registration of all 5 stock-effect readers; `BasicEffectReader` against a real, precisely
byte-range-located slice of an actual MonoGame-produced fixture; the other 4 readers (no real
fixture available) against hand-constructed streams verified field-by-field against FNA's own
`*EffectReader.cs` sources.

## Executive Verdict
Excellent, methodologically careful test file. `BasicEffectReaderParsesRealFnaProducedBytes`
includes a defensive `ASSERT_EQ(fileBytes.size(), 1802u)` size check with an explicit failure
message ("Real fixture not found relative to CWD, or changed size") BEFORE extracting the hardcoded
byte-offset slice — correctly guarding against silently reading garbage or an out-of-range
substring if the fixture is ever missing or unexpectedly modified.

## Checklist Results
- The shared `ReadViaReader<T>()` helper's own comment correctly explains a real, non-obvious type-
  erasure detail: every stock-effect reader erases to `std::shared_ptr<Effect>` (the common base),
  not the concrete type, because `ReadSharedResource<std::shared_ptr<Effect>>()` needs every reader
  to agree on one erased type regardless of which concrete effect a given file actually used — this
  is exactly the kind of architectural detail that's easy to get wrong in a test helper (e.g.
  attempting `std::any_cast<std::shared_ptr<ConcreteType>>` directly, which would fail even for a
  correctly-functioning reader) and the comment correctly documents why the downcast step exists.
- Each of the 4 hand-constructed-stream tests (AlphaTest/DualTexture/EnvironmentMap/Skinned) uses
  distinct, non-uniform, non-trivial-to-transpose values for every field (e.g. `DiffuseColor(0.2,
  0.3, 0.4)` vs. `EmissiveColor(0.01, 0.02, 0.03)` vs. `SpecularColor(0.5, 0.5, 0.5)` in the
  `SkinnedEffect` test) — a good, consistent practice across this file for catching field-ordering
  bugs that identical/zero values would mask.
- `BasicEffectReaderParsesRealFnaProducedBytes`'s own comment precisely documents the provenance and
  location-verification methodology for the exact byte range extracted (cross-referenced to the
  fixture's own `manifest.json`), and honestly notes `ModelReader` (which would otherwise enable a
  full `ContentManager::Load<Model>` round trip through this same shared resource) wasn't
  implemented at authoring time — this reader is instead tested against real, independently-located
  FNA-produced bytes directly, a reasonable and clearly-disclosed alternative to a full end-to-end
  load.
- The precise floating-point values asserted for the real-fixture test (e.g.
  `0.64000004529953f`, `9.607843399047852f`) are the kind of oddly-specific values that indicate
  genuine extraction from real data rather than convenient round numbers invented for the test.

## Detailed Findings
None.

## Cross-File Observations
This file's real-fixture-slice test is a good complement to `ModelContentTypeReaderTests.cpp`'s
full end-to-end load of the same `BlenderDefaultCube.xnb` fixture — the model test's own
`BasicEffect` assertions (diffuse color, alpha) independently corroborate the values this file
extracts directly from the raw byte slice, giving cross-validated confidence in the shared-resource
byte offset/length used here.

## Missing or Weak Tests
None identified — the one real-fixture-availability gap (no full round-trip through `ModelReader`
at the time of authoring) is honestly disclosed and reasonably mitigated by direct byte-slice
testing; this gap has likely since been closed by `ModelContentTypeReaderTests.cpp`'s own
subsequent full round-trip test of the same fixture, corroborating this file's real-bytes test
independently.

## Positive Findings
The defensive fixture-size assertion before extracting a hardcoded byte-offset slice is a good,
easy-to-overlook safety check that prevents a silently-wrong test if the fixture file ever changes
unexpectedly.

## Final Assessment
No findings.
