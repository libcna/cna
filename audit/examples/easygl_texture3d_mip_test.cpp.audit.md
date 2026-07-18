# Audit: examples/easygl_texture3d_mip_test.cpp

## Metadata

- Source file: `examples/easygl_texture3d_mip_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `Texture3D` mip-level data round-trip test
- File type: C++ example/integration-test executable (`Texture3DMipTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::Texture3D` (`Texture3D.cpp`/`.hpp`,
  `SetData`/`GetData` 10-arg overloads), `CNA::Internal::Backends::EasyGL::EasyGLTexture3DBackend`
  (`EasyGLGraphicsBackend.cpp` lines 66-126, 176-203)
- XNA/FNA relevance: exercises `Texture3D.SetData<T>`/`GetData<T>` (10-arg box overload) and the
  mip-level-count rule from FNA's `Texture3D` constructor (`LevelCount = mipMap ? CalculateMipLevels(Width,
  Height) : 1` — depth excluded), judged against `FNA/src/Graphics/Texture3D.cs`.
- Main related tests: this file (Tasks 862/864, per header); sibling files in this same batch cover
  z-slices (Task 173) and partial-box placement (Tasks 273/274).

## Purpose

Verifies that `Texture3D::SetData`/`GetData` round-trip correctly across all three mip levels of a 4×4×4
volume created with `mipMap=true`: level 0 (4×4×4, 64 texels, Red), level 1 (2×2×2, 8 texels, White), level 2
(1×1×1, 1 texel, Orange). Placement under `examples-tests-easygl` matches the shard convention for backend
pixel/data integration tests.

## Executive Verdict

**Healthy** — the test correctly exercises the actual mip pre-allocation logic in
`EasyGLTexture3DBackend`'s constructor and its `SetData`/`GetData` per-level-and-slice implementation, its
math for level count and per-level dimensions matches both `Texture3D.cpp`'s `CalculateMipLevels(w,h)` and
FNA's own w/h-only (not depth) mip-level rule, and its write-then-read-everything ordering is a deliberate,
correctly-reasoned defense against level-aliasing bugs. No correctness defect found; only a couple of narrow,
low-severity coverage gaps.

## Checklist Results

### API / XNA / FNA parity
`Texture3D(device, 4, 4, 4, /*mipMap=*/true, SurfaceFormat::Color)` and the 10-arg `SetData`/`GetData(level,
left, top, right, bottom, front, back, data, startIndex, elementCount)` overloads used here (lines 73-75,
80, 90, 100) are real XNA members with correct signatures per `Texture3D.cpp`/`.hpp`. `getRProperty()` /
`getGProperty()` / `getBProperty()` used in `colourEq()` (lines 38-43) are the established
CNA C#-property-getter convention.

### Behavioral correctness
Confirmed `Texture3D.cpp`'s `CalculateMipLevels(w,h)` (lines 24-29) only considers width/height, matching
FNA's `Texture3D` constructor comment in the test header exactly (verified against
`FNA/src/Graphics/Texture3D.cs` — FNA delegates level count entirely to `FNA3D_CreateTexture3D` with a
`LevelCount` computed the same width/height-only way as `Texture.CalculateMipLevels`). For a 4×4×4 volume this
correctly yields 3 levels (4→2→1), matching the test's own three `SetData`/`GetData` calls at levels 0/1/2
with box sizes 4×4×4, 2×2×2, 1×1×1 respectively (lines 73-75, 80, 90, 100).

Traced `EasyGLTexture3DBackend`'s constructor (`EasyGLGraphicsBackend.cpp:81-108`): it pre-allocates GPU
storage for **every** mip level via `set_image_3d` in a loop (lines 90-103), with an explicit comment noting
this is required because `set_sub_image_3d` (used by `SetData`, called via `glTexSubImage3D`) needs the
target level to already have a defined image — without this loop, `SetData(level>0, ...)` would silently
no-op (the exact bug class the comment cites from Task 276's `TextureCube` precedent). This means the test's
level-1 and level-2 `SetData` calls (lines 74, 75) are genuinely exercising a real pre-allocation path, not
one that happens to work by accident.

`GetData` (`EasyGLGraphicsBackend.cpp:176-203`) attaches the requested mip `level`/slice directly via
`fbo.attach_texture_layer(..., tex_, level, slice)` and `glReadPixels` — this correctly reads back the
specific mip level requested, and since it addresses the texture object directly (not "whatever's currently
bound"), there is no dependency on a prior `bind()` call, so no missed-bind risk exists here.

### Logic
The test's own header (lines 10-16) documents that "All writes happen first; only then is every level read
back and verified." This ordering is a real design decision, not incidental: writing level 0 (Red), then
level 1 (White), then level 2 (Orange), and only *afterward* reading level 0 back and checking it is still
pure Red would catch a level-aliasing bug (e.g., if the backend's per-level storage for levels 1/2
accidentally overlapped or fed back into level 0's texels) that a per-level "write-then-immediately-read"
approach would risk missing if the aliasing corrupted a *later*-written level instead of an earlier one.
Given the actual write order used (0, 1, 2) and read order (0, 1, 2), this specifically catches
"a later SetData corrupts an earlier level," which is the more common bug shape for a naively-shared mip
storage layout.

### Memory/resource lifetime
`std::vector<Color>` buffers (`red64`, `white8`, `orange1`, and the three readback vectors) are all local,
correctly sized to their exact `elementCount`, with well-defined lifetimes bounded to `Initialize()`. No
dangling-pointer or lifetime risk.

### C++ correctness
`colourEq()` (lines 38-43) compares only R/G/B, silently ignoring Alpha for every comparison in this file —
see Missing or Weak Tests. `check()` (lines 50-57) is a straightforward pass/fail accumulator that sets
`result_ = 1` on any failure without early-exiting, so all 73 per-texel checks run and print regardless of
earlier failures — correct for maximizing diagnostic output from a single run.

### Performance
N/A — this is a one-shot `Initialize()`-only test (no `Draw()` work); `Draw()` is an empty override (line
107).

### Robustness
No malformed-input path is exercised (deliberately — this is a positive-path round-trip test); all box
arguments are internally consistent with the volume's real dimensions at each level, so no exception path in
`Texture3D::SetData`/`GetData` is (or needs to be) exercised here.

### Testing
This file is itself a test; see Missing or Weak Tests for coverage gaps within it.

## Detailed Findings

No HIGH/CRITICAL findings.

### F1 — `colourEq()` never checks the Alpha channel

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `colourEq()` (lines 38-43), every `check()` call site (lines 85, 95, 101)
- Evidence: `colourEq()` compares `getRProperty()`/`getGProperty()`/`getBProperty()` only; Alpha is never
  read or compared, even though every `Color` constant used (`kRed`, `kWhite`, `kOrange`, `kGray`) sets Alpha
  to 255 explicitly.
- Why it matters: a backend regression that corrupted only the Alpha channel of a `SurfaceFormat::Color`
  volume texture round-trip (e.g., a channel-order swap that happened to leave RGB untouched, or an alpha
  clear-to-zero bug on `SetData`) would pass this test undetected. This gap is shared by every other
  `easygl_texture3d_*` file in this batch (`colourEq` is redefined identically in each), so it is a systemic,
  not file-specific, coverage hole.
- FNA/XNA comparison: N/A (test-authoring gap, not an FNA behavior question).
- Suggested future action: extend `colourEq()` (or add a second comparison helper) to include Alpha, at least
  in one of the sibling Texture3D test files, to close this gap once across the group.

## Cross-File Observations

- `colourEq()`, the `check()`/`result_` pass-fail-accumulator pattern, and the `Texture3DMipTest` class shape
  (private `gdm_`, `result_`, `check()`, public `getResult()`) are duplicated near-verbatim across all four
  `easygl_texture3d_*` files in this batch rather than factored into a shared test-utility header — consistent
  with this codebase's established per-file example/test convention (no shared test-support header exists for
  this shard), not a defect, but worth noting alongside the shared Alpha-blind-spot in F1 since a single fix
  to a shared helper would otherwise have closed the gap for all four files at once.
- `Texture3D.cpp`'s `SetData`/`GetData` box-bounds validation (`left>=0 && left<right`, etc., no upper bound
  against the texture's actual per-level width/height/depth) was confirmed to match FNA's own `Texture3D.cs`
  `GetData<T>` validation almost verbatim (`(left<0||left>=right)||...`) — FNA likewise never validates the box
  against the real level dimensions. This is a faithful parity choice, not a CNA-introduced gap; noted here
  since it means an out-of-range box (right/bottom/back larger than the mip level's real size) would not throw
  in either implementation — a latent, FNA-shared robustness gap, not exercised by this test's own inputs
  (which are always exactly the mip level's real dimensions).

## Missing or Weak Tests

- Alpha channel is never verified (F1).
- No case exercises a non-cubic volume where `CalculateMipLevels(w,h)`'s level count is smaller than what
  would be needed for `depth` to reach 1 (e.g., a 4×4×16 volume with `mipMap=true` produces only 3 levels,
  leaving level 2's depth at 4, never 1) — this is documented, intentional FNA-matching behavior per the
  file's own header, but no test in this batch exercises the asymmetric-dimension case to confirm the
  per-level depth halving (`EasyGLGraphicsBackend.cpp:100-102`) behaves correctly when it *doesn't* bottom out
  at 1 in lockstep with width/height.
- No negative/error-path test for this file's own scenario (e.g., `SetData` at a mip level beyond
  `LevelCount`) — reasonable scope limit for a "happy path" round-trip test, but worth flagging since no
  sibling file in this batch covers it either.

## Positive Findings

- Independently re-derived `CalculateMipLevels(w,h)` in both `Texture3D.cpp` and
  `EasyGLTexture3DBackend`'s constructor and confirmed both match each other and FNA's own width/height-only
  mip-level rule exactly for this test's 4×4×4 case.
- The write-everything-then-read-everything ordering (explicitly called out in the file's own header) is a
  genuinely reasoned defense against level-aliasing bugs, not an arbitrary structure — confirmed the backend's
  own per-level pre-allocation loop is the exact mechanism this ordering is designed to catch a regression in.
- All 73 individual texel checks (64 + 8 + 1) are printed with per-texel PASS/FAIL labels, giving precise
  failure localization rather than a single aggregate pass/fail.

## Final Assessment

A correctly-targeted, evidence-based mip-level round-trip test whose per-level dimensions and level count are
independently verified against both `Texture3D.cpp` and FNA's reference behavior; its write-then-read-all
ordering is a deliberate, well-reasoned defense against a real class of level-aliasing bug. The only gaps
(Alpha never checked, non-cubic mip-depth interaction untested) are low-severity and shared systemically
across the shard's `Texture3D` test files rather than unique to this one.
