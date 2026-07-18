# Audit: examples/sdlrenderer_spritebatch_texture_sort_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_texture_sort_test.cpp` (158 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteSortMode::Texture` reorder-and-rebind pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_spritebatch_texture_sort` /
  `SDL_Renderer_SpriteBatch_TextureSort`, `cmake/Tests/SdlRendererTests.cmake:56-58`), introduced by
  `0e1918cf`/`f27356a5` ("test(Task 668): verify SpriteSortMode::Texture grouping on SDL_Renderer").
- XNA/FNA relevance: direct — `SpriteSortMode.Texture`'s grouping-by-texture-reference contract.
- FNA reference: FNA's own equivalent sort is by texture reference for `SpriteSortMode.Texture` (grouping to
  minimize GPU bind-state changes; exact tie-break order is not part of the documented contract).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`flushBatch()`,
  lines 185-216; `pushSprite`/`flushSingle`, lines 145-183), `src/CNA/Internal/Backends/SdlRenderer/
  SdlGraphicsBackend.cpp` (`SdlSpriteBatchBackend::Draw` overloads, lines 124-210).

## Purpose

`SdlSpriteBatchTextureSortTest` (lines 66-151) draws 4 non-overlapping 60x60 sprites in a deliberately scrambled
"Red, Blue, Red, Blue" submission order (lines 107-114), each using one of two 1x1 solid-colour textures
(`redTex_`/`blueTex_`, lines 83-89), with `sb_->Begin(SpriteSortMode::Texture, ...)`. After `End()` triggers the
texture-grouping reorder, it samples the centre of each of the 4 destination rectangles and asserts each still
shows its *originally assigned* colour (lines 118-136) — i.e. that whichever order `SpriteBatch` reorders the
draws into, the SDL backend still binds the correct texture for each individual reordered call.

## Executive Verdict

**Healthy.** The test is honestly scoped (its own header comment explicitly says it is *not* trying to predict or
assert pointer-sort order, deferring that half of the contract to the mock-backed `SpriteBatchTests.cpp` unit
test) and it independently verified, by reading `flushBatch()`, that the actual sort key really is
`a.texture < b.texture` (a raw pointer comparison) — matching the header comment's claim precisely.

## Checklist Results

### API / XNA / FNA parity

`SpriteSortMode::Texture`'s dispatch in `flushBatch()` (`SpriteBatch.cpp:203-209`) is
`std::stable_sort(spriteQueue_.begin(), spriteQueue_.end(), [](a,b){ return a.texture < b.texture; })` — a raw
`const Texture2D*` pointer comparison, exactly as the file's header comment states. This is consistent with the
general XNA/FNA design intent of `SpriteSortMode.Texture` (group draws by texture to reduce bind-state changes);
the specific tie-break/ordering is runtime-pointer-dependent in both FNA and CNA, so this test correctly avoids
asserting on it and instead asserts on the property that *is* part of the contract: each reordered draw call must
still carry its own correct texture/destination pairing.

### Behavioral correctness

Verified `SdlSpriteBatchBackend::Draw` (the 3-arg source/dest overload actually used here, `SdlGraphicsBackend.cpp`
lines 143-183) independently fetches `texture.GetNativeTexture()` on every call (line ~152) rather than caching a
"last bound" texture pointer across draws — so a `stable_sort`-reordered sequence cannot accidentally reuse a stale
native handle from the previous call in the reordered sequence. This is exactly the failure mode a naive
cache-and-reuse implementation could introduce, and the test's 4-destination, 2-texture, interleaved-submission
layout (each destination rect isolated, non-overlapping) is the right minimal shape to catch a texture/position
mismatch if one existed.

### Logic

`colourMatch` (lines 56-61) uses a `tol=60` per-channel tolerance and only checks R/G/B (not alpha) — reasonable
for a solid opaque-fill test where the only two possible "wrong" answers (red vs. blue, or background black) are
far apart in colour space; a 60-tolerance cannot confuse red/blue/black with each other here.

### Memory/resource lifetime

`redTex_`/`blueTex_` are each a `std::make_unique<Texture2D>(dev, 1, 1)` with one `SetData` call in `Initialize()`
and are kept alive for the whole `Draw()` call (single-shot, `done_` guard at line 94) — no lifetime concerns.

### C++ correctness

No unsafe casts; `const_cast<SamplerState*>(&SamplerState::PointClamp)` (line 103) is the same established pattern
seen throughout this shard for `SpriteBatch::Begin`'s non-const `SamplerState*` parameter — a pre-existing API
surface characteristic of `SpriteBatch::Begin`, not something this test introduces or should be blamed for.

### Performance

N/A — a single-frame diagnostic executable, not a hot path.

### Thread safety

N/A — single-threaded test executable, consistent with the rest of this shard.

### Architecture

Correctly scoped: the file's header explicitly delegates the "does the sort predictably order by pointer" question
to the existing mock-backend unit test (`SpriteBatchTests.cpp`'s `TextureGroupsDrawsByTextureAndPreservesGroupOrder`,
Task 414) and keeps this pixel test focused on the one thing a mock backend cannot verify: real SDL texture binding
after reorder. This is a good instance of avoiding duplicate/overlapping test responsibility across the codebase.

### Maintainability

158 lines, single responsibility, clear naming (`kRed`/`kBlue`, per-check `label` strings for `printf` diagnostics).

### Portability

Requires `PresentationMode::NativeBackBuffer` (line 147) — correctly justified by the file's own comment citing
Task 915's finding that `SDL_RenderReadPixels` operates in physical coordinates while the backend's default
`FixedHeightDynamicWidth` presentation mode does not map 1:1 to physical pixels; this reasoning was independently
corroborated in the `SdlGraphicsBackend.cpp` audit's own discussion of `ReadBackbuffer`.

### Robustness

`result_` is set to a nonzero value (not aggregated as pass/fail counts) as soon as any single check fails (line
135), and the process `Exit()`s cleanly either way (line 138) with the specific PASS/FAIL line for each of the 4
checks printed to stdout for diagnosability.

### Testing

This file itself is a test; its own coverage is discussed above. It is a genuine, non-redundant addition alongside
the mock-backend `SpriteBatchTests.cpp::TextureGroupsDrawsByTextureAndPreservesGroupOrder` — verified by reading
`flushBatch()`'s real sort predicate rather than merely trusting the header comment's characterization of it.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW/INFO observation:

### F1 — Test cannot detect a specific-but-narrow regression: two same-texture draws at overlapping destinations

- Severity: INFO
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: whole-file scope (4 non-overlapping destinations, exactly 2 distinct textures)
- Evidence: all 4 destination rectangles are disjoint (`(100,100)`,`(200,100)`,`(100,200)`,`(200,200)`, each
  60x60, so no overlap) and there are exactly 2 distinct texture identities.
- Why it matters: this is a deliberate, reasonable simplification (the file's header explains why: results must
  not depend on which texture group's pointer sorts first) — flagged here purely as an observation, not a defect;
  a *stress* variant with 3+ textures and interleaved z-overlap would exercise more of `stable_sort`'s stability
  guarantee (same-texture submission-order preservation within a group) end-to-end through the real backend, which
  today only the mock-backend unit test verifies.
- Suggested future action (not implemented by this audit): none required; noted as a possible (optional) coverage
  extension, not a gap that undermines this file's own stated goal.

## Cross-File Observations

- Corroborates, rather than merely repeats, the `SdlGraphicsBackend.cpp` audit's positive finding (Task 705) that
  `SdlSpriteBatchBackend::Draw`'s overloads always reach the texture via the virtual `GetNativeTexture()` per call
  — this test is effectively a live, pixel-level regression guard for exactly that finding remaining true.
- `SpriteBatch.cpp`'s `flushBatch()` sort predicate (`a.texture < b.texture`, a `<` on `const Texture2D*`) is
  technically comparing addresses of two `Texture2D` value-type instances (`redTex_`/`blueTex_` here are
  heap-allocated via `unique_ptr`, so this is well-defined pointer comparison, not comparing objects on the stack
  that could otherwise have unspecified relative order in different translation units — not an issue in practice
  here, but worth the `xna-graphics`/`tests-xna-graphics` shard confirming no code path ever compares pointers to
  two stack-local `Texture2D`s where `<` between unrelated objects would be unspecified behavior pre-C++20 /
  implementation-defined in practice).

## Missing or Weak Tests

None beyond the optional F1 stress-variant extension noted above.

## Positive Findings

- Textbook example of avoiding duplicate test responsibility: explicitly reads and cites the existing mock-backend
  unit test covering the "sort key + stability" half of the contract, and scopes itself to the complementary
  "real backend texture-binding correctness" half.
- Correctly justifies its `PresentationMode::NativeBackBuffer` requirement with a concrete, previously-confirmed
  Task/finding reference rather than an unexplained magic setting.

## Final Assessment

A well-scoped, correctly-targeted pixel test; its numeric/positional claims and its characterization of
`flushBatch()`'s sort predicate were both independently verified against the actual production source.
