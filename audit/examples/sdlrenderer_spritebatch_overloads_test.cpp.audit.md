# Audit: examples/sdlrenderer_spritebatch_overloads_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_overloads_test.cpp` (209 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::Draw` overload-coverage pixel test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_overloads …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_Overloads …)`,
  `cmake/Tests/SdlRendererTests.cmake:29-31`. Header traces to Task 666; confirmed via `git log`
  (`924f7a0a test(Task 666): audit all SpriteBatch::Draw overloads on SDL_Renderer`) with no later follow-up
  commit touching this file.
- XNA/FNA relevance: direct — every public `SpriteBatch::Draw` overload.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp` (all `Draw` declarations),
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (all `Draw` bodies),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`SdlSpriteBatchBackend::Draw`, 3 overloads).
- FNA reference: `Graphics/SpriteBatch.cs` (`public void Draw(...)`, 7 overloads, lines 336-...-eof of the
  `#region Public Draw Methods` block).

## Purpose

Header comment (lines 1-22) states the goal explicitly: exercise every `SpriteBatch::Draw` overload through
this specific backend, "fresh, from-scratch," not inherited from EasyGL test assumptions — "SpriteBatch::Draw
has 9 public overloads (3 NOXNA taking a raw Texture2D + explicit Rectangle/float args, 6 real XNA overloads
taking Vector2/optional<Rectangle> args). Each is exercised exactly once here." Nine 60×60 (or native-size)
slots are drawn side by side, each with a unique tint color (or, for the flip check, an asymmetric split
texture), and each slot's own centre/origin pixel is read back and compared.

## Executive Verdict

**Needs attention** — the file's own stated overload count is off by one, and the specific overload it omits is
not merely miscounted but genuinely never exercised anywhere in this file: `SpriteBatch::Draw(texture,
destinationRectangle, std::optional<Rectangle> sourceRectangle, color, rotation_rad, origin, effect,
layerDepth)` — the destRect-based overload with an *optional* source rectangle and rotation/origin/effect/depth
— exists in `SpriteBatch.hpp` (declared at line 289, defined at `SpriteBatch.cpp` line 356) as a real,
non-`NOXNA` XNA-parity overload (it matches FNA's `Draw(Texture2D, Rectangle, Rectangle?, Color, float, Vector2,
SpriteEffects, float)` exactly), and is not called by this test or, as far as this batch shows, by any of the
other 7 files audited alongside it. See F1.

## Checklist Results

### API / XNA / FNA parity
Independently counted every public `Draw` overload in `SpriteBatch.hpp`: 3 `NOXNA`-marked overloads (`Draw(tex,
x, y)`; `Draw(tex, destRect, srcRect [Rectangle, not optional], color)`; `Draw(tex, destRect, srcRect
[Rectangle, not optional], color, rotation, origin, effect, layerDepth)`) plus **7**, not 6, real (non-`NOXNA`)
XNA overloads:
1. `Draw(tex, position, color)`
2. `Draw(tex, position, optional<srcRect>, color)`
3. `Draw(tex, position, optional<srcRect>, color, rotation, origin, float scale, effects, depth)`
4. `Draw(tex, position, optional<srcRect>, color, rotation, origin, Vector2 scale, effects, depth)`
5. `Draw(tex, destRect, color)`
6. `Draw(tex, destRect, optional<srcRect>, color)`
7. `Draw(tex, destRect, optional<srcRect>, color, rotation, origin, effect, depth)` ← **not exercised by this
   test** (see F1)

This is a 3+7=10-overload total, not the 3+6=9 the file's own header claims — cross-checked directly against
FNA's `SpriteBatch.cs`, which declares exactly 7 public `Draw` overloads (the same 7 enumerated above, modulo
CNA's `std::optional<Rectangle>` standing in for C#'s `Rectangle?`), confirming CNA's 7 real overloads are a
complete, correct 1:1 parity set with FNA — the miscount is in this test file's own comment, not a production
API gap.

### Behavioral correctness
The 9 checks actually present (lines 161-175) were each independently traced to their claimed overload and
found to assert the right thing:
- Overload 1 (`Draw(tex,x,y)`, NOXNA): correctly checks the native 1×1 pixel at the exact origin, not a scaled
  region — this overload has no scale/tint parameter, so the check design (reading the single physical pixel,
  not a slot-centre average) is correct.
- Overload 3 (NOXNA, rotation/origin/effect/depth with **non-optional** `Rectangle` source, using
  `SpriteEffects::FlipHorizontally` and a split red/green texture): checks that the LEFT half of the
  destination reads green (the texture's own right half) — a genuine, discriminating flip check, not a "did it
  draw something" placeholder. Confirmed against `SdlSpriteBatchBackend::Draw`'s flip-corner-permutation logic
  (`SdlGraphicsBackend.cpp` lines 244-249, 284-292) that `FlipHorizontally` does swap which source corner lands
  on which screen corner.
- Overloads 4/5 (`Draw(tex,position,color)` / `Draw(tex,position,optSrcRect,color)`): correctly checked at the
  exact origin pixel only, since neither signature has a scale parameter (native-size draw) — matches the
  comment's own caveat (lines 166-168).
- Overloads 6/7 (float-scale / Vector2-scale): both correctly use `kSlotW`/`kSlotW,kSlotH` as the scale factor so
  the 1×1 texture fills the entire slot, and both are checked at slot-centre — a valid, if minimal, check that
  each scale overload dispatches without crashing and produces the right tint; does not separately verify
  non-uniform-scale geometry the way `sdlrenderer_spritebatch_scale_test.cpp` (same batch) does more rigorously.
- Overloads 8/9 (`destRect,color` / `destRect,optSrcRect,color`): straightforward slot-centre tint checks.

All 9 present checks are internally consistent and correctly derived; the issue is exclusively the 10th
overload's total absence, not a wrong assertion among the 9 present ones.

### Logic
`Slot(index)` (lines 79-82) computes non-overlapping 60×60 (+4px pad) regions; verified no two slots' regions
overlap for indices 0-8 given `kSlotW=60`, `kSlotPad=4` — arithmetic checks out (`Slot(i).X = 4 + i*64`, so
consecutive slots are exactly 64px apart, wider than each 60px slot, leaving a 4px gap).

### Memory/resource lifetime
`whiteTex_`/`splitTex_`/`sb_` `unique_ptr`-owned, constructed once in `Initialize()`, consistent with the shard.

### C++ correctness
No unsafe casts; `colourMatch` tolerance (`tol=40`, line 62) is tighter than the `tol=60` used elsewhere in this
batch, appropriate given 9 distinct tint colors packed into the same test (a looser tolerance risks two
adjacent tints becoming ambiguous, though the 9 chosen tints in `kTints` are spaced widely enough that even
`tol=60` would likely still discriminate them).

### Performance
N/A — single-frame test with 9 small draws.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 198), consistent with the rest of this batch.

### Maintainability
209 lines; the `kTints`/`Slot`/`Check` pattern keeps 9 distinct overload exercises readable in one file rather
than splitting into 9 tiny executables — a reasonable design choice for an "audit every overload" file, though
it does make the miscounted total (F1) easier to have missed, since there's no single place enumerating "here
are all N overloads" against the header for a human to cross-check line-by-line.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The split red/green texture + `FlipHorizontally` design (overload 3's check) is a strong, discriminating test
of flip correctness specifically, not merely "did the sprite draw somewhere" — a naive implementation that
silently ignored the flip flag would fail this specific check (reading red instead of green) while every other
check in the file would still pass, isolating the failure precisely.

### Testing
See F1 — this is fundamentally a testing/coverage-completeness finding about the file itself.

### Cross-file consistency
Cross-checked whether the missing overload (`Draw(destRect, optional<srcRect>, color, rotation, origin, effect,
depth)`) is exercised by any of the other 7 files in this batch: `sdlrenderer_spritebatch_rotation_test.cpp`
uses the sibling NOXNA overload with a **non-optional** `Rectangle` source rectangle (not the optional-source
variant); `sdlrenderer_spritebatch_sourcerect_test.cpp` uses the 4-argument NOXNA `destRect,srcRect,color`
overload (no rotation, non-optional source); `sdlrenderer_spritebatch_scale_test.cpp` uses the
position-based scale overloads, not any destRect-based overload. None of the 8 files in this batch calls the
missing overload. Its underlying implementation (`SpriteBatch.cpp` lines 356-376) is a thin wrapper that
resolves the `std::optional<Rectangle>` to a concrete `Rectangle` and then calls `pushSprite` with an identical
body to the already-tested NOXNA non-optional-Rectangle sibling overload, so the risk of an *undetected*
production bug specifically in this overload is lower than a wholly-untested code path would normally carry —
but it remains a genuine, confirmed gap in what this file claims to do.

## Detailed Findings

### F1 — File claims "9 overloads, each exercised exactly once," but `SpriteBatch::Draw` actually has 10 public overloads, and the 10th (destRect + optional source rect + rotation/origin/effect/depth) is never called by this file

- Severity: MEDIUM
- Confidence: HIGH (directly counted against the current `SpriteBatch.hpp`/`.cpp`, both of which are internally
  consistent with each other, and against the current FNA reference source)
- Category: test-coverage / correctness-of-test-claim
- Location/symbol: header comment lines 6-7 ("SpriteBatch::Draw has 9 public overloads... Each is exercised
  exactly once here"); missing case is `SpriteBatch::Draw(const Texture2D&, const Rectangle&
  destinationRectangle, std::optional<Rectangle> sourceRectangle, Color, float rotation_rad, Vector2 origin,
  SpriteEffects effect, float layerDepth)` (`SpriteBatch.hpp` line 289, `SpriteBatch.cpp` line 356)
- Evidence: `SpriteBatch.hpp` declares 10 total public `Draw` overloads (3 `NOXNA` + 7 non-`NOXNA`), confirmed by
  `grep -n "Draw(const Texture2D"` against the header returning 10 matches, and independently cross-checked
  against FNA's `SpriteBatch.cs`, which declares exactly 7 public `Draw` overloads — the same 7, matching CNA's
  7 non-`NOXNA` ones parameter-for-parameter (modulo `Rectangle?` → `std::optional<Rectangle>`). This test's own
  `kTints` array and inline overload-numbering comments (lines 50-59, 119-157) number exactly 9 cases, 1-9, and
  the 7th of the "real XNA" group they enumerate (`Draw(tex, destRect, color)`, their #8) skips directly to
  `Draw(tex, destRect, optSrcRect, color)` (their #9) without ever adding the rotation/origin/effect/depth
  variant of the destRect-based overload.
- Why it matters: this file's entire stated purpose is to be the from-scratch, comprehensive per-overload audit
  of `SpriteBatch::Draw` against this specific backend ("Each is exercised exactly once here" — an explicit
  completeness claim). A reader or future maintainer relying on this file's header comment as a coverage map
  would incorrectly believe all real XNA `Draw` overloads are backend-verified here, when one full XNA-parity
  overload is not directly exercised by any file in this 8-file batch.
- FNA/XNA comparison: the missing overload is a real, FNA-parity XNA API surface member (not a NOXNA extension),
  making its exclusion from a file titled "audit all SpriteBatch::Draw overloads" more significant than if it
  were one of the NOXNA convenience overloads.
- Related files: `include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`, `examples/sdlrenderer_spritebatch_rotation_test.cpp`
  and `examples/sdlrenderer_spritebatch_sourcerect_test.cpp` (both checked and confirmed not to cover this
  overload either).
- Suggested future action (not implemented by this audit): add a 10th slot exercising
  `Draw(texture, destinationRectangle, std::optional<Rectangle>{sourceRectangle}, color, rotation, origin,
  effect, layerDepth)` specifically (with a real `optional<Rectangle>` cropping value, not the empty/default
  case, to also exercise the source-rectangle resolution branch at `SpriteBatch.cpp` line ~360), and correct the
  header comment's overload count from 9 to 10.

## Cross-File Observations

- The missing overload's implementation body is structurally near-identical to two already-tested sibling
  overloads in this same file (the NOXNA non-optional-Rectangle rotation overload, exercised as "overload 3"
  here, and `sdlrenderer_spritebatch_rotation_test.cpp`'s own use of that same NOXNA overload) — this lowers,
  but does not eliminate, the practical risk versus a fully novel, untested code path.
- Consistent `PresentationMode::NativeBackBuffer` / `GetBackBufferData`-per-slot idiom shared with the rest of
  the batch.

## Missing or Weak Tests

See F1 — add a 10th check for the destRect + optional-source + rotation/origin/effect/depth overload, and
correct the header's overload count.

## Positive Findings

- All 9 checks actually present are correctly designed and correctly matched to their claimed overload,
  including the genuinely discriminating flip check (overload 3) and the correct native-size-only checks for
  the two overloads that lack a scale parameter (overloads 1, 4, 5).
- The non-overlapping `Slot()` layout is arithmetically sound for all 9 slots used.
- The file's stated intent (fresh, backend-specific overload audit, not inherited EasyGL assumptions) is a good
  practice that this audit endorses even though the execution has one gap.

## Final Assessment

A well-constructed test let down by an inaccurate completeness claim: 9 of `SpriteBatch::Draw`'s 10 public
overloads are genuinely, correctly exercised, but the file's own header asserts a total of 9 and "each exercised
exactly once," when a 10th real, non-`NOXNA` overload (`Draw` with a destination rectangle, optional source
rectangle, and rotation/origin/effect/depth) exists in the current API and is not called here or, as far as
this batch shows, anywhere else in the `examples-tests-sdlrenderer` shard.
