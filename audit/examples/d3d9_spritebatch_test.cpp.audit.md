# Audit: examples/d3d9_spritebatch_test.cpp

## Metadata

- Source file: `examples/d3d9_spritebatch_test.cpp` (486 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note)
- Subsystem: `examples-tests-d3d9` shard — the real D3D9 `SpriteBatch` backend, tested through the
  public `SpriteBatch`/`Texture2D` API (`plans/plan_dx9.md` D9-9, D9-90 through D9-93), `Game`-subclass,
  CTest-registered, real device/window path.
- XNA/FNA relevance: direct — `SpriteBatch.Draw()` overloads, `SpriteEffects.FlipHorizontally`,
  rotation/origin, `TextureAddressMode.Wrap`/`Mirror`, and `SpriteSortMode.Deferred`/`BackToFront`/
  `FrontToBack` are all real XNA 4.0 API surface.
- Related production code read in full: `src/CNA/Internal/Backends/D3D9/D3D9SpriteBatch.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`pushSprite()`/`flushBatch()`/`Draw()`
  overloads, lines 145-260-ish), `src/CNA/Internal/Backends/D3D9/D3D9StateMapping.cpp`
  (`TextureAddressModeToD3D9()`).

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only; this report is entirely
static/source-reading. No build or execution was attempted or claimed in this Linux sandbox. The
file's own header comment states every expected pixel value was independently confirmed against a
real XNA 4.0 oracle (`tools/xna-oracle/scenes/*.scene`) before this audit — that oracle comparison
itself was not re-run here; this report instead independently re-traces the production logic each
check depends on.

## Purpose

A 9-check-group test (A through I) of `D3D9SpriteBatchBackend` covering: exact destination-rectangle
boundary alignment / the D9-91 half-pixel offset (A), `SpriteEffects.FlipHorizontally` (B), rotation +
origin on a square destination (C), `TextureAddressMode.Wrap`/`Mirror` sampling beyond `[0,1]` UVs
(D/E), all three `SpriteSortMode` values via two overlapping alpha-blended sprites (F/G/H), and
multi-texture batching / flush-on-texture-change correctness (I).

## Executive Verdict

**Healthy** — every check traced against current production code (`D3D9SpriteBatch.cpp`,
`SpriteBatch.cpp`) matches what that code actually does. One architectural observation (not a defect):
Checks F/G/H exercise `SpriteSortMode` reordering logic that lives entirely in the shared,
backend-agnostic `Microsoft::Xna::Framework::Graphics::SpriteBatch::flushBatch()`, not in
`D3D9SpriteBatchBackend` itself — so these three checks, while correct, provide no D3D9-specific
signal beyond confirming the D3D9 backend draws sprites in whatever order it is handed (which it
does, correctly).

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Draw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
layerDepth)` (Checks B/C/F/G/H) and the shorter `Draw(texture, destinationRectangle, color)` (Check A)
and `Draw(texture, destinationRectangle, sourceRectangle, color)` (Checks D/E/I) all map to real FNA
`SpriteBatch.Draw()` overloads. `SamplerState::PointWrap` (used at Check D) and a hand-built
`TextureAddressMode::Mirror` `SamplerState` (Check E, correctly noting "real XNA has no named
`PointMirror` preset" — confirmed, FNA's `SamplerState` class only predefines
`PointClamp`/`PointWrap`/`LinearClamp`/`LinearWrap`/`AnisotropicClamp`/`AnisotropicWrap`, no `*Mirror`
variant) are both real, correctly-used API.

### Behavioral correctness
- **Check A** (half-pixel offset boundary alignment): traced `D3D9SpriteBatchBackend::
  BuildMatrixTransformEXT()` (`D3D9SpriteBatch.cpp` lines 148-187). The `-0.5f * projection.M11`/
  `-0.5f * projection.M22` shift (lines 180-181) is applied to the orthographic projection's own
  translation terms, and the function's own header comment documents this was "empirically verified
  against the real XNA 4.0 runtime… NOT reasoned out from first principles." Check A's boundary
  assertions (pixels just inside vs. just outside each of the 4 destination-rectangle edges) are a
  real, discriminating proof: an off-by-one-texel error in the shift's sign or magnitude would move
  the measured boundary by a full pixel in a specific direction, which this check's paired
  inside/outside sample points would catch.
- **Check B** (`FlipHorizontally`): traced `SpriteBatch::Draw()`'s effects handling
  (`D3D9SpriteBatch.cpp` line 300: `if (effects & FlipHorizontally) std::swap(u1, u2);`) — correct,
  swaps only the U texture coordinates, leaving V (and thus top/bottom) untouched, matching the
  check's own "TL↔TR, BL↔BR, top/bottom unchanged" assertion. The dedicated half-pixel
  content-sampling sub-check (lines 197-222) samples exactly on the internal color boundary this same
  draw produces and is explicitly documented as mutation-tested (deliberately removing the M41/M42
  offset changed this exact pixel in a reproducible way) — a genuinely strong, non-tautological proof
  distinct from Check A's own geometric-edge proof (the file's own comment correctly notes Check A
  cannot detect a content-sampling shift because a 1×1 source texture has no internal content to
  shift).
- **Check C** (rotation + origin): the check itself only asserts "some pixel in the bounding region
  is non-background" (line 247-249), not an exact per-quadrant color match — the file's own comment
  (lines 251-257) is honest about this, explicitly stating the exact pattern was oracle-confirmed but
  "not re-verified byte-for-byte in this offline CTest." This is accurately self-disclosed, not
  overclaimed.
- **Checks D/E** (Wrap/Mirror): `Draw()`'s UV computation (`D3D9SpriteBatch.cpp` lines 295-298) does
  not clamp `u1/v1/u2/v2` to `[0,1]` — confirmed no clamp exists in this code path, matching the
  check's own comment that FNA's real `SpriteBatch.cs` "divides straight through," and that the
  bound sampler's own `TextureAddressMode` (correctly mapped via `TextureAddressModeToD3D9()`,
  `D3D9StateMapping.cpp` lines 90-97: `Wrap→D3DTADDRESS_WRAP`, `Mirror→D3DTADDRESS_MIRROR`) is what
  actually resolves out-of-range UVs at the hardware level, not any CNA-side software wrapping.
- **Checks F/G/H** (`SpriteSortMode`): traced `SpriteBatch::flushBatch()`
  (`SpriteBatch.cpp` lines 185-209): `BackToFront` sorts by `a.layerDepth > b.layerDepth` (descending
  — farthest/highest-depth sprite drawn FIRST, nearest LAST, so the nearest ends up on top under
  painter's-algorithm compositing); `FrontToBack` sorts by `a.layerDepth < b.layerDepth` (ascending —
  nearest first, farthest last, so the FARTHEST ends up on top). Independently re-derived: Check F
  (Deferred, RED@0.0-then-GREEN@1.0 insertion order, unsorted) draws GREEN last → green-dominant;
  Check G (BackToFront, same insertion order) sorts to GREEN(1.0)-then-RED(0.0) → RED drawn last →
  red-dominant, matching the check's own expectation; Check H (FrontToBack, REVERSED insertion order
  GREEN@1.0-then-RED@0.0) sorts ascending to RED(0.0)-then-GREEN(1.0) → GREEN drawn last →
  green-dominant, matching Check F's value despite the opposite insertion order and sort mode — this
  independently confirms the check's own claimed discriminating property (the reorder is genuinely by
  `layerDepth`, not merely insertion order) is real and correctly implemented in current production
  code. `std::stable_sort` (not `std::sort`) is used for all three modes — the correct choice for a
  a sort whose comparator can produce ties (equal `layerDepth`), preserving insertion order for those
  ties as FNA's own SpriteBatch (a stable sort internally) does.
- **Check I** (multi-texture flush-on-change): `SpriteBatch::Draw()`'s per-call check at
  `D3D9SpriteBatch.cpp` line 285 (`if (currentTexture_ != nullptr && currentTexture_ != &texture)
  FlushBatch();`) correctly flushes on every texture-identity change, not just the first one —
  confirmed this branch re-evaluates on every `Draw()` call, so the interleaved
  RED→BLUE→RED sequence in Check I genuinely forces two separate flushes (RED→BLUE, then BLUE→RED),
  matching the check's own stated goal of proving the rebind is not "a one-shot fluke."

### Logic
The `SpriteSortMode` reordering itself (Checks F/G/H) is implemented once, in the shared
`Microsoft::Xna::Framework::Graphics::SpriteBatch::flushBatch()` — `D3D9SpriteBatchBackend` has no
sorting logic of its own at all (its `Draw()` simply appends to `pendingVertices_`/`pendingIndices_`
in call order, and `FlushBatch()` draws them as received). This means Checks F/G/H, while their
assertions are correct, are really regression-testing the shared XNA-layer sort logic rather than
anything D3D9-specific — see Architecture below. Not a defect, but worth noting for shard-level
coverage-planning purposes (this exact scene is presumably duplicated, near-verbatim, across every
other backend's own `*_spritebatch_test.cpp`, all really exercising the same shared code once each).

### C++ correctness
`D3D9SpriteBatchBackend::Draw()`'s vertex construction (`D3D9SpriteBatch.cpp` lines 316-347) computes
`scaleX = dw/sw`, `scaleY = dh/sh` with no zero-guard — a `sourceRectangle` with `Width==0` or
`Height==0` would produce a divide-by-zero (`inf`/`NaN`). Not exercised by any check in this file
(every source rectangle used has genuinely non-zero dimensions), and matches XNA's own real behavior
of not special-casing a degenerate source rectangle either (FNA's `SpriteBatch.cs` performs the
equivalent division unconditionally) — an intentional non-deviation, not a bug, but also not tested
either way, by this file or (as far as this audit traced) elsewhere in this shard.

### Memory/resource lifetime
`Texture2D tex/texA/texB` locals in each check's own scope are all constructed before the
`SpriteBatch sb` that draws them and outlive the `sb.Begin()/Draw()/End()` sequence within the same
block — no dangling-texture risk in this file as written.

### Performance
N/A — a one-shot pixel-correctness CTest, not a hot path.

### Architecture
See Logic above — `D3D9SpriteBatchBackend` correctly contains no sort-order logic of its own,
matching the intended layering (`SpriteSortMode` is XNA-level, backend-agnostic behavior; only the
final draw-order execution is backend-specific). This is the *correct* architecture (confirmed by
grep: no `stable_sort`/`layerDepth`-comparison code exists anywhere in `D3D9SpriteBatch.cpp`), not a
gap — flagged under Testing/Logic only as a shard-level observation about what these 3 particular
checks actually add over the same checks already existing (near-identically) for every sibling
backend.

### Maintainability
The file's own header comment is detailed, cites the exact oracle scene file for each check, and (D9-93)
documents a real bug this task found (the `zFarPlane=1` → `zFarPlane=-1` fix for
`BuildMatrixTransformEXT()`, needed once any check used a nonzero `layerDepth`) — independently
confirmed present in current `D3D9SpriteBatch.cpp` (line 179: `CreateOrthographicOffCenter(0.0f,
viewportWidth, viewportHeight, 0.0f, 0.0f, -1.0f)`, matching the comment's own derivation).

### Robustness
No check exercises a degenerate `sourceRectangle` (zero width/height) or a null/disposed texture (the
latter is `SpriteBatch`-level, `pushSprite()`'s own `ObjectDisposedException::ThrowIf` guard,
`SpriteBatch.cpp` line 156 — not re-tested here, reasonably out of this file's own D3D9-specific
scope).

### Testing
Strong, broad coverage of the D3D9-specific concerns (half-pixel offset, flip, wrap/mirror sampling,
multi-texture rebind) plus incidental (correct, but not D3D9-specific) coverage of the shared
sort-mode logic.

### Cross-file consistency
`D3D9SpriteBatch.cpp`'s vertex-format struct (`SpriteVertex{x,y,z; r,g,b,a; u,v}`, stride 24) is
confirmed consistent with `D3D9VertexDeclarations.cpp`'s own stride-24 layout (`POSITION0`
FLOAT3, `COLOR0` UBYTE4N at offset 12, `TEXCOORD0` FLOAT2 at offset 16 — cross-checked against this
same shard's `d3d9_common_test.cpp.audit.md`, which independently verified this exact table), and the
byte order used when packing `r,g,b,a` (`Draw()` lines 303-306, ascending R,G,B,A) matches the
R,G,B,A-ascending `D3DDECLTYPE_UBYTE4N` convention that same sibling report documents as a previously
real, fixed bug (`D3DCOLOR`'s B,G,R,A order would have silently swapped channels) — this file does not
reintroduce it.

## Detailed Findings

No CRITICAL/HIGH findings. No MEDIUM findings — every check traced against current production source
is real, correctly targeted, and (as far as this audit's independent re-derivation can determine)
currently passing.

## Missing or Weak Tests

- No check exercises `SpriteSortMode.Immediate` or `SpriteSortMode.Texture` — explicitly and honestly
  scoped out by the file's own header comment (lines 49-53), which correctly notes `Immediate`'s only
  real behavioral difference (per-`Draw()` GPU submission vs. batching until `End()`) is not
  pixel-observable by this oracle methodology, and `Texture` needs a different multi-texture scene
  design. A disclosed gap, not a hidden one.
- A degenerate (zero-width/-height) `sourceRectangle` is untested (see C++ correctness above) — low
  priority given this matches real XNA/FNA's own unguarded behavior rather than a CNA-specific risk.

## Positive Findings

- Check A's half-pixel-offset boundary proof and Check B's dedicated half-pixel *content*-sampling
  sub-check are correctly recognized by the file's own comment as testing two different things (
  geometric edge placement vs. which texture content a given screen pixel samples) — a sophisticated,
  accurate distinction, and the content-sampling check's own mutation-testing claim (verified via this
  audit's own reading of `BuildMatrixTransformEXT()`) is well-founded.
- Check H's reversed-insertion-order design is a genuinely well-thought-out test: it rules out the
  "looks sorted but is actually just insertion order" false-positive that Checks F/G alone could not
  rule out on their own.
- Check I's interleaved (not merely paired) texture-change design specifically targets a "one-shot
  flush fluke" class of bug, not just a single rebind.

## Final Assessment

A well-targeted, correctly-verified D3D9 `SpriteBatch` pixel-correctness test. The only notable
observation — that the `SpriteSortMode` checks (F/G/H) exercise shared, backend-agnostic logic rather
than anything D3D9-specific — is an architectural note about test-value/coverage-overlap, not a defect
in either the test or the production code it (correctly) exercises.
