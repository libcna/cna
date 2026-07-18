# Audit: examples/easygl_spritebatch_layerdepth_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_layerdepth_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — SpriteBatch `layerDepth`/`SpriteSortMode` pixel test
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`)
- XNA/FNA relevance: exercises `SpriteBatch::Begin(SpriteSortMode::FrontToBack, ...)` and the
  `layerDepth` parameter of `SpriteBatch::Draw`, judged against FNA's `FrontToBackComparer`/
  `BackToFrontComparer` (`SpriteBatch.cs`).
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritebatch_layerdepth`
  (`EasyGL_SpriteBatch_LayerDepthOrder`); **also reused verbatim** by
  `cmake/Tests/VulkanTests.cmake` → `cna_test_vulkan_spritebatch_layerdepth`
  (`Vulkan_SpriteBatch_LayerDepthOrder`) — this file is compiled against two different backends
  from the same source, not EasyGL-exclusive despite its filename.
- Main related production file: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
  (`flushBatch()`, lines 185-216).

## Purpose

Closes the loop on a purely-CPU-side sort-order test (Tasks 415/416, using a mock/recording
backend) with a real GPU pixel test: 2 overlapping opaque 60×60 sprites are submitted in the
*wrong* visual order (`Draw(B)` then `Draw(A)`) but tagged with `layerDepth` values (`A=0.1`,
`B=0.9`) that, under `SpriteSortMode::FrontToBack`, sort them back into the *correct* draw order
(A first, B last) so B legitimately wins the overlap via simple no-depth-test painter's-algorithm
compositing.

## Executive Verdict

**Healthy.** The sort direction, the "submit in reverse order to genuinely discriminate the bug",
and the 3 sample-point derivations all check out against the real `flushBatch()` sort predicate and
FNA's own comparer semantics.

## Checklist Results

### API / XNA / FNA parity
Exercises the 8-arg `SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*,
DepthStencilState*, RasterizerState*, Effect*, Matrix)` overload (lines 97-100) and
`Draw(texture, destRect, srcRect, color, rotation, origin, effects, layerDepth)` (lines 104-107).
Confirmed `SpriteBatch::flushBatch()`'s `FrontToBack` branch (`SpriteBatch.cpp` lines 196-202) sorts
**ascending** by `layerDepth` (`a.layerDepth < b.layerDepth`), matching FNA's
`FrontToBackComparer.Compare` (`SpriteBatch.cs` lines 1612-1620:
`p1->depth.CompareTo(p2->depth)`, i.e. ascending) exactly — smaller depth values are drawn first.

### Behavioral correctness
Traced the actual overlap outcome: since CNA sprite vertices carry no Z component (confirmed by
reading `EasyGLSpriteBatchBackend::Draw`, which never writes a depth attribute — `layerDepth` is a
CPU-side sort key only, consumed by `flushBatch()`'s `std::stable_sort`, never touching vertex data)
and depth testing is not enabled for 2D SpriteBatch draws, draw order alone determines the winner of
the overlap — matching the header comment's own derivation (lines 8-11) and FNA's documented default
`DepthStencilState.None` SpriteBatch behavior.

### Logic
The test deliberately submits `Draw(*blueTex_, ...)` **before** `Draw(*redTex_, ...)` (lines 104-107)
— the exact opposite of the correct final draw order (A then B) — specifically so that a
hypothetical regression that ignores `layerDepth` and falls back to raw submission order would
produce the *opposite*, wrong winner (A on top instead of B), making this test genuinely
discriminating rather than one that would pass by coincidence under either sort semantics. Confirmed
`std::stable_sort` (not plain `sort`) is used (line 198), which matters for ties but not for this
specific 2-sprite, distinct-depth case.

### Memory/resource lifetime
`redTex_`/`blueTex_` are 1×1 `Texture2D` unique_ptrs constructed once in `Initialize()`, stretched to
60×60 via the destination rectangle — standard flat-color pixel-test idiom used throughout this
shard, no lifetime concerns.

### C++ correctness
`colourMatch` (lines 52-57) does per-channel `std::abs((int)a - (int)b) <= tol` with `tol=60` default
— safe against the `bytecs`/`uint8_t` underflow that a naive `unsigned` subtraction would risk, since
both operands are explicitly widened to `int` before subtracting.

### Performance
N/A — single-frame example.

### Thread safety
N/A — single-threaded.

### Architecture
Good separation of concerns already noted in the file's own header comment: this is explicitly
described as the GPU-pixel-level closure of an earlier CPU-only mock-backend test (Tasks 415/416) —
a sensible layered testing strategy (unit-test the sort predicate in isolation, then confirm the
real backend renders the resulting order correctly).

### Maintainability
The header comment (lines 1-30) states the design, the deliberate reversed-submission-order
rationale, and the expected pixel/label table up front — easy to audit and easy for a future reader
to understand without re-deriving the math.

### Portability
N/A — one backend build per test binary (EasyGL and, separately, Vulkan via the reused source).

### Robustness
`result_` defaults to `0` (pass) and is only set to `1` inside the per-check loop (lines 118-128) —
if `Draw()` were somehow never invoked, this would silently report success. In practice this isn't a
live risk (an uncaught exception during `Initialize()`/`Draw()` would abort the process rather than
skip straight to a clean exit), but it's a slightly less defensive default than
`easygl_spritebatch_blendstate_leak_test.cpp`'s (`result_ = 1` initially) — see Cross-File
Observations.

### Testing
This file is itself a test. See Missing or Weak Tests below for what it doesn't additionally cover.

## Detailed Findings

No correctness defects found in this file. The one item worth recording is informational, not a
defect (see Cross-File Observations).

## Cross-File Observations

- **Verbatim cross-backend reuse**: this exact `.cpp` is compiled into two separate CTest targets
  (`cna_test_easygl_spritebatch_layerdepth` and `cna_test_vulkan_spritebatch_layerdepth`) per
  `cmake/Tests/EasyGLTests.cmake` line 798 and `cmake/Tests/VulkanTests.cmake` line 745 — despite the
  `easygl_` filename prefix, the test's actual assertions are backend-agnostic (they only touch the
  shared `Microsoft::Xna::Framework::Graphics` API surface, never an EasyGL-specific type), which is
  exactly the intended shared-source-across-backends pattern this project uses to avoid duplicating
  ~40 lines of scene setup per backend. This means a genuine finding in this test (or the production
  `flushBatch()` sort logic it exercises) is not EasyGL-specific — it would equally affect Vulkan.
- `result_` defaulting to `0` (pass-by-default until a check fails) vs.
  `easygl_spritebatch_blendstate_leak_test.cpp`'s `result_ = 1` (fail-by-default) is a minor,
  pre-existing stylistic inconsistency across this shard's test files, not unique to this one file —
  worth a single project-wide note in cross-cutting findings rather than repeated per file.

## Missing or Weak Tests

- Only `SpriteSortMode::FrontToBack` is exercised here; `BackToFront` and `Texture` sort modes have
  their own (already-referenced) CPU-mock tests per the header comment, but no sibling GPU pixel test
  in this shard appears to close the loop for `BackToFront` the same way this one does for
  `FrontToBack` — worth checking during a full-shard sweep whether a `..._backtofront_test.cpp`
  equivalent exists elsewhere in the tree.

## Positive Findings

- Deliberately submits sprites in the *wrong* order to make the test genuinely discriminating rather
  than passing by coincidence — good test design.
- Sort-direction claim independently verified against both the actual `SpriteBatch.cpp` predicate and
  FNA's own `FrontToBackComparer` — both agree with the test's premise.

## Final Assessment

A well-targeted, cross-backend-reused pixel test whose sort-order premise and discriminating design
both hold up under direct inspection of `flushBatch()` and FNA's reference comparer. No defects found.
