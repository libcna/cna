# ASCII Renderer — Completeness Status (historical)

> **Historical document (2026-08).** The `ASCII` graphics-renderer identity described below was
> removed from CNA's public renderer system: it was never a genuine terminal/TTY renderer, only a
> thin `SDL_RENDERER` decorator that quantized its own frame before presenting. Its reusable
> quantizer/font-atlas logic and visual behavior now live as a renderer-neutral post-process
> effect, `CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`), usable with any CNA
> renderer's `RenderTarget2D` output — see [`docs/ascii-post-process-effect.md`](ascii-post-process-effect.md).
> This document is preserved unchanged below as the historical completeness record for the former
> renderer; do not treat it as describing current CNA renderer identities.

`ASCII` is CNA's SDL-windowed retro glyph-grid graphics renderer: **not a real terminal/TTY
renderer** — it renders inside a normal SDL window, exactly like `SDL_RENDERER`, and is in fact
architecturally a thin decorator around `SDL_RENDERER`'s own `SdlRenderer`
(`plans/plan_ascii.md` design decision 2). The game draws normally into a private offscreen target
(`gameTarget_`); `Present()` reads that frame back, quantizes it into a grid of glyph+color cells,
and draws the grid onto the real window instead of the game's actual pixels.

This document is the completeness snapshot after `plans/plan_ascii.md` Phases G1–G8 (`ASCII-1`–`ASCII-82`).
See that file's own task table for full task-by-task verification detail.

**Status legend** (matches `docs/sdl-renderer-2d-completeness.md`'s own convention):

- ✅ — fully supported and pixel-verified.
- ⚠️-emulated — works, but via a renderer-specific accommodation rather than a native capability.
- ❌-throws-by-design — intentionally unsupported; throws a clear, specific exception (the
  `ThrowNo3D` convention, reused verbatim from `SDL_RENDERER` — see below) rather than silently
  misbehaving.

Unlike `docs/sdl-renderer-2d-completeness.md`, this renderer has **no `⛔ BLOCKED` section and no
`needs_human` gate anywhere** — every row below is automated and pixel-verified in this dev
environment (`plans/plan_ascii.md` design decision 9), including real-window `Present()` output. The
only genuinely subjective thing left is font/grid aesthetic choice, not functional correctness.

---

## 1. Everything `SDL_RENDERER` already does — inherited transparently

`SpriteBatch` (all 9 `Draw` overloads, sort modes, rotation/origin/flip/scale, `transformMatrix`),
`Texture2D` (`SetData`/`GetData`, `FromStream`, NPOT, `SaveAsPng`/`SaveAsJpeg`), `SpriteFont`
(single/multi-glyph, kerning, newlines, default-character fallback, flip/rotation), `BlendState`,
`SamplerState`, `RenderTarget2D`, and `Viewport`/`PresentationParameters`/`GraphicsDeviceManager`
all forward directly to the wrapped `SdlRenderer` (`plans/plan_ascii.md` design decision 2) —
**`docs/sdl-renderer-2d-completeness.md`'s own completeness table applies here unchanged**, row
for row, since the game's actual draws land on `gameTarget_` via that exact same, already-audited
implementation. This document does not repeat that table; it only covers what's genuinely new or
different about `ASCII`.

One partial exception: **`RenderTarget2D` binding semantics are extended, not changed.**
`SetRenderTarget(nullptr)` — XNA's "target the back buffer" idiom — is intercepted and redirected
to `gameTarget_` instead of the literal real backbuffer (`ASCII-20`), so the game can never
accidentally draw straight onto the real window. A genuinely explicit `RenderTarget2D` the game
creates itself is bound exactly as `SDL_RENDERER` already does, unaffected.

## 2. 3D pipeline — `ThrowNo3D`, reused verbatim

| Feature | Status | Rationale |
|---|---|---|
| `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ❌-throws-by-design | Forwards directly to `SdlRenderer`'s own `ThrowNo3D`-driven implementations — not re-declared. Verified by direct call (`Ascii_ThrowNo3D` ctest, `ASCII-60`). |
| `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ❌-throws-by-design | Same. |
| `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateOcclusionQuery` | ❌-throws-by-design | Same. |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawInstancedPrimitivesEx` | ❌-throws-by-design (unreachable, not just untested) | Forward with the identical one-line pattern as every other method here; confirmed correct by code review since no real `IVertexBufferRenderer`/`IIndexBufferRenderer` can ever exist to call them with (`CreateVertexBuffer`/`CreateIndexBuffer16` already throw first). See `plans/plan_ascii.md` `ASCII-60`'s own honest scope note. |
| `SupportsDepthStencil()` | `false` | Same 2D-only reality as `SDL_RENDERER`. |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube`/`CreateEffectRenderer` | `nullptr` | Never overridden by `SdlRenderer` either — `AsciiRenderer` correctly leaves them un-overridden too (same net behavior, less code). |

## 3. The glyph/color grid quantizer (`AsciiQuantizer`)

| Feature | Status | Rationale |
|---|---|---|
| Glyph selection by luminance rank | ✅ | Each cell averages the block of source pixels it represents, derives standard luma (`0.299R+0.587G+0.114B`), and picks a glyph from `kAsciiGlyphRamp` (`" .:-=+*#%@"`, darkest→densest) by linear rank. Pixel-verified strictly increasing density (0,2,4,6,12,20,24,40,48,60 of 64px) — `Ascii_FontAtlas` ctest. |
| `BlackWhite` mode | ✅ | Fixed white foreground, no background fill — glyph shape alone conveys brightness (the more "authentic ASCII art" convention). |
| `Color` mode (default) | ✅ | Foreground = the cell's own exact averaged color; background = that color at quarter brightness. A real, if simple, implementation of an "optional" background fill, not skipped. |
| `CNA_ASCII_MODE` env-var selection | ✅ | `BLACKWHITE`/`COLOR`, case-insensitive, defaults to `COLOR` on unset/unrecognized — mirrors `HEADLESS`'s own `ParseHeadlessModeFromEnvironment()` pattern exactly. `AsciiRenderer::SetMode()`/`GetMode()` also available programmatically. |
| Non-exact-multiple source sizes | ✅ | Grid rounds up (`ceil`); edge cells only average the real remaining pixels, never out-of-bounds memory. Verified with a 20px-wide image against 8px cells (`Ascii_Quantizer` ctest). |
| Cell pixel size | ✅ (runtime-configurable) | `AsciiRenderer::SetCellSize(int, int)`/`GetCellSize(int&, int&)` control how many `gameTarget_` pixels `Present()` averages per glyph cell (default 8×8, the font atlas' own glyph size); throws `std::invalid_argument` for non-positive sizes. Independent of the atlas' fixed 8×8 glyph texture and of the on-screen cell size, which is always `realWidth/columns`×`realHeight/rows` regardless. `Ascii_Present` ctest: `SetCellSize(16, 16)` on a 64×64 view measurably produces a 4×4 grid instead of 8×8. |
| Dirty-cell diffing (perf optimization) | ⬜ not implemented | Optional, perf-only (`ASCII-42`) — every frame fully re-reads, re-quantizes, and redraws the whole grid. Left for later if performance ever demands it; not a correctness gap. |

## 4. The font atlas (`AsciiFontAtlas`)

| Feature | Status | Rationale |
|---|---|---|
| Glyph coverage | ✅ (10 characters, by design) | Hand-authored 8×8 pixel data for exactly the 10 characters in `kAsciiGlyphRamp` — not a vendored CP437/font file, avoiding a new asset/license dependency entirely (`plans/plan_ascii.md` design decision 4). The quantizer only ever indexes by ramp position, never by arbitrary character, so a fuller font would be unused scope. Extending to more glyphs (or 8×16) later is a data-only change, no architecture change. |
| `BuildAsciiFontAtlas(GraphicsDevice&)` | ✅ | Real `SpriteFont` wrapper for application/diagnostic code that already has a `GraphicsDevice` (e.g. `DrawString` demos) — uses the same "app builds the atlas itself" constructor path every CNA `SpriteFont` test already uses; no XNB pipeline needed. **Not used by `Present()` itself** — see below. |
| `BuildAsciiFontAtlasImageData()` | ✅ | The lower-level, `GraphicsDevice`-free raw pixel version `Present()` itself uses internally (no XNA-level object available inside the renderer). Includes one extra solid-white slot (`kAsciiSolidGlyphIndex`) used only for background fills, never exposed as a `SpriteFont` character. |

**Why `Present()` doesn't call the real `SpriteFont`**: `AsciiRenderer` is a renderer
(`IGraphicsRenderer` implementation), and per this project's own layering rule renderers stay below
and hidden from the XNA public API — `SpriteFont`/`SpriteBatch` are built *on top of* renderers,
constructed from a `GraphicsDevice&` that itself wraps a renderer instance. `Present()` calling into
`SpriteFont::DrawString` would invert that dependency direction. Design decision 4's "same shape as
an ordinary `DrawString`" is satisfied in effect — one textured, tinted quad per glyph, same atlas,
same per-draw tint model — via the internal `ISpriteBatchRenderer`/`ITextureRenderer` pair instead,
which is the layer a renderer is actually supposed to operate in.

## 5. `Present()` — drawing the grid for real

| Feature | Status | Rationale |
|---|---|---|
| Reads `gameTarget_`, quantizes, draws the grid onto the real backbuffer, presents, rebinds `gameTarget_` | ✅ | `ASCII-40`. Cell destination rects are computed directly from `col*realWidth/columns` (not accumulated), so adjacent cells' shared edges always match exactly — no rounding gaps/overlaps. |
| Background fill + glyph tint compositing | ✅ (fixed — 1 real bug) | **Real bug found and fixed**: `presentSpriteBatch_` never goes through `GraphicsDevice::BlendState` (only the real XNA-level `SpriteBatch` does), so the SDL renderer's blend mode was left at whatever it happened to default to — not alpha-aware. Background fills were invisible: the glyph's transparent "off" pixels silently overwrote the background with the texture's stored black RGB instead of leaving it alone. Fixed by forcing real `BlendState.AlphaBlend` factors (`Blend::One`/`Blend::InverseSourceAlpha`/`BlendFunction::Add`) via `ApplyBlendState()` before every grid draw, independent of the game's own `BlendState` (`ASCII-40`). |
| Pixel-verification methodology | ✅ (1 real finding, not a bug) | Reading pixels immediately after a *real* `Present()` call returns garbage/stale content — SDL_Renderer/OpenGL presents via a genuine double-buffer swap, not a copy, so "the current render target" right after a swap is the buffer not drawn into this frame. Solved with a test-only `DrawQuantizedGridForTesting()` (draws, deliberately skips the swap and rebind) + `ReadRealBackbufferForTesting()`, so a test can read what was actually drawn before any swap could invalidate it (`ASCII-41`). Real game code always calls the real `Present()`, never these. |
| `Ascii_Present` ctest | ✅ | 4/4, using `PresentationMode::NativeBackBuffer` + a 64×64 virtual resolution (exact multiple of the 8×8 glyph cell) for pixel-exact sampling with no stretch/interpolation ambiguity. |

## 6. Input (`Mouse`/`Keyboard`/`GamePad`)

| Feature | Status | Rationale |
|---|---|---|
| Works completely unmodified | ✅ | Confirmed exactly as expected (`plans/plan_ascii.md` §0.2) — zero new code needed. A real window (unlike `HEADLESS`/`SOFTWARE`) means SDL's own input event pump and logical-presentation coordinate mapping (`SdlInputBridge`) just work, unaffected by the `gameTarget_` redirect (that redirect only changes which render target draws land on, never the renderer's own configured logical size, which is what mouse coordinate mapping actually keys off). |
| Verification | ✅ | Two independent proofs: the pre-existing, renderer-agnostic `MouseTest.SetPositionConvertsLogicalToWindowForLetterboxedRenderer`/`SetPositionHandlesLetterboxOffsetNotJustScale` both run and pass unmodified under `CNA_GRAPHICS_RENDERER=ASCII` (checked directly, not just inferred from the full suite); new dedicated `Ascii_Input` ctest (3/3): real window exists, `Mouse`/`Keyboard`/`GamePad.GetState()` don't throw, `Mouse.SetPosition()`/`GetState()` round-trips within 1px through the real coordinate-transform path. |
| Exposing the glyph-cell the mouse is over | ⬜ not implemented | Considered (`ASCII-51`'s own task text) but not added — no concrete need for it surfaced, and this project avoids speculative API surface added without one. |

---

## Boundaries — permanent, not v1-only gaps

- **No 3D pipeline** — inherited from `SDL_RENDERER`'s own 2D-only nature underneath.
- **Real terminal/TTY output of any kind** — the original direction was fully cancelled by the
  project owner; this renderer renders inside a normal SDL window, always.
- **A "this looks like real game art" claim** — a deliberate retro glyph-grid aesthetic (10-level
  luminance ramp, 8×8 cells), not a high-fidelity mode. Quantizing arbitrary existing sprite art
  into a coarse glyph grid is inherently lossy by construction.
