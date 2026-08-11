# ASCII Graphics Backend (SDL-windowed, not a real terminal) — Implementation Plan

> **Superseded, 2026-08.** The `ASCII` public graphics-renderer identity this plan built was
> removed from `CNA_GRAPHICS_RENDERER`/`GraphicsRendererType`. Its reusable quantizer/font-atlas
> logic and visual behavior were migrated to a renderer-neutral post-process effect,
> `CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`) — see
> `docs/ascii-post-process-effect.md`. This plan is preserved unchanged below as the historical
> record of the former renderer's own implementation phases.

> **Status: ✅ COMPLETE 2026-07-15 — all 8 phases (`ASCII-1`–`ASCII-82`) closed** on `feature/ascii`
> (worktree `../cnaascii`, branched from `develop`, pushed to `origin/feature/ascii`). Full
> `CnaTests` regression: 4364 pass / 2 skip / 11 pre-existing failures (zero regressions across
> the whole plan); dedicated `Ascii_*` ctest suite: 6/6 (`Ascii_Present` extended to 9/9 by the
> `ASCII-30` correction below). Two real bugs found and fixed along the
> way — see `ASCII-40`'s own note (a blend-state bug that silently hid every `Color`-mode
> background fill) and `ASCII-41`'s (a double-buffer-swap pixel-verification pitfall). Not yet
> merged to `develop` — awaiting the project owner's decision on next steps (PR, merge, etc.).
>
> **Correction pass, 2026-07-15 (same day)**: a review flagged that `ASCII-30`'s "configurable
> cell pixel size" claim was true only of the internal `QuantizeFrameToGrid()` function's
> parameters, not of anything a caller of `AsciiGraphicsBackend` could actually reach — `Present()`
> hardcoded `kAsciiGlyphWidth`×`kAsciiGlyphHeight` at its one call site. Fixed for real:
> `SetCellSize()`/`GetCellSize()` added, `Ascii_Present` extended 4/4 → 9/9 to prove it. The same
> review also flagged `ASCII-11`'s note as reading like full `SpriteFont` reuse in `Present()`
> itself; `ASCII-11`'s note below now states plainly why that specific claim doesn't hold and
> won't — `Present()` runs at the backend layer, below the XNA-level `SpriteFont`/`GraphicsDevice`
> it would need, per this project's own layering rule — and what *is* actually reused (the
> internal `ISpriteBatchBackend`/`ITextureBackend` pair, same one-quad-per-glyph shape). A third,
> low-severity finding (`Ascii_*` ctests not registered under `NOT WIN32`) was checked and found to
> be an existing project-wide convention shared by five other backends, not an `ASCII`-specific
> gap — see `ASCII-81`'s note.
>
> **Revision note**: the original direction (rendering to a *real* terminal via raw `stdin`/ANSI
> escape codes) has been **fully cancelled by the owner** and is not part of this plan. The
> replacement idea — same retro text/glyph-grid *look*, but rendered inside a normal SDL window via
> `SDL_Renderer`, exactly like the existing `SDL_RENDERER` backend — turns out to be **dramatically
> simpler**, because it sidesteps the two hardest problems the terminal version had: input and
> protocol/emulator variability. See §0.2.
>
> **Status legend**: ✅ implemented *and verified*; 🟨 exists but unverified; ⬜ not implemented.
> Every task below is ⬜.

---

## 0. Feasibility / reality check

### 0.1 Is this realistic?

More realistic than the terminal version, and with a better precedent: this is exactly how real,
well-loved shipped games already render — roguelikes like *NetHack*, *Dwarf Fortress* (its default
tileset), *Cogmind*, *Brogue* all draw a glyph+color grid inside a normal GPU-backed window, not a
literal terminal. That's a much stronger precedent than the terminal version's own reference points
(`chafa`/`libcaca`/`curl parrot.live`), which are genuinely just "picture in a terminal" novelties.
Caveat still applies honestly: those games' *art* was authored for the glyph-grid medium from the
start. CNA quantizing an arbitrary existing XNA game's real sprite art down to a coarse cell grid
will still look "blocky/retro" — a deliberate style choice to present as such, not to oversell as
high-fidelity.

### 0.2 Mouse and keyboard — now essentially a non-issue

This is the actual reason the SDL-windowed approach is worth doing. The terminal version's whole
§0.2 problem was that CNA's `Mouse`/`Keyboard`/`GamePad` input is fundamentally window-event-driven
(`plan_headless.md`'s `HEADLESS-52` finding), and a terminal has no window — forcing a whole new,
POSIX-only, character-cell-resolution, TTY-only input subsystem to be built from scratch.

**A real SDL window removes the problem entirely.** This backend creates and owns a normal SDL
window, exactly like `SDL_RENDERER` does — every existing `Mouse`/`Keyboard`/`GamePad` code path
keeps working completely unchanged, with full pixel-accurate mouse coordinates (translated to
whichever logical/glyph-cell coordinate space the game wants, the same way every other windowed
backend already handles logical-vs-physical coordinate mapping). **No raw `termios` mode, no SGR
mouse-tracking protocol, no `isatty()` gate, no POSIX-vs-Windows-console split, no "silently absent
when not a real TTY" caveat.** The entire terminal-input phase from the cancelled original plan (a
whole new backend-specific input subsystem) is deleted outright, not merely deferred.

### 0.3 Will it look terrible?

Less of a concern than the terminal version, for concrete reasons:

- **No 80×24 physical terminal-size ceiling.** The grid resolution (how many glyph cells wide/tall)
  is just a configuration choice against a real window of any size — pick a denser grid and a
  crisp, purpose-made bitmap font instead of whatever the user's terminal emulator happens to
  render.
- **No terminal-emulator font-rendering variance, no color-depth negotiation** (`COLORTERM`/`TERM`
  sniffing, 16/256/truecolor fallback) — a real SDL window always has full RGBA available, so that
  entire fallback ladder from the terminal plan disappears too.
- **No frame-rate risk from escape-code churn** — there's no escape-code protocol at all anymore;
  this just draws textured quads via `SDL_Renderer`, the same cost class as any normal sprite-heavy
  `SpriteBatch` scene.
- The honest remaining caveat is purely aesthetic, not technical: quantizing arbitrary existing
  sprite art into a coarse glyph grid is inherently lossy (fine detail/gradients/small text will
  still read as blocky), same as it would be for any glyph-grid medium — just no longer compounded
  by terminal-specific constraints on top.

**Overall recommendation: build it.** It keeps the fun "retro text-mode aesthetic" novelty value
without any of the input/protocol pain the terminal version had — genuinely a much better
effort-to-value ratio.

---

## Design decisions

1. **A real SDL window, not headless.** Extends nothing from `HEADLESS`/`SOFTWARE`'s "no window"
   guards — the opposite: this backend needs a real window exactly like `SDL_RENDERER`, specifically
   *because* that's what makes §0.2 work.
2. **Architecture: a thin decorator around the existing `SDL_RENDERER` backend, not a new CPU
   compositor.** This is the single biggest simplification from the cancelled terminal plan (which
   needed to reuse `SOFTWARE`'s CPU rotation/scale/tint/blend math because it had no GPU-backed
   blit available at all). Here, a real `SdlGraphicsBackend` instance already exists and already
   does correct, already-tested compositing — so `SpriteBatch`/`Texture2D`/`RenderTarget2D`/etc.
   calls forward straight to it, rendering the game's actual content at its own logical resolution
   into an offscreen render target `SDL_RENDERER` already knows how to create. **This backend adds
   exactly one new thing: a custom `Present()`.** (An alternative considered: implement this as a
   runtime toggle/NOXNA extension directly on `SDL_RENDERER` instead of a separate
   `CNA_GRAPHICS_BACKEND` value, since it *is* structurally "`SDL_RENDERER` plus a present-time
   filter." Decided to keep it a distinct, separately-selectable backend for discoverability and
   consistency with how this project already exposes backend choice, but this is a real, low-cost
   alternative worth revisiting if a toggle turns out to be more convenient in practice.)
3. **The `Present()` override**: read back the offscreen game-resolution framebuffer, quantize it
   into an *N*×*M* grid of glyph+color cells (sampling or block-averaging the source pixels each
   cell represents), then draw that grid as textured quads from a bitmap font atlas into the real,
   visible window — using the exact same `SDL_RenderTexture` call `SDL_RENDERER` already uses for
   ordinary sprite blits. No new draw primitive is needed.
4. **Reuse CNA's existing `SpriteFont`/bitmap-font glyph-drawing code for the grid itself**, rather
   than writing a new glyph-blit path — each cell is architecturally just one more glyph draw call,
   the same shape as an ordinary `DrawString`. A monospace font atlas (e.g. a classic VGA CP437
   8×16 font, or a custom-authored one — license-clean, redistributable) is the one genuinely new
   *asset* dependency this plan introduces; no new *code* dependency.
5. **Two runtime modes on one CMake backend** (`CNA_ASCII_MODE=BLACKWHITE|COLOR`, default `COLOR`),
   mirroring `plan_headless.md`'s own "one CMake backend, N runtime modes" precedent, kept from the
   original plan. `BlackWhite` = monochrome luminance→glyph-density ramp (the lesser-looking,
   simpler tier, per §0.3); `Color` = colored glyph plus optional background fill per cell. This
   naming also resolves the earlier `ASCII`-backend-vs-`Ascii`-mode collision noted in the prior
   revision of this plan.
6. **Grid size is configurable**, not fixed at a terminal's 80×24 — derived from the game's logical
   resolution and a chosen cell pixel size (e.g. one cell per 8×16 source pixels), reusing the same
   virtual-resolution/letterbox math every other backend already shares.
7. **No color-depth detection, no `isatty()` gate, no dirty-cell diffing required for correctness**
   (all deleted from the terminal plan, §0.3) — a full-grid redraw every frame is cheap for
   `SDL_Renderer`, the same cost class as any normal sprite scene. A dirty-cell diff could still be
   added later as a pure performance optimization, not a correctness requirement.
8. **No platform gate** — this is exactly as portable as `SDL_RENDERER` already is, since it *is*
   `SDL_RENDERER` underneath plus a present-time filter.
9. **Fully testable in this dev loop, no `needs_human` split at all** — unlike both the original
   terminal-based version of this plan and `CANVAS`/`DX3`'s own gaps, there is no real-TTY, no
   real-browser, and no real-Windows-machine dependency here: real window + real readback are
   already proven (`SDL_RENDERER`'s own existing test suite). The only genuinely subjective thing
   left is font/grid aesthetic choice, not functional correctness.
10. **2D-only, `ThrowNo3D` reused — likely verbatim, not just in spirit.** Since this backend
    delegates to `SdlGraphicsBackend` for everything except `Present()`, its 3D-pipeline entry
    points can probably just forward directly to `SDL_RENDERER`'s own already-implemented
    `ThrowNo3D` calls rather than re-declaring them — confirm and reuse, don't duplicate.

---

## Active execution order

1. Phase G1 (CMake integration + skeleton, decide the composition-over-`SDL_RENDERER` wiring)
   unblocks everything else.
2. Phase G2 (bitmap font atlas) and Phase G3 (offscreen game-resolution render target) can proceed
   in either order once G1 lands.
3. Phase G4 (quantization: framebuffer → glyph/color grid) is the actual point of this backend —
   verify continuously once G2/G3 exist.
4. Phase G5 (drawing the grid into the real window) closes the loop — this is what makes G4
   actually visible.
5. Phase G6 (confirm zero `Mouse`/`Keyboard`/`GamePad` changes needed) is a verification task, not
   implementation — do it early, it should be nearly free given Design decision 1/§0.2.
6. Phase G7 (`ThrowNo3D` reuse) any time after G1.
7. Phase G8 (tests + docs) — since Design decision 9 means everything is testable here for real,
   hold this backend to the same automated bar `SDL_RENDERER` itself already meets, not a lesser one.

---

## Phase G1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-1 | Add `"ASCII"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` and a matching `CNA_BACKEND_ASCII` option | ✅ | Verified 2026-07-15: configures cleanly with `-DCNA_GRAPHICS_BACKEND=ASCII`. |
| ASCII-2 | `cna_backend_graphics_ascii` target — internally constructs/owns a real `SdlGraphicsBackend` instance (or equivalent shared implementation) rather than reimplementing texture/vertex/compositing machinery | ✅ | Implemented via a new `cna_backend_graphics_sdl_renderer_core` static lib (mirrors the existing `cna_backend_graphics_d3dcommon` shared-core pattern) that compiles `SdlGraphicsBackend.cpp` again for the ASCII build — `CNA_BACKEND_SDL_RENDERER` is not defined in this build, so that file's own `#ifdef`-guarded `CreateGraphicsBackend()` compiles out cleanly, leaving Ascii's own factory as the only one. Builds clean. |
| ASCII-3 | `CNA_ASCII_MODE` env-var parsing (`BLACKWHITE`/`COLOR`, default `COLOR`) + programmatic `SetMode()` override, same pattern as `HEADLESS-5` | ✅ | Closed in Phase G4 (see `ASCII-33`'s note) — deliberately deferred here since mode selection was meaningless before quantization existed. |
| ASCII-4 | `AsciiGraphicsBackend` skeleton: every `IGraphicsBackend` method not related to presentation forwards straight to the wrapped `SdlGraphicsBackend` | ✅ | `include/CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp` + `.cpp`. Every pure virtual, plus every method `SdlGraphicsBackend` itself overrides with real behavior (`ReadBackbuffer`/`SetSwapInterval`/`ApplyMultiSampleCount`/`CreateRenderTarget2D`/`SetRenderTarget2D`/`SetRenderTargets`/`SetScissorRect`/`ApplyBlendState`/`SupportsDepthStencil`/`CreateOcclusionQuery`), forwards to `inner_`. Methods where `SdlGraphicsBackend` itself just uses the `IGraphicsBackend` base default (e.g. `CreateTexture3D`, `ApplySamplerState`) are left un-overridden here too — same net behavior, less code. `Present()` currently forwards unchanged (Phase G4/G5 will add quantization). |
| ASCII-5 | Factory dispatch for `ASCII`; real SDL window creation (no `#ifdef` "skip window" guard needed — Design decision 1) | ✅ | Verified 2026-07-15: full `CnaTests` (4377 tests) built and ran under `-DCNA_GRAPHICS_BACKEND=ASCII` with a real X11 display (`SDL_VIDEODRIVER=x11`) — **4364 pass, 2 skipped** (the same 2 pre-existing hardware-sensor skips every backend has), confirming a real window is created and the whole test corpus runs against it. **11 pre-existing failures found, none caused by ASCII**: `EffectApplyTest` (2), `SkinnedModelEXTPartTest` (6), `ContentManagerSkinnedModelTest` (3) all construct a real `VertexBuffer`/`IndexBuffer` directly, which `SdlGraphicsBackend::CreateVertexBuffer`/`CreateIndexBuffer16` throw on unconditionally (`ThrowNo3D`) — since ASCII wraps that same real implementation (design decision 2), it inherits this exact, deterministic, backend-general SDL_RENDERER limitation. These 3 test files assume a 3D-capable backend and would fail identically under plain `SDL_RENDERER` too; not modified here (out of scope — a pre-existing test/backend mismatch unrelated to this plan, not a regression). Two **real, in-scope** fixes made instead, both backend-registration gaps a new `CNA_GRAPHICS_BACKEND` value mechanically needs: `GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` didn't count `CNA_BACKEND_ASCII` (fixed); `GraphicsDeviceValidationTests.cpp`'s `SetRenderTargets_FourTargets_DoesNotThrow` only special-cased `CNA_BACKEND_SDL_RENDERER` for the "throws because 2D-only" branch, not `CNA_BACKEND_ASCII` (fixed — same throw, same reason, since ASCII forwards to the same real backend). Both confirmed fixed by a clean rerun (4364/4377, same 11 pre-existing failures, 0 new). |

## Phase G2 — Bitmap font atlas

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-10 | Source or author a license-clean monospace glyph atlas (e.g. VGA CP437 8×16) | ✅ | **Deviated from the "8×16" example deliberately**: hand-authored an 8×8 atlas instead (`AsciiFontAtlas.cpp`'s `kGlyphBitmaps`), covering exactly the 10 characters in `kAsciiGlyphRamp` (`" .:-=+*#%@"`) rather than full CP437 — the quantizer (Phase G4) only ever indexes by ramp position, never by arbitrary character, so a full font would be unused scope. 8×8 was simpler to hand-verify correctly than 8×16 for a first cut; extending to 8×16 later is a data-only change, no architecture change. Fully custom/original pixel data (not a vendored font file) — no license/attribution to track, matches CNA's own existing convention of hand-built fixtures instead of real font assets (`docs/sdl-renderer-2d-completeness.md`'s SpriteFont tests do the same). |
| ASCII-11 | Load it through CNA's existing `SpriteFont`/`BitmapFont` infrastructure rather than a new texture-atlas loader | ✅ (documented architectural deviation, see below) | `BuildAsciiFontAtlas(GraphicsDevice&)` builds a real `Texture2D` (white-on-transparent glyph pixels, tinted by `SpriteBatch`'s per-draw color like any other CNA texture) and returns a real `SpriteFont` over it via the exact same "app builds the atlas itself" constructor path CNA's own SpriteFont tests already use (no XNB pipeline exists or is needed). Verified 2026-07-15: `Ascii_FontAtlas` ctest, 4/4 checks — glyph pixel-count ramp is strictly increasing (0,2,4,6,12,20,24,40,48,60 of 64 px, verified by direct popcount, not just eyeballed), atlas builds without throwing, `SpriteFont::getCharactersProperty()` matches `kAsciiGlyphRamp` exactly in order, default character is space. **Honest scope note, made explicit 2026-07-15**: this real `SpriteFont` path is available for application/diagnostic code that already holds a `GraphicsDevice`, but `Present()`'s own production draw path does **not** go through it — it draws `fontAtlasTexture_` (a plain `ITextureBackend`) via the internal-only `presentSpriteBatch_` (an `ISpriteBatchBackend`), one textured quad per cell. This is not an oversight; it is a hard architectural constraint documented in `AsciiFontAtlas.hpp`'s own comment on `BuildAsciiFontAtlasImageData()` and `AsciiGraphicsBackend.hpp`'s `fontAtlasTexture_` member comment: `AsciiGraphicsBackend` is a backend (`IGraphicsBackend` implementation, `src/CNA/Internal/Backends/...`), which per this project's own layering rule (`CLAUDE.md`'s "Internal (CNA) vs XNA Layer" table) sits *below* and must stay hidden from the XNA public API — `SpriteFont`/`SpriteBatch` (`Microsoft::Xna::Framework::Graphics::...`) are built *on top of* backends, constructed from a `GraphicsDevice&` that itself wraps a backend instance. A backend calling into `SpriteFont::DrawString` from inside `Present()` would invert that dependency direction (backend → XNA API → backend), which this codebase does not do anywhere else either. Design decision 4's "each cell is architecturally just one more glyph draw call, the same shape as an ordinary `DrawString`" is satisfied in *effect* (same one-quad-per-glyph structure, same atlas, same tint-by-draw-color model) via the internal `ISpriteBatchBackend`/`ITextureBackend` pair, which is the correct layer for a backend to operate in — not via the literal `SpriteFont` class. |

## Phase G3 — Offscreen game-resolution render target

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-20 | The game's actual `SpriteBatch`/`Texture2D`/`RenderTarget2D` draws are forwarded to the wrapped `SdlGraphicsBackend`, targeting an offscreen render target sized to the game's own logical resolution | ✅ | `gameTarget_` (an `IRenderTargetBackend` created via `inner_->CreateRenderTarget2D`) is bound as the default target at construction and after every `SetVirtualResolution()`. `SetRenderTarget2D(nullptr)`/`SetRenderTargets(..., 0)` — XNA's "target the back buffer" idiom — are intercepted and redirected to `gameTarget_` instead of being forwarded as a literal nullptr, so game code can never draw straight onto the real window; a genuinely non-null target (the game's own `RenderTarget2D`) still forwards unchanged. `Present()` unbinds `gameTarget_`, stretch-blits its full content onto the real backbuffer via an internal-only `ISpriteBatchBackend` (Phase G3: plain copy; Phase G4/G5 will replace this blit with the quantized glyph-grid draw), presents for real, then rebinds `gameTarget_`. |
| ASCII-21 | Confirm this offscreen target is genuinely readable back (`GetBackBufferData`-style) before Phase G4 needs it | ✅ | New `Ascii_OffscreenTarget` ctest, 4/4 checks: `Clear()`+`GetBackBufferData()` round-trips the exact color; `SetRenderTarget(nullptr)` genuinely redirects to `gameTarget_` (not the game's own explicit `RenderTarget2D`, which was bound just before); the explicit `RenderTarget2D` (constructed with `RenderTargetUsage::PreserveContents` — the 2-arg ctor's `DiscardContents` default auto-clears to black on every bind, a real, documented, unrelated XNA/FNA behavior that would otherwise make this check fail for the wrong reason) still holds its own independent content afterward, proving the two framebuffers are genuinely separate, not aliased; `Present()` does not throw. Also re-verified visually: the Phase G2 screenshot demo (glyph-ramp `DrawString`, now routed through a real `Present()` call) produced a byte-for-byte identical screenshot to the pre-Phase-G3 one. Full `CnaTests` regression re-run: still exactly the same 4364 pass / 2 skip / 11 pre-existing failures as `ASCII-5` established — zero new regressions from the redirect logic. |

## Phase G4 — Quantization (framebuffer → glyph/color grid)

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-30 | Grid sizing from logical resolution + configurable cell pixel size, reusing shared letterbox/virtual-resolution math | ✅ | New `AsciiQuantizer.hpp`/`.cpp`, a pure function (`QuantizeFrameToGrid`) with no `GraphicsDevice`/window dependency at all. Grid `columns`/`rows` round up (`ceil(srcWidth/cellWidth)`/`ceil(srcHeight/cellHeight)`); edge cells that aren't a full `cellWidth`×`cellHeight` block only average the real remaining pixels, never out-of-bounds memory. **Correction 2026-07-15**: the first cut of this task (see git history) hardcoded `Present()`'s call site to `kAsciiGlyphWidth`×`kAsciiGlyphHeight` (8×8) with no way to actually change it at runtime — the quantizer function itself took `cellWidth`/`cellHeight` as parameters, but nothing in `AsciiGraphicsBackend`'s public API let a caller reach them, so "configurable" was true only in the sense that a future code change could do it, not that the shipped backend could. Closed the gap for real: `AsciiGraphicsBackend::SetCellSize(int, int)`/`GetCellSize(int&, int&) const` (mirrors the existing `SetMode()`/`GetMode()` pattern, callable at any time including before `Game::Run()`, throws `std::invalid_argument` for non-positive sizes), stored in new `cellWidth_`/`cellHeight_` members defaulting to `kAsciiGlyphWidth`×`kAsciiGlyphHeight`, and `DrawQuantizedGridOntoRealBackbuffer()` now reads those members instead of the constants directly. The on-screen cell quad size is unaffected by this (still always `realWidth/columns`×`realHeight/rows`, computed fresh from whatever grid results) — only the *source-pixel sampling granularity*, i.e. how many `gameTarget_` pixels get averaged per cell, changes. `Ascii_Present` ctest extended (4/4 → 9/9): `GetCellSize()` defaults to 8×8; `SetCellSize(0, 8)` throws; `SetCellSize(16, 16)`/`GetCellSize()` round-trip; and, the actual functional claim (not just the accessors), `SetCellSize(16, 16)` on a 64×64 view measurably produces a 4×4 grid instead of 8×8, verified via a new test-only `GetLastGridDimensionsForTesting()` accessor. |
| ASCII-31 | `BlackWhite` mode: per-cell luminance → glyph-density ramp (e.g. `" .:-=+*#%@"`-style) | ✅ | Per-cell average RGB → standard luma (`0.299R+0.587G+0.114B`) → glyph index via linear rank into `kAsciiGlyphRamp`. `BlackWhite` mode: fixed white foreground, `hasBackground=false` (glyph shape alone conveys brightness — the more "authentic ASCII art" convention, not a per-cell color fill). |
| ASCII-32 | `Color` mode: per-cell representative foreground color (+ optional background fill) via block sampling/averaging | ✅ | Foreground = the cell's own averaged RGB (exact, not just luminance); background = that same color at quarter brightness (`hasBackground=true`) — a real, if simple, implementation of the "optional" fill the task text calls for, not skipped. |
| ASCII-33 | `Ascii_Quantize` test: known synthetic framebuffer → asserted exact glyph/color grid output — fully automatable (Design decision 9) | ✅ | Named `Ascii_Quantizer` ctest (13/13 checks, no display/window needed — genuinely a pure-function test): `BlackWhite` picks the correct extreme glyph indices for solid-black/solid-white blocks with no background fill; `Color` mode's foreground/background are the exact expected values for a solid-red block; a non-exact-multiple source size (20px wide, 8px cells) rounds the grid up to 3 columns and the clamped edge cell (4 real px wide) reads correctly, not garbage; `CNA_ASCII_MODE` env-var parsing (`ASCII-3`, deferred here from Phase G1) is case-insensitive and defaults to `Color` on unset/unrecognized, mirroring `HEADLESS`'s own `ParseHeadlessModeFromEnvironment()` pattern exactly. `AsciiGraphicsBackend::SetMode()`/`GetMode()` added, `mode_` initialized from the env var at construction — not yet consumed by `Present()` (that's Phase G5's job). |

## Phase G5 — Drawing the grid into the real window

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-40 | `Present()` override: draw the quantized grid as textured glyph quads (reusing `SpriteFont`/`SDL_RenderTexture` draw calls, Design decision 3/4) into the real visible window | ✅ | `Present()` reads `gameTarget_` back (while still bound), quantizes it, switches to the real backbuffer, draws one background-fill quad (if `hasBackground`) + one glyph quad per cell via `presentSpriteBatch_` (both sampling `fontAtlasTexture_` — the background fill uses a dedicated fully-solid extra atlas slot, `kAsciiSolidGlyphIndex`, added in this phase), presents for real, then rebinds `gameTarget_`. Cell destination rects are computed as `col*realWidth/columns` directly (not accumulated), so adjacent cells' shared edges always match exactly — no rounding gaps/overlaps. **Real bug found and fixed**: `presentSpriteBatch_` never goes through `GraphicsDevice::BlendState` (only the real XNA-level `SpriteBatch` does), so the SDL renderer's blend mode was left at whatever it happened to default to — not guaranteed alpha-aware. Confirmed empirically via the new pixel test: background fills were invisible, with the glyph's *transparent* "off" pixels silently overwriting the background with the texture's stored black RGB instead of leaving it alone. Fixed by calling `inner_->ApplyBlendState(...)` with real XNA `BlendState.AlphaBlend` factors (`Blend::One`/`Blend::InverseSourceAlpha`/`BlendFunction::Add`) unconditionally before every grid draw, independent of whatever the game's own `BlendState` is. |
| ASCII-41 | `Ascii_Present` pixel test: render known content, read back the *real window's* presented pixels, assert the correct glyph cells/colors appear — genuinely possible here, unlike the terminal version | ✅ | New `Ascii_Present` ctest (4/4). Uses `PresentationMode::NativeBackBuffer` + a 64×64 virtual resolution (exact multiple of the 8×8 glyph cell), so the real window is pixel-for-pixel identical to the logical resolution — no stretch/interpolation ambiguity when sampling specific pixels. **A second real, separate finding surfaced while building this test, not a product bug**: reading pixels immediately after a *real* `Present()` call returned all-black, because SDL_Renderer/OpenGL presents via a genuine double-buffer swap, not a copy — "the current render target" right after a swap is the buffer that wasn't drawn into this frame, not the one just shown. Solved by splitting `Present()`'s drawing logic out into a shared, private `DrawQuantizedGridOntoRealBackbuffer()`, and adding a test-only `DrawQuantizedGridForTesting()` (does the draw, deliberately skips the swap and the `gameTarget_` rebind) + `ReadRealBackbufferForTesting()` so a test can read what was actually just drawn, before any swap could invalidate it. Verified: `Clear(gray 140)` → every cell picks the same glyph (`'+'`, ramp index 5) with the exact expected foreground/background colors; a cell's corner (glyph row 0, always off for `'+'`) reads the exact background color, its center (rows 3-4, on) reads the exact foreground color. Also re-verified visually (screenshot, gradient bar + colored blocks) both before and after the blend-state fix — the "before" version genuinely showed black gaps where the background fill should have been, confirming the bug was real and visible, not just a test artifact. |
| ASCII-42 | (Optional, perf-only) dirty-cell diff to skip redrawing unchanged cells | ⬜ | Design decision 7 — optimization, not correctness. Still not implemented; every frame fully re-reads, re-quantizes, and redraws the whole grid. Left for later if performance ever demands it. |

## Phase G6 — Input (verification only)

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-50 | Confirm `Mouse`/`Keyboard`/`GamePad` work completely unmodified against this backend's real window | ✅ | Confirmed exactly as expected — zero new code needed. Two independent pieces of evidence: (1) the pre-existing, backend-agnostic `MouseTest.SetPositionConvertsLogicalToWindowForLetterboxedRenderer`/`SetPositionHandlesLetterboxOffsetNotJustScale` (`tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`) both run and pass unmodified under `-DCNA_GRAPHICS_BACKEND=ASCII` (verified directly via `--gtest_filter`, not just inferred from the full suite's aggregate pass count); (2) the new dedicated `Ascii_Input` ctest (3/3, see `ASCII-51`'s own note). |
| ASCII-51 | Mouse coordinate mapping: physical window pixel → logical game coordinate, reusing the same `TransformWindowToLogical` mechanism every windowed backend already has (optionally also expose the glyph-cell the mouse is over, as a convenience, not a replacement for pixel coordinates) | ✅ | New `Ascii_Input` ctest (3/3): a real window exists (`GetWindowInternal() != nullptr`, unlike `HEADLESS`/`SOFTWARE`); `Mouse`/`Keyboard`/`GamePad.GetState()` don't throw; `Mouse.SetPosition()` to the logical center of the game's own virtual resolution round-trips through `Mouse.GetState()` within 1px — a real, ASCII-specific exercise of the actual coordinate-transform path (`SdlInputBridge`), not just an inference. Confirmed architecturally why this needed no new code: SDL's own logical-presentation coordinate mapping is keyed to the *renderer's* configured logical size (set via `SetVirtualResolution`/`SetPresentationMode`, both plainly forwarded to `inner_`), not to whichever render target (`gameTarget_` vs. the real backbuffer) happens to be currently bound — so Phase G3's redirect never touches this path at all. The "expose the glyph-cell the mouse is over" convenience mentioned in this task's own text was **not** added — no concrete need for it surfaced, and speculative API surface is exactly what this project avoids adding without one. |

## Phase G7 — `ThrowNo3D` reuse

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-60 | Confirm every 3D-pipeline `IGraphicsBackend` method can forward directly to the wrapped `SdlGraphicsBackend`'s own existing `ThrowNo3D` calls rather than re-declaring them | ✅ | New `Ascii_ThrowNo3D` ctest (17/17): all 12 directly-reachable 3D-pipeline entry points throw (`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`/`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateOcclusionQuery`); `SupportsDepthStencil()` is `false`; `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube`/`CreateEffectBackend` all return `nullptr` (never overridden by `SdlGraphicsBackend` either, so `AsciiGraphicsBackend` correctly leaves them un-overridden too — design decision 2's "same net behavior, less code"). **Honest scope note**: `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawInstancedPrimitivesEx` are not exercised directly — they need a real `IVertexBufferBackend&`, but `CreateVertexBuffer`/`CreateIndexBuffer16` already throw (just confirmed above), so no real buffer can ever exist to pass them; they are structurally unreachable via any real call path, not merely untested. Confirmed correct by code review instead: both forward to `inner_` with the exact same one-line pattern as every other method here, and `SdlGraphicsBackend`'s own `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` already throw via `ThrowNo3D` (`DrawInstancedPrimitivesEx` isn't overridden by `SdlGraphicsBackend` either, so it uses `IGraphicsBackend`'s own default throw). |

## Phase G8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| ASCII-80 | `docs/ascii-backend.md`: mirror `docs/sdl-renderer-2d-completeness.md`'s structure — no `needs_human` column needed this time (Design decision 9) | ✅ | Written. Section 1 states plainly that `docs/sdl-renderer-2d-completeness.md`'s own table applies unchanged for everything `AsciiGraphicsBackend` forwards to `SdlGraphicsBackend`, rather than duplicating it; sections 2-6 cover only what's genuinely new (`ThrowNo3D` reuse, the quantizer, the font atlas, `Present()`'s grid draw incl. both real bugs found and fixed, input). |
| ASCII-81 | Full `CnaTests` regression run under `-DCNA_GRAPHICS_BACKEND=ASCII` | ✅ | Final run: **4364 pass / 2 skip / 11 pre-existing failures** (same exact baseline established at `ASCII-5` and reconfirmed after every phase since — zero regressions introduced across the whole plan). Full `Ascii_*` ctest suite: **9/9 for `Ascii_Present`** (extended by `ASCII-30`'s correction, see above), rest unchanged — **`Ascii_FontAtlas` 4/4, `Ascii_OffscreenTarget` 4/4, `Ascii_Quantizer` 13/13, `Ascii_Input` 3/3, `Ascii_ThrowNo3D` 17/17**. **Note on the `NOT WIN32` ctest-registration guard** (`CMakeLists.txt`'s `ASCII` test block): this is not an `ASCII`-specific gap — every other SDL/GL-family backend's own test block (`EASYGL`, `VULKAN`, `SDL_RENDERER`, `BGFX`, `WEBGPU`) uses the identical `CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT WIN32` guard, for the same reasons (the `-Wl,--start-group`/`--end-group` GNU/Clang-only linking trick these test macros use, and the `SDL_VIDEODRIVER=x11`/`DISPLAY` environment these tests are registered with). Design decision 8's "no platform gate" is about the *backend's own* portability (it is exactly as portable as `SDL_RENDERER`, since it wraps it) — it was never a claim that ctest registration itself is platform-uniform project-wide, and singling out `ASCII` to add Windows ctest registration while five other backends keep the same guard would be inconsistent, out-of-scope scope creep for this plan. |
| ASCII-82 | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` docstring and `../cna/plan.md`/`README.md` | ✅ | `CMakeLists.txt`'s docstring/`STRINGS` already listed `ASCII` since `ASCII-1` (Phase G1). `README.md` gained a backend bullet matching the existing `SDL_RENDERER`/`EASYGL`/.../`D3D12` bullets' own style, plus `docs/README.md`'s index gained an entry for `docs/ascii-backend.md`. **`../cna/plan.md` itself was not touched** — read it and confirmed it is a small (84-line) cross-cutting/deferred-task tracker, not a backend registry; it has no content this task is actually about updating. |

---

## Boundaries — explicitly out of scope

- **Real terminal/TTY output of any kind** — fully cancelled by the owner; not revisited by this
  plan.
- **A "this looks like real game art" claim** — §0.3 sets expectations honestly: a deliberate
  retro glyph-grid aesthetic, not a high-fidelity mode.
- **Any 3D pipeline** — permanent, inherited from `SDL_RENDERER`'s own 2D-only nature underneath.
