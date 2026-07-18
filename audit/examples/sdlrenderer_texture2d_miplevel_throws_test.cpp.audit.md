# Audit: examples/sdlrenderer_texture2d_miplevel_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_texture2d_miplevel_throws_test.cpp` (130 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 681, documents/tests the deliberate decision that
  `Texture2D::SetData` with `level>0` throws on SDL_Renderer rather than silently no-oping.
- CMake registration: `cna_sdl_test(cna_test_sdl_texture2d_miplevel_throws
  examples/sdlrenderer_texture2d_miplevel_throws_test.cpp)` / `SDL_Renderer_Texture2D_MipLevelThrows` —
  confirmed at `cmake/Tests/SdlRendererTests.cmake:127-129`.
- XNA/FNA relevance: direct — `Texture2D.LevelCount`, `Texture2D.SetData(level, rect, data, startIndex,
  elementCount)`, `Texture2D.GetData` mip-level overloads (FNA `Texture2D.cs`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
  (constructor/`CalculateMipLevels`, lines 152-178; `SetData(level, rect, …)`, lines 245-316;
  `GetData(level, rect, …)`, lines 378-449); `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlTextureBackend::UpdatePixelsLevel`, lines 51-57); `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`
  (`ITextureBackend::UpdatePixelsLevel` default no-op, line 197).

## Purpose

Verifies a specific, previously-made architectural decision for this backend: since SDL3's `SDL_Texture`
has no native mip chain or per-level LOD sampling, and the shared `ITextureBackend::UpdatePixelsLevel`
interface defaults to a silent no-op, `SdlTextureBackend` was given an explicit override that *throws*
`std::runtime_error` for any `level>0` `SetData` call rather than silently discarding the write. The test
constructs a 4x4 mip-mapped `Texture2D`, checks `LevelCount==3`, then verifies: (1) `level=0` SetData/GetData
remain fully functional, (2) `level=1` SetData throws, (3) `level=1` GetData (a pure CPU-side cache read, per
the file's own cited Task 678 architecture) does **not** throw merely because SetData is unsupported at
that level.

## Executive Verdict

**Healthy** — every claim in this file's unusually detailed header comment (throw rationale, the
"CPU buffer already updated before the throw" deviation, the mip-level count) was independently traced
against the current production code and confirmed accurate, not stale.

## Checklist Results

### API / XNA / FNA parity
`LevelCount==3` for a 4x4 mip-mapped texture matches FNA's own mip-level-count convention (`Texture.LevelCount`,
confirmed present in the FNA reference tree at `Graphics/Texture.cs`) — `floor(log2(max(w,h)))+1 = 2+1 = 3`.
Independently re-derived `CalculateMipLevels(4,4)` (`Texture2D.cpp` lines 152-157): starts at `levels=1`;
iteration 1: `w=2,h=2,levels=2`; iteration 2: `w=1,h=1,levels=3`; loop condition `w>1||h>1` now false — result
`3`, exactly matching the test's own assertion (line 72). This is a case where this audit independently
re-derived the expected constant rather than trusting the test's own comment, and it checks out exactly.

### Behavioral correctness
Traced `Texture2D::SetData(int level, const Rectangle* rect, …)` (`Texture2D.cpp` lines 245-316) line by
line for the `level=1` case: no rect is passed (`nullptr`), so `x=0,y=0,w=levelW,h=levelH` covers the full
level (`mipDim(4,1)=2`, a 2x2 level); the merge loop (lines 283-294) writes into `getMipBuffer(1)`'s CPU
buffer *before* the `level==0` branch is checked; since `level!=0`, execution reaches line 312-314's
`else if (backend_) { backend_->UpdatePixelsLevel(level, buf.data(), levelW, levelH); }`, which calls
`SdlTextureBackend::UpdatePixelsLevel` (`SdlGraphicsBackend.cpp` line 51), confirmed to unconditionally
`throw std::runtime_error(...)` — exactly matching the test's `catch (const std::runtime_error&)` (line 95).
The header's own claimed "known, accepted, documented deviation" (lines 21-28) — that the CPU-side mip
buffer is already merged *before* the exception fires — was independently confirmed by reading the exact
line order in `SetData`: `getMipBuffer(level)` and the merge loop (lines 281-294) execute unconditionally
ahead of the `UpdatePixelsLevel` call that throws (lines 312-314). This is not a stale claim.

### Logic
`GetData(1, nullptr, …)`'s "must not throw" assertion (lines 101-107) was traced against
`Texture2D::GetData(int level, const Rectangle* rect, …)` (`Texture2D.cpp` lines 378-449): since `level!=0`,
it skips the `level==0 && rect==nullptr` delegation (lines 391-395) and calls `getMipBufferConst(1)`
directly (line 397) — this returns the CPU buffer populated moments earlier by the (throwing) `SetData(1,…)`
call in the previous test block, so the buffer is non-null and the function returns normally without
reaching the `gpuOnlyContent_` backend-readback branch or the final `throw std::runtime_error("no CPU-side
pixel data…")` (line 424) at all. Correctly confirms the "pure CPU-side cache read" architecture the
comment describes.

### C++ correctness
No unsafe casts, no raw pointers beyond `std::vector::data()` passed to well-bounded API calls. The
`try { … } catch (...)` blocks (lines 77-80, 103-106) correctly distinguish "any exception" from the
specific `std::runtime_error` catch used for the actual throw-under-test (line 94) — appropriately scoped:
the two "must not throw" checks use a broad `catch (...)` (correct, since *any* exception would be a
regression), while the "must throw" check narrows to `std::runtime_error` specifically (correct, since a
different exception type escaping would itself be a distinct, worth-noticing regression, though the test
would not currently distinguish that from a clean pass/fail — see F1).

### Memory/resource lifetime
`gdm_` is the only owned resource (a `unique_ptr`), released with the `Game` object; no manual GPU-handle
management in this file.

### Performance / Thread safety
N/A — one-shot CTest executable, single-threaded.

### Architecture
Correctly XNA-facing: the test never reaches into `SdlTextureBackend` or `ITextureBackend` directly; it
only observes the throw behaviour through the public `Texture2D` API, which is the right level for an
example/integration test.

### Maintainability
130 lines, single clear responsibility, no dead code. The header comment is unusually thorough
(rationale + explicitly-flagged accepted deviation + explicit statement of what is *not* affected) —
a strong positive pattern matching the density of "Task NNN finding" comments already noted as a strength
in the sibling `SdlGraphicsBackend.cpp` audit.

### Portability
`gdm_->setPreferredBackBufferWidthProperty(1)` / `Height(1)` (lines 118-119) — this test never draws or
reads back a framebuffer pixel, so it correctly does not need `PresentationMode::NativeBackBuffer` (unlike
every other file in this batch) — the omission is intentional and correct, not an oversight.

### Robustness
The three-part structure (level=0 unaffected / level>0 SetData throws / level>0 GetData does not throw)
correctly isolates three independently-failable hypotheses rather than conflating them into one assertion —
good test design.

### Testing
This file is itself the primary test for this specific decision. No gaps found for what it explicitly
claims to cover.

### Cross-file consistency
Consistent with `SdlGraphicsBackend.cpp`'s own `UpdatePixelsLevel` override and its inline comment (lines
51-57), and with `IGraphicsBackend.hpp`'s documented default no-op (line 197) that this backend deliberately
overrides rather than relies on — the whole chain of claims in the header traces cleanly end-to-end.

## Detailed Findings

### F1 — `GetData(1,…)`'s "does not throw" check does not verify the *value* read back, only the absence of an exception

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: lines 101-107 (`check(!threw, "level=1 GetData (pure CPU-side read) does not throw")`)
- Evidence: `lvl1` is initialized to `kRed` (line 102) before the call; after the call, its contents are
  never compared against any expected value (unlike the `level=0` block just above it, lines 82-87, which
  does verify `rb`'s post-call contents match red). Since the immediately-preceding `SetData(1, …)` call
  (lines 92-97) merged `blue4` into the level-1 CPU buffer *before* throwing (per the confirmed "known
  deviation"), the actual correct expected content here would be all-Blue, not the initial all-Red
  placeholder — but the test asserts neither value.
- Why it matters: a regression that corrupted the level-1 CPU buffer's *contents* (while still correctly
  not throwing) would pass this test undetected. This is a narrower, lower-severity version of the same
  "checks presence of a property but not its value" pattern flagged in this shard's sibling files.
- FNA/XNA comparison: N/A (CNA-internal architecture detail, the CPU-shadow-buffer-persists-across-a-failed-
  upload behavior is explicitly a CNA-only, self-disclosed deviation per the header comment, not an XNA
  behavior).
- Related files: none beyond this one — the fix would be local (add a value comparison to the existing
  `lvl1` array against the known-correct Blue value).
- Suggested future action (not implemented by this audit): assert `lvl1[i] == kBlue` for all 4 elements to
  make this check load-bearing rather than presence-only.

## Cross-File Observations

- This file's rationale directly corroborates the sibling `SdlGraphicsBackend.cpp` audit's own
  characterization of this backend's "throw for what can't be honored" convention (there cited via Task 676's
  `SetCustomEffect`) — this is the same discipline applied consistently to a different feature (mip uploads).

## Missing or Weak Tests

See F1 — the final `GetData` check would be strictly stronger with an explicit value assertion.

## Positive Findings

- The header comment transparently documents its own accepted architectural deviation (CPU buffer merged
  before the throw) rather than silently letting a reader discover it independently — this is the same
  standard of engineering honesty already praised in this shard's `EasyGL` sibling audits.
- `LevelCount==3` was independently re-derived by this audit (not merely trusted) and matches exactly.
- Cleanly isolates 3 distinct behavioral claims (level=0 unaffected / level>0 SetData throws / level>0
  GetData unaffected) rather than one conflated assertion.

## Final Assessment

A well-reasoned, accurately-commented test for a genuine architectural decision, verified line-by-line
against the current production code with no stale claims found. One low-severity test-strength gap (F1)
is the only opportunity for improvement.
