# Audit: examples/d3d12_smoke_test.cpp

## Metadata
- Source file: `examples/d3d12_smoke_test.cpp` (5239 lines; representative sampling — full header
  comment (Checks A-BB), the fog on/off section (Check V/DX-113, lines 3082-3176), the
  `SpriteFont`/`SpriteEffects` sections (targeted `grep` sweep across the whole file), and the tail
  (custom `ShaderEffect`/`SpriteBatch` section, DX-121, plus the final pass/fail tally))
- Audit status: AUDITED (representative sampling of this exceptionally large, single-file backend
  smoke test)
- Subsystem: `examples-tests-d3d12` shard
- File type: standalone real-GPU integration-test executable (non-`Game`, manual device/queue/
  command-list setup; Wine+DXVK/Proton+VKD3D)
- XNA/FNA relevance: exercises the full public `GraphicsDevice`/`SpriteBatch`/`SpriteFont`/Effects
  API surface against the D3D12 backend's real device/command-list/descriptor-heap/pipeline-state
  pipeline

## Purpose
The D3D12 counterpart to `d3d11_smoke_test.cpp`: real `ID3D12Device`/command-queue/descriptor-heap/
command-allocator/fence construction, `D3D12ResourceStateTracker`/`D3D12RootSignatureCache`/
`D3D12PipelineStateCache`, vertex/index/texture buffer round-trips, device recreation, and the same
full family of shader-variant draws (colored/textured/lit/alpha-test/dual-texture/skinned/
instanced/env-map), occlusion queries, custom `ShaderEffect`, `SpriteBatch`, fog, and render
targets/MRT — around 90+ individually-labeled checks (`A` through `NN`).

## Executive Verdict
Same exceptional rigor as its D3D11 twin, and explicitly self-aware of that relationship: several
sections' own comments cite the corresponding D3D11 check by name (e.g. Check V's header explicitly
states it's "closing the same real gap D3D11's own DX-81 audit found and fixed," reusing the exact
same fixture/methodology). This inherited-by-design relationship also means this file inherits the
identical MEDIUM test-design gap already identified in `d3d11_smoke_test.cpp`'s fog tests (see
below) — not a new, independently-introduced bug, but the same blind spot propagated intentionally
via reuse.

## Checklist Results
- Check L (`RecreateDeviceEXT()`, DX-110) verifying the full device/queue/heaps/command-list/fence
  set is genuinely torn down and rebuilt (not merely re-initialized in place) is a real device-loss-
  recovery proof, mirroring similar device-loss handling audited elsewhere in the D3D backends.
- Check H (`D3D12RootSignatureCache`) and Check I (`D3D12PipelineStateCache`) both verify identity-
  vs-distinctness for repeated/differing cache keys — consistent with the same cache-correctness
  testing pattern already seen in D3D11's `D3D11SamplerCache`/`D3D11InputLayoutCache` checks.
- `SpriteEffects` usage across the whole file (`grep`-confirmed, lines 2075/2121/4991/4998) only
  ever uses `None`, `FlipHorizontally`, or `FlipVertically` individually — never the combined 4th
  value (`FlipHorizontally|FlipVertically`) — so, like its D3D11 twin, this file does not exercise
  the confirmed HIGH `DrawString()` axis-direction out-of-bounds-stack-read bug.
- The `SpriteFont`-glyph-placement tests (KK2-KK5, lines ~4931-5008) construct their `SpriteFont`
  directly from explicit `chars`/glyph-bounds data, not exercising the `defaultCharacter`-absent-
  from-character-set fallback path — so, again like D3D11, this file does not exercise the
  confirmed HIGH invalid-iterator-dereference bug either.
- NN0/NN1 (custom `ShaderEffect` through `SpriteBatch::Begin(..., effect)`, DX-121) is explicitly
  noted as previously blocked by `GraphicsDevice`'s constructor unconditionally creating a real
  window (crashing for D3D12 outside a Proton-managed launch) until `PresentationParameters::
  HeadlessEXT` (commit `b3289ac6`) removed that blocker — an honest, specific account of a real
  historical blocker and its actual fix commit.

## Detailed Findings

### MEDIUM — The fog test (Check V/DX-113) inherits D3D11's identical World=View=Projection=Identity blind spot, by explicit design
This file's Check V (lines 3082-3176) is a near-verbatim reuse of `d3d11_smoke_test.cpp`'s Check AC
fog test, and its own header comment says so explicitly ("closing the same real gap D3D11's own
DX-81 audit found and fixed... same fixture as D3D11's own Check AC"). As with the D3D11 original,
every fog draw call here (lines 3155-3156, 3166-3167) passes `Matrix::getIdentityProperty()` for
World, View, *and* Projection — so object-space Z, view-space Z, and clip-space Z are numerically
identical throughout, and this test cannot distinguish a correct view-space-Z fog implementation
from the already-confirmed-elsewhere (EasyGL) object-space-only fog defect. This is the same MEDIUM
finding as `d3d11_smoke_test.cpp.audit.md`, inherited here via the file's own stated intentional
reuse of that fixture, not an independently-discovered second instance.

## Cross-File Observations
- Directly mirrors `d3d11_smoke_test.cpp`'s fog-test World/View-identity gap (see Detailed
  Findings) — both D3D backends' fog tests share the identical blind spot, tracing back to the same
  original fixture design.
- Also mirrors `d3d11_smoke_test.cpp`'s non-exercise of the two confirmed `SpriteFont`/`SpriteBatch`
  HIGH bugs (`SpriteEffects`' combined-flag value, `defaultCharacter` fallback) — neither D3D backend
  smoke test reaches those code paths.
- DX-121's `PresentationParameters::HeadlessEXT` unblocking note is a useful, concrete cross-
  reference for anyone investigating why some D3D12 tests were historically deferred.

## Missing or Weak Tests
Same as `d3d11_smoke_test.cpp`: the fog tests' reliance on identity World/View/Projection matrices
is the one identified gap, inherited from the shared original design rather than unique to this
file.

## Positive Findings
The explicit, named cross-referencing of D3D11's own check numbers/task IDs throughout this file
(rather than silently re-deriving an equivalent test from scratch) is good practice — it makes the
intentional-reuse relationship, and therefore the shared blind spot identified above, traceable
rather than hidden.

## Final Assessment
One MEDIUM finding, inherited from `d3d11_smoke_test.cpp` by explicit, disclosed design: the fog
test's identity World/View/Projection matrices cannot discriminate correct view-space fog from the
already-confirmed EasyGL object-space-only defect. Otherwise consistent with its D3D11 twin's
exceptional rigor; no other findings in the sampled portion.
