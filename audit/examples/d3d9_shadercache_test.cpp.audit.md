# Audit: examples/d3d9_shadercache_test.cpp

## Metadata

- Source file: `examples/d3d9_shadercache_test.cpp` (125 lines)
- Audit status: AUDITED (static/source-reading only — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — `D3D9ShaderCache` live-device shader creation/caching
- File type: standalone `Game`-subclass executable, CTest-registered as `D3D9_ShaderCache`
  (`cna_test_d3d9_shadercache`, `cmake/Tests/D3D9Tests.cmake:105-108`, `TIMEOUT 60 LABELS "D3D9"`;
  comment there notes this needs both DXVK and the real compiler's embedded bytecode output at once).
- XNA/FNA relevance: indirect — this exercises CNA's own NOXNA shader-object cache
  (`D3D9ShaderCache`), not a public XNA API, but the 66 shaders it creates are the vendored,
  verbatim-from-FNA XNA Stock Effect HLSL bytecode (`shaders/xna/**`, exempt per D-5) compiled ahead
  of time by a separate task (D9-71) and embedded in `shaders/d3d9_shaders.hpp`.
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9ShaderCache.cpp` (84 lines, the
  entire feature), `include/.../D3D9ShaderCache.hpp` (56 lines), `shaders/d3d9_shaders.hpp` (the
  4569-line embedded-bytecode manifest, `kAllShaders[]`).

### Environment Note (per D-P4)

D3D9 is Windows-only and cannot be built or run in this Linux sandbox. This report is static/
source-reading only: the "42 vertex + 24 pixel = 66" claim was independently re-verified by counting
`kAllShaders[]`'s own entries directly (`grep -c ", true}"`/`", false}"` against
`shaders/d3d9_shaders.hpp`), not merely trusted from the test's own comment.

## Purpose

4-check proof that `D3D9ShaderCache` genuinely creates and caches real `IDirect3DVertexShader9`/
`IDirect3DPixelShader9` objects through a live D3D9 device: (A) `CreateAllEXT()` creates all 66
embedded shaders with zero failures; (B) a second lookup for the same name returns the *identical*
cached pointer (not a freshly-recreated object) and the cached counts do not grow; (C) an unknown
shader name throws a real, named error rather than returning null or silently creating garbage;
(D) the lookup is stage-aware — a real vertex-shader name requested via `GetPixelShader()` (and vice
versa) throws too, rather than silently matching by name alone across D3D9's separate vs/ps object
address spaces.

## Executive Verdict

**Healthy** — every check's claim was verified directly against `D3D9ShaderCache.cpp`'s actual
implementation and independently re-counted against the real embedded-shader manifest; no gaps or
discrepancies found.

## Checklist Results

### Behavioral correctness

- Check A: independently counted `shaders/d3d9_shaders.hpp`'s `kAllShaders[]` (lines 4499-4566) —
  exactly 42 entries with `isVertexShader=true` and 24 with `false` (`grep -o ", true}"` / `", false}"`
  → 42/24), matching the test's asserted `42`/`24` exactly and matching `kAllShadersCount=66`
  (line 4567). `D3D9ShaderCache::CreateAllEXT()` (`D3D9ShaderCache.cpp:76-83`) iterates every entry
  and calls `GetVertexShader`/`GetPixelShader` per its own `isVertexShader` flag — a real, exhaustive
  creation pass, not a stub that just counts entries without creating anything.
- Check B: `GetVertexShader`/`GetPixelShader` (`D3D9ShaderCache.cpp:34-74`) both check
  `vertexShaders_.find(name)`/`pixelShaders_.find(name)` first and return the cached `.Get()` pointer
  immediately if present (lines 36-37, 57-58) — confirmed a second call for the same name never
  reaches `CreateVertexShader`/`CreatePixelShader` again, so `vs1==vs2`/`ps1==ps2` and the cached
  counts (`vertexShaders_.size()`/`pixelShaders_.size()`) genuinely cannot grow from a repeated
  lookup of an already-cached name.
- Check C: `FindEmbeddedShader(name, wantVertexShader)` (lines 18-26) linearly scans `kAllShaders`
  and returns `nullptr` if no entry matches both `name` AND the requested stage — `GetVertexShader`/
  `GetPixelShader` both throw `std::runtime_error` immediately when `found==nullptr` (lines 40-41,
  61-62), confirmed before any `CreateVertexShader`/`CreatePixelShader` call is attempted.
- Check D: `FindEmbeddedShader`'s own match condition is `s.isVertexShader == wantVertexShader &&
  name == s.name` (line 22) — a real vertex-shader name (`"BasicEffect_VSBasic"`) requested via
  `GetPixelShader("BasicEffect_VSBasic")` calls `FindEmbeddedShader(name, /*wantVertexShader=*/false)`,
  which will never match that entry's `isVertexShader=true` flag even though the *name* matches —
  correctly falls through to `nullptr`/throw, confirming the lookup is genuinely stage-aware rather
  than name-only. Symmetric for the reverse case.

### Logic

`FindEmbeddedShader`'s linear scan over 66 entries per lookup is simple and correct; not indexed by a
hash map keyed on `(name, stage)`, but performance is not a concern for a one-time-per-shader-variant
lookup cost (66 entries max, called at most once per distinct name since results are cached
thereafter) — see Performance below.

### C++ correctness

`vertexShaders_.emplace(name, std::move(shader))` (line 51) followed by returning `raw` (captured
*before* the move, line 50) is the correct pattern to avoid using `shader` after it has been moved
from — confirmed `raw` is captured from `shader.Get()` prior to the `std::move`, not read from the
(now-moved-from) `shader` variable afterward. Same pattern in `GetPixelShader` (lines 71-73).

### Memory/resource lifetime

`ComPtr<IDirect3DVertexShader9>`/`ComPtr<IDirect3DPixelShader9>` values stored in the
`unordered_map`s provide correct RAII release on cache destruction or `unordered_map` node removal
(never done in this class's own API, so effectively cache-lifetime-scoped) — no leak or
double-release risk visible.

### Performance

`FindEmbeddedShader`'s O(66) linear scan per uncached lookup is "theoretical, not likely-significant"
per the checklist's own performance categories — `CreateAllEXT()` performs 66 such scans total
(one per entry, each an O(66) linear search before creation) i.e. up to `66*66≈4356` string
comparisons at startup — negligible even for a one-time startup cost, and the class's own header
comment (`D3D9ShaderCache.hpp:8-10`) explicitly justifies the name-keyed design over a hand-written
enum. Not flagged as a defect.

### Architecture

`D3D9ShaderCache` is correctly scoped as an internal backend helper (`CNA::Internal::Backends::D3D9`
namespace), consumed by `D3D9EffectDraw.cpp`'s `DrawBasicEffectEXT` (confirmed via
`shaderCache_->GetVertexShader(...)`/`GetPixelShader(...)` calls at
`D3D9EffectDraw.cpp:657-659` during the audit of `d3d9_shaderdispatch_test.cpp`'s production call
chain) — a real, live consumer beyond this test, not dead code.

### Testing

All 4 public methods (`GetVertexShader`, `GetPixelShader`, `CreateAllEXT`, and implicitly the two
count getters) are exercised. Not tested: behavior when `CreateVertexShader`/`CreatePixelShader`
itself fails (`FAILED(hr)` branch, lines 46-48/67-69) — this would require a way to force a real
device-level shader-creation failure, which is reasonably out of reach for a test at this level
(would need a corrupted bytecode blob or a lost/invalid device, neither of which this test attempts).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `CreateAllEXT()`'s failure-handling behavior for a mid-sweep `CreateVertexShader`/`CreatePixelShader` failure is untested

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `D3D9ShaderCache::CreateAllEXT()` (`D3D9ShaderCache.cpp:76-83`) has no
  try/catch of its own — a failure partway through the sweep (e.g. entry 30 of 66 fails) would
  propagate as an exception out of `CreateAllEXT()`, leaving the cache with the first 29 shaders
  already created and cached, and the sweep abandoned. This is a defensible design (the class's own
  header comment frames `CreateAllEXT()` as "eagerly creates every one of the 66 embedded shaders...
  the real per-task verification this row's own plan text asks for," i.e. a warm-cache convenience
  meant to fail loudly, not swallow errors) but is not exercised by this test at all — Check A only
  proves the all-succeed path.
- Why it matters: if a future embedded-bytecode regression corrupted one shader's bytecode blob, this
  test would correctly still fail Check A (the count assertion), but would not distinguish "one
  specific named shader failed" from "the whole cache is broken" without additional diagnostics.
- Suggested future action (not implemented by this audit): not critical enough to require a fix; a
  future enhancement could have `CreateAllEXT()` collect and report all failing names rather than
  throwing on the first, but this is a design preference, not a defect.

## Cross-File Observations

- `kAllShadersCount=66` (`d3d9_shaders.hpp:4567`) is a hardcoded literal alongside the actual
  66-element `kAllShaders[]` array — if a future entry were added/removed without updating this
  constant, `kAllShadersCount` and `std::size(kAllShaders)` would silently disagree; this file's own
  Check A (asserting `42`/`24` against the *live* cache's own counted creations, not against
  `kAllShadersCount` directly) is actually a more robust check than trusting the hardcoded constant,
  since it independently proves the real count via actual creation rather than reading the same
  literal back.
- `git log --oneline -- examples/d3d9_shadercache_test.cpp` shows a single authoring commit
  (`2082aa73 feat(plans/plan_dx9.md): close D9-74 -- D3D9ShaderCache creates all 66 shaders live -- Phase
  D9-7 COMPLETE`), consistent with the file's own single-task scope.
- This test's Check D (stage-aware lookup) is a good complement to `d3d9_shaderdispatch_test.cpp`'s
  own exhaustive name-sweep — together the two files prove both "the name tables are transcribed
  correctly" (`d3d9_shaderdispatch_test.cpp`) and "the names they produce actually resolve to real,
  correctly-staged GPU shader objects" (this file).

## Missing or Weak Tests

See F1 (mid-sweep failure handling untested — reasonable, not a priority).

## Positive Findings

- The independently-recomputed manifest count (`grep`-counted 42 vertex + 24 pixel from the raw
  `.hpp` source) matches the test's asserted values exactly, corroborating both the test and the
  production data it depends on.
- Move-then-use-captured-raw-pointer pattern in `GetVertexShader`/`GetPixelShader` is correct and
  avoids a use-after-move hazard that would otherwise be an easy mistake in this exact shape of code.
- Check D specifically targets a real, D3D9-specific hazard (vertex and pixel shader objects occupy
  entirely separate D3D9 type/register spaces, so a name-only cache lookup could otherwise silently
  return semantically-wrong results) rather than a generic "unknown key" case alone.

## Final Assessment

A tight, correctly-reasoned test with an accurately-transcribed expected shader count, verified
against the real embedded-bytecode manifest by this audit independently (not merely trusted). No
defects found in the production `D3D9ShaderCache` class.
