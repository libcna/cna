# HTML DOM Graphics Backend — Implementation Plan

> **Status: APPROVED 2026-07-31 — the project owner requested this backend directly** ("vytvoř html
> dom gr. backend pro cna … použije to html css js dom a jiné a mělo by to být optimalizované aby to
> bylo rychlé, bude to 2d only část xna 4.0 api bude házet not yet implemented například 3d") and
> approved every design recommendation below before implementation started.
>
> **Status legend** (this project's own convention): ✅ implemented *and verified against its stated
> acceptance criteria*; 🟨 code or documentation exists but has not met those criteria; ⬜ not
> implemented.

---

## What this backend is

`HTML_DOM` renders XNA `SpriteBatch` output as **real DOM elements styled with CSS** — one pooled
`<div>` per visible sprite, positioned with a CSS `transform`, textured with `background-image` +
`background-position`, and faded with `opacity`. There is no `<canvas>` in the sprite path at all:
the browser's own compositor does the drawing.

It is CNA's **fifteenth** graphics backend and its **second browser-native 2D-only** one, next to
`CANVAS`. The difference between the two is the drawing primitive, not the scope:

| | `CANVAS` | `HTML_DOM` |
|---|---|---|
| Draw primitive | `ctx.drawImage()` into one `<canvas>` | one styled `<div>` per sprite in the DOM |
| Who rasterizes | Canvas2D, per frame, every sprite | the browser compositor, only what changed |
| Cost of a static frame | full redraw | **zero** — no JS, no repaint |
| Cost of moving a sprite | full redraw | one `transform` write, composite only |
| Texture upload | `putImageData` (cheap) | PNG re-encode to a data URL (expensive) |
| Readback | `getImageData` on the main canvas | backbuffer readback impossible (see D6) |

That trade is the whole point of the backend, and it drives every design decision below.

`EASYGL` (WebGL2) remains the default Emscripten backend; this is an alternative for 2D games that
want DOM/CSS composition — accessibility tooling, DOM-based debugging, CSS filters, and a genuinely
idle main thread when nothing moves.

---

## Scope

**2D only.** `SpriteBatch`, `Texture2D`, `SpriteFont`, `RenderTarget2D`. Everything on the 3D
surface (vertex/index buffers, `DrawPrimitives`, depth/stencil clears, `Texture3D`, `TextureCube`,
`RenderTargetCube`, `Effect` compilation) throws
`std::runtime_error("HTML_DOM backend: … not yet implemented")` via the shared
`CNA::Internal::Backends::NotYetImplemented()` helper — the owner's explicit instruction.

**Emscripten-only**, hard-gated in CMake exactly like `CANVAS`: `document`, `HTMLDivElement` and
CSS do not exist in a native build.

---

## Design decisions

**1. Emscripten-only, hard `FATAL_ERROR` gate.** Same shape as `CANVAS`'s own gate
(`cmake/BackendSelection.cmake`). The `.cpp` files still compile natively (every `EM_JS` block sits
behind `#if defined(__EMSCRIPTEN__)`) so the pure-C++ logic stays unit-testable, but configuring
`-DCNA_GRAPHICS_BACKEND=HTML_DOM` without Emscripten fails loudly.

**2. Reuse SDL3's window; own the DOM overlay.** SDL3's Emscripten video driver already creates and
sizes a `<canvas>`; input, events and `SDL_GetWindowSize` all keep working. This backend adds its
own `<div id="cna-dom-root">` absolutely positioned over that canvas element, and hides the canvas
itself (`visibility:hidden` — not `display:none`, which would stop SDL sizing it). Everything this
backend draws lives inside that div.

**3. One pooled `<div>` per sprite, recycled across frames — never rebuilt.** The pool is an array
of elements appended once, in order, to the root container. Document order *is* paint order for
absolutely positioned siblings, so painter's-algorithm ordering comes for free with no `z-index`
bookkeeping. A frame activates a prefix of the pool: sprite *n* of the frame always lands on pool
element *n*. Elements past the frame's sprite count are hidden, and only those that were visible in
the previous frame are touched.

**4. Per-element style diffing; only `transform` and `opacity` in the steady state.** Every pool
element caches its last applied style values JS-side. A frame writes a property only when the value
actually changed. `transform` and `opacity` are compositor-only properties — a frame that just
moves sprites triggers no layout and no repaint. `will-change: transform` keeps each element on its
own compositor layer. Nothing in the draw path ever *reads* layout (no `getBoundingClientRect`), so
no forced synchronous reflow is possible.

**5. One JS call per batch, not per sprite.** `Draw()` appends a fixed-stride 80-byte POD command
into a C++ `std::vector`; `End()` hands the whole array to a single `EM_JS` call that walks it
through `HEAP32`/`HEAPF32` views. A 2000-sprite frame costs one wasm→JS boundary crossing, not
2000. This is the single most important performance decision in the backend.

**6. Textures become PNG data URLs, cached, regenerated only on `SetData`.** CSS
`background-image` needs a URL. The only *synchronous* canvas→URL route in a browser is
`canvas.toDataURL()` (`toBlob`/`convertToBlob` are async and would reorder draws), so each texture
is encoded once at upload time and cached JS-side. **Consequence, stated plainly:** a texture
re-uploaded every frame re-encodes every frame, which is slow. This backend is for static sprite
sheets; per-frame `Texture2D::SetData` belongs on `CANVAS` or `EASYGL`.

**7. Exact tint and blend via cached texture *variants*, not CSS filters.** A CSS `filter` chain
cannot express XNA's `color` tint exactly, and `CANVAS`'s own external review found that
compositing tricks silently produce wrong math. So each needed variant of a texture is generated
once by an exact per-pixel canvas pass and cached under its own data URL:
- *straight* — as uploaded (`NonPremultiplied`),
- *un-premultiplied* — RGB ÷ alpha (`AlphaBlend`, whose `srcBlend=One` contract assumes already
  premultiplied source data, exactly as `CANVAS` concluded from `SDL_RENDERER`'s Task 697 test),
- *alpha-stripped* — every alpha forced to 255 (`Opaque`, whose `srcBlend=One`/`dstBlend=Zero`
  ignores source alpha entirely),
- *tinted(r,g,b)* — RGB × tint, alpha untouched, on top of whichever of the three applies.

The variant cache is LRU-capped (256 entries) so an animated per-frame tint degrades into cache
churn rather than unbounded memory growth.

**8. `Additive` maps to `mix-blend-mode: plus-lighter`.** That is CSS Compositing's exact
`lighter` operator — the same one `CANVAS` uses. Any `BlendState` outside the four standard presets
throws, same honest boundary `CANVAS` drew: CSS has no generic blend-factor/equation model.

**9. `Clear()` resets the frame.** `Clear()` sets the root container's `background-color` and drops
every sprite queued so far this frame (pool cursor back to 0). That is exactly XNA's semantics —
`Clear` overwrites everything already drawn — expressed in retained-mode terms. Called while a
render target is bound, it clears that target's canvas instead.

**10. `RenderTarget2D` is backed by a real off-screen `<canvas>` (hybrid).** DOM has no render
targets, and refusing them would break every 2D game that uses one. While a target is bound, draws
route through a Canvas2D path (`drawImage`, the same geometry `CANVAS` derived); when the target is
later sampled as a texture, its data URL is regenerated lazily from a dirty flag. `Present()` and
the DOM sprite path are untouched by this.

**11. Backbuffer readback is impossible and says so.** There is no browser API to rasterize a live
DOM subtree to pixels. `ReadBackbuffer` throws unless a render target is bound (in which case it
reads that target's canvas for real). This is the one capability `CANVAS` has and `HTML_DOM`
genuinely cannot — stated here, in `docs/html-dom-backend.md`, and in the exception message.

**12. `SpriteFont` needs zero backend code.** Every glyph funnels through the same
`ISpriteBatchBackend::Draw` overload; kerning, line spacing and glyph-order mirroring are all
shared, backend-agnostic `SpriteFont.cpp` logic — the same conclusion `CANVAS` reached.

---

## Phases and tasks

### D1 — Build wiring and skeleton

| Task | Status | Description |
|---|---|---|
| HTMLDOM-1 | ✅ | `CNA_GRAPHICS_BACKEND=HTML_DOM` option, cache STRINGS entry, `CNA_BACKEND_HTML_DOM` option and define (`cmake/BackendSelection.cmake`). |
| HTMLDOM-2 | ✅ | Emscripten-only `FATAL_ERROR` gate (design decision 1). |
| HTMLDOM-3 | ✅ | `BACKEND_DIR`/`BACKEND_TARGET` = `src/CNA/Internal/Backends/HtmlDom` / `cna_backend_graphics_html_dom`; links SDL3 only (`cmake/BackendLibraries.cmake`). |
| HTMLDOM-4 | ✅ | `GraphicsBackendType::HtmlDom` + name mapping + `getCurrentGraphicsBackendType()` branch. |
| HTMLDOM-5 | ✅ | `HtmlDomGraphicsBackend` class + `CreateGraphicsBackend()` factory. |
| HTMLDOM-6 | ✅ | `SupportsCapability()` → false for every `GraphicsCapability` (2D-only), `SupportsDepthStencil()` → false. |
| HTMLDOM-7 | ✅ | 2D-only backend guards in the shared example/GTest files that enumerate such backends. |

### D2 — DOM surface, Clear, Present, viewport

| Task | Status | Description |
|---|---|---|
| HTMLDOM-10 | ✅ | `CNA_HtmlDom_EnsureRoot()` — creates `<div id="cna-dom-root">` over SDL's canvas, hides the canvas, sets `overflow:hidden`/`transform-origin:0 0`/`contain:strict`. |
| HTMLDOM-11 | ✅ | `Clear()` → root `background-color` + frame reset (design decision 9); routes to the bound render target when one is bound. |
| HTMLDOM-12 | ✅ | `Present()` → flush any pending batch, hide unused pool elements, apply the logical→physical container scale. Nothing to swap: the compositor presents. |
| HTMLDOM-13 | ✅ | `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode` — the same `FixedHeightDynamicWidth` logical-size math every other backend uses. |
| HTMLDOM-14 | ✅ | `TransformWindowToLogical`/`TransformLogicalToWindow` for correct mouse mapping under scaling. |
| HTMLDOM-15 | ✅ | Structural smoke test (`examples/htmldom_smoke_test.cpp`) + `cmake/Tests/HtmlDomTests.cmake`. |

### D3 — Textures

| Task | Status | Description |
|---|---|---|
| HTMLDOM-20 | ✅ | `HtmlDomTextureBackend` — off-screen canvas per texture, integer id in `Module['cnaDomTextures']`, PNG data URL generated once (design decision 6). |
| HTMLDOM-21 | ✅ | `UpdatePixels` re-uploads and invalidates every cached variant of that texture. |
| HTMLDOM-22 | ✅ | `UpdatePixelsLevel(level>0)` throws — no mip chain exists here (same boundary as `CANVAS`/`SDL_RENDERER`). |
| HTMLDOM-23 | ✅ | Variant generation + LRU cache (straight / un-premultiplied / alpha-stripped / tinted), design decision 7. |
| HTMLDOM-24 | ✅ | `Texture2D::GetData` on a plain texture is served by the shared CPU shadow — no backend work needed. Closed by HTMLDOM-96a (D14, below), which added a byte-exact browser check for exactly this path — this row's own status was left stale at the time and is corrected here. |

### D4 — SpriteBatch

| Task | Status | Description |
|---|---|---|
| HTMLDOM-30 | ✅ | Fixed-stride draw-command buffer, single `EM_JS` flush per batch (design decision 5). |
| HTMLDOM-31 | ✅ | Element pool with per-index reuse and prefix activation (design decision 3). |
| HTMLDOM-32 | ✅ | Per-property style diffing (design decision 4). |
| HTMLDOM-33 | ✅ | Sprite geometry: `translate(destX,destY) rotate(θ) scale(destW/sw,destH/sh) translate(-originX,-originY)` — places `origin` (source-pixel space) exactly at `(destX,destY)`, invariant under rotation, matching FNA's `GenerateVertexInfo`. |
| HTMLDOM-34 | ✅ | `SpriteEffects` flips mirror about the sprite's own local centre, leaving the destination footprint unchanged (real XNA semantics). |
| HTMLDOM-35 | ✅ | `sourceRectangle` → `background-position` + element size; source rects are clamped into the texture. |
| HTMLDOM-36 | ✅ | `Begin(transformMatrix)` → a CSS `matrix(M11,M12,M21,M22,M41,M42)` on a batch wrapper element, composed under each sprite's own transform. |
| HTMLDOM-37 | ✅ | `SetCustomEffect(non-null)` throws — no programmable shader stage exists. |
| HTMLDOM-38 | ✅ | `SpriteFont::DrawString` needs no backend-specific code (design decision 12) — every glyph funnels through the same `Draw` overload. Verified in the browser run: a one-glyph font's `DrawString` produces a real DOM element, textured from a generated PNG data URL, sized from the glyph's own atlas bounds. |

### D5 — Blend and sampler state

| Task | Status | Description |
|---|---|---|
| HTMLDOM-41 | ✅ | `BlendStateToDomCompositeOp()` — pure function, unit-tested without a DOM; throws for non-preset states (design decision 8). |
| HTMLDOM-42 | ✅ | `Opaque` → alpha-stripped variant; `AlphaBlend` → un-premultiplied variant; `NonPremultiplied` → straight; `Additive` → `mix-blend-mode: plus-lighter`. |
| HTMLDOM-43 | ✅ | Colour tint → cached tinted variant; alpha → `opacity` (free). |
| HTMLDOM-44 | ✅ | `TextureFilter` → `image-rendering: pixelated` vs `auto`, using the same magnification-dominant grouping `SDL_RENDERER` Task 701 and `CANVAS` CANVAS-42 use. |
| HTMLDOM-45 | ✅ | `Clamp` is exact and unit-tested; `Mirror` and mixed per-axis modes throw (unit-tested). `Wrap` maps to CSS background repetition on the DOM path — verified in the browser run: a texture drawn with a source rectangle double its own size under `SamplerState.PointWrap` keeps its full (unclamped) element width and gets `background-repeat: repeat`. The render-target path's separate Canvas2D-repeating-pattern implementation is verified too, pixel-exact: a 2x2 source tiled into a 4x4 render target under Wrap reads back with every texel matching `source(x%2, y%2)` exactly. **Superseded by HTMLDOM-97 (Phase D15)**: "Mirror and mixed per-axis modes throw" was this task's original, narrower scope — symmetric `Mirror` and non-`Mirror` mixed per-axis modes (e.g. U=Wrap, V=Clamp) are both real, pixel-verified now; only `Mirror` mixed with a DIFFERENT mode on the other axis still throws. |
| HTMLDOM-46 | ✅ | `ColorWriteChannels`/`MultiSampleMask` documented as inexpressible in CSS compositing. |

### D6 — Render targets

| Task | Status | Description |
|---|---|---|
| HTMLDOM-50 | ✅ | `HtmlDomRenderTargetBackend` — off-screen canvas, `Bind`/`UnbindAsRenderTarget` (design decision 10). |
| HTMLDOM-51 | ✅ | Draws route to Canvas2D while a target is bound; back to the DOM path on unbind. |
| HTMLDOM-52 | ✅ | Target content is re-published as a data URL lazily, from a dirty flag set on every bind. |
| HTMLDOM-53 | ✅ | `GetData` — real synchronous `getImageData`, with the full argument validation contract (REMED-GFX-127). |
| HTMLDOM-54 | ✅ | `HasRealDepthBuffer()` → false; `SetRenderTargets(count>1)` and cube-face bindings throw. |
| HTMLDOM-55 | ✅ | `ReadBackbuffer` throws for the DOM backbuffer, reads for real from a bound target (design decision 11). |

### D7 — The 3D surface

| Task | Status | Description |
|---|---|---|
| HTMLDOM-60 | ✅ | Every depth/stencil clear, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, vertex/index buffer creation and `Draw*Primitives` throws `"HTML_DOM backend: … not yet implemented"`. |
| HTMLDOM-61 | ✅ | `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube`/`CreateOcclusionQuery`/`CreateEffectBackend` keep `IGraphicsBackend`'s own null/throw defaults — the same choice `CANVAS` made deliberately in Phase C7. |

### D8 — Verification and documentation

| Task | Status | Description |
|---|---|---|
| HTMLDOM-70 | ✅ | GTest coverage for every pure-C++ unit: blend mapping, address-mode validation, command encoding, 3D throw surface. |
| HTMLDOM-71 | ✅ | Real Emscripten build (`emcmake`, emsdk 6.0.5). |
| HTMLDOM-72 | ✅ | **Real browser verification** — 24/24 checks pass in headless Chromium via Playwright, asserting against the DOM/pixels the backend actually produced (surface, clear colour, sprite transforms/sizes/opacity/data-URL backgrounds, element recycling, a `RenderTarget2D` readback round-trip, the backbuffer refusal, `SpriteFont::DrawString`, `TextureAddressMode::Wrap` on both the DOM path and the render-target's separate Canvas2D-pattern path, the latter checked pixel-exact). This is what `CANVAS` could never do in its own dev loop — and it earned its keep immediately: see the note below. |
| HTMLDOM-73 | ✅ | `docs/html-dom-backend.md` capability/limitation matrix. |

### D9 — Closing the gap between "implemented" and "verified"

The owner reviewed D1-D8 and pushed back: 24 browser checks is real coverage, but it is not
exhaustive, and several rows marked ✅ above were verified only in the sense of "the code exists,
was reviewed, and nothing threw" — not "the actual rendered output was checked against a
hand-derived expected value." This phase closes that gap, item by item, against the owner's own
list. Several items required a real design decision before they could be scoped as tasks at all;
those decisions are recorded inline.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-80 | ✅ | Implemented as `clip-path: inset(...)`, applied unconditionally (matching `SDL_RENDERER`'s own `RasterizerState.ScissorTestEnable`-independent behaviour). Verified in `htmldom_smoke_test.cpp` with three cases: a rect fully inside the surface maps to the exact `inset(top,right,bottom,left)` values; the full-surface rect produces an all-zero (no-op) inset; a rect extending past the surface's own bounds clamps its far insets to 0 instead of producing a negative (expanding) `inset()`. Verification compares parsed numeric inset values rather than the raw CSS string, since the CSSOM normalizes `inset(0px 0px 0px 0px)` down to the `inset(0px)` shorthand when all four values are equal — caught by the first attempt's own failing run, not assumed. **Original scope was honestly whole-surface/current-value (applied to a single `clip-path` on `#cna-dom-root`); superseded by HTMLDOM-94 (Phase D12) below**, which replaced that single clip with per-scissor-rect regions so an earlier batch's clip survives a later batch's different rect — the three cases this task verified still hold, now against a region's own clip-path instead of root's. |
| HTMLDOM-81 | ✅ | **Confirmed non-gap, documented rather than implemented.** No 2D-only sibling backend implements `SetViewport` either (`SDL_RENDERER`, `CANVAS`, `DX3` all leave it as the inherited no-op; only 3D-capable backends — `EASYGL`, `D3D11`/`D3D12`/`D3D9`, `Bgfx`, `SdlGpu`, `Headless`, `Software` — override it, since `Viewport` is fundamentally the NDC-to-screen transform a rasterizer pipeline needs and this backend has none). Downgraded from "gap" to "confirmed non-gap, matching every comparable sibling" — the reasoning is on record here rather than silently assumed, which is what "done" means for this task. **Superseded by HTMLDOM-98 (Phase D15)**: this task's own non-gap finding was scoped against 2D-only siblings only, stated explicitly at the time — a real, closeable gap against `EASYGL` specifically remained, and is now implemented; see HTMLDOM-98. |
| HTMLDOM-82 | ✅ | Verified pixel-exact in `examples/htmldom_pixel_verification_test.cpp` with two deliberately unambiguous (no rotation sign-convention risk) matrices: a pure translation `Matrix::CreateTranslation(6,8,0)` moves a 1x1 sprite from the origin to exactly (6,8); a pure `Matrix::CreateScale(2)` renders it as a 2x2 block instead of 1x1. Both drawn into a bound render target, exercising `transformMatrix` on the Canvas2D `targetCtx` branch specifically — a separate code path from the DOM branch the smoke test's own CSS-string check covers. |
| HTMLDOM-83 | ✅ | Verified pixel-exact in `examples/htmldom_pixel_verification_test.cpp`: cleared RT A to a distinct colour, unbound it, `Draw()` RT A itself (an ordinary `RenderTarget2D : Texture2D`) onto RT B, unbound, read B back — the sampled region matches RT A's real content exactly (within 1-unit rounding) and the surrounding area stays untouched. Confirms the data-URL regenerated-from-a-dirty-flag path (design decision 10) round-trips real content, not just that the call sequence doesn't throw. |
| HTMLDOM-84 | ✅ | Colour tint verified pixel-exact in the browser: a (200,150,100,255) texel tinted (255,128,64,255) under `AlphaBlend` over a transparent target reads back (200,75,25) — exactly `round(src*tint/255)` per channel, cross-checked against the same formula computed independently in C++, not restated from the JS. See `examples/htmldom_pixel_verification_test.cpp`. |
| HTMLDOM-85 | ✅ | **The highest-risk item on this list, now closed.** A hand-premultiplied texel (straight colour (255,100,50) at alpha 128, uploaded as (128,50,25,128), mirroring `SDL_RENDERER`'s own Task 697 test) drawn under `AlphaBlend` reads back (255,100,52,128) — within the expected double-rounding tolerance (this backend's own division, plus the browser's own internal premultiply/un-premultiply round trip on readback) of the exact expected (255,100,50,128). The un-premultiply maths reconstructs the original straight colour correctly; it is not double-dividing, not skipping the divide, and not off by a swapped operand. |
| HTMLDOM-86 | ✅ | Verified with genuinely semi-transparent source data (180,90,40,120) — every prior check used alpha=255 source, making the strip a no-op every time it ran until now. Originally read back (181,89,40,255): alpha forced to 255 exactly, RGB within 1-unit rounding of the source, unaffected by the transparent destination it was drawn over — confirming the strip does not blend before forcing alpha. **Superseded by HTMLDOM-100 (Phase D15)**: that forced-255 result was itself later found to diverge from real XNA (`BlendState::Opaque` preserves source alpha exactly, confirmed via `BlendState.cpp`) — fixed on the Canvas2D render-target path (which is what this test exercises), now reading back at alpha≈120, the source's own value. This row is kept as a record of what the original implementation did, not what it does now. |
| HTMLDOM-87 | ✅ | Verified with two fully opaque, colour-disjoint draws (pure red, then pure blue) at the identical destination pixel under `BlendState.Additive`: reads back exact (255,0,255,255) — a real channel-wise sum (clamped), not a plain overwrite (which would read back as just blue) or an average (which would read back as a dim purple). |
| HTMLDOM-88 | ✅ | Verified in `examples/htmldom_pixel_verification_test.cpp` with a two-glyph, distinct-kerning, two-colour-half font: `\n` correctly resets X and advances Y by `lineSpacing` (not smearing onto the same line); same-line kerning advance places the next glyph at exactly `glyphWidth+rightBearing`, with a real gap in between; the rotation/scale/flip `DrawString` overload's `scale` parameter correctly resizes the rendered glyph (4x4 → 8x8, checked via a pixel only inside the scaled extent); its `SpriteEffects::FlipHorizontally` reverses the two halves' relative order (checked as an observable invariant — red left-of-blue vs. right-of-blue — rather than an assumed absolute position, since `DrawString`'s own flip handling shifts the whole string's anchor by `MeasureString(text).X` in addition to the per-glyph mirror, a real, previously-unknown-to-this-plan detail of the *shared* `SpriteBatch.cpp` layer, not an HTML_DOM-specific behaviour). |
| HTMLDOM-89 | ✅ | Measured in `examples/htmldom_stress_test.cpp`: 500 animated sprites/frame (position and tint both changing every frame, the genuine steady-state case), averaged over 60 frames after a warm-up frame, came to **2.338 ms/frame (~428 fps-equivalent)** in this headless-Chromium container — well inside the generous 50ms sanity threshold, confirming no accidental per-sprite JS call or O(n²) DOM cost crept in. Honestly scoped as absolute numbers on this machine, not a comparative benchmark against `CANVAS`/`EASYGL`. |
| HTMLDOM-90 | ✅ | Verified in `examples/htmldom_stress_test.cpp` with 300 frames of oscillating sprite counts (5-50/frame) and a tint continuously cycling through far more than 256 distinct values: the sprite pool stayed exactly bounded at the true peak (500, from the benchmark phase) with zero leak, and the variant LRU cache capped at exactly 256 — real eviction confirmed, not merely "never filled up". Zero console errors/exceptions over the whole run (the harness itself fails the run on any pageerror or `[FAIL]` line). |
| HTMLDOM-91 | ✅ | Verified reasoning converted into `HtmlDom3DSurfaceTest.InertStateSettersAcceptArbitraryValuesWithNoObservableEffect`: `ApplyDepthStencilState`/`SetReferenceStencil` are meaningless because `SupportsDepthStencil()` is unconditionally `false`; `SetBlendFactor`'s constant colour can only matter for `Blend.BlendFactor`/`InverseBlendFactor`, and `BlendStateToDomCompositeOp` already throws for both before `ApplyBlendState` could ever consume that colour; `ApplyRasterizerState`'s `CullMode`/`FillMode`/depth-bias fields have no 2D analogue (matching every 2D-only sibling), and its `scissorTestEnable` field is confirmed not to leak into a subsequent `ApplyBlendState` call's own composite-op state. Each setter is called with a deliberately non-default value and asserted to neither throw nor change any state a caller could later observe. |

### D10 — Comparative cross-backend benchmark

HTMLDOM-89 measured this backend's own steady-state cost in isolation. The owner asked for the
number that actually matters: how it compares to the sibling backends, measured with the identical
workload rather than compared against each backend's own separately-written test.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-92 | ✅ | Added `examples/graphics_backend_benchmark.cpp` — a genuinely backend-agnostic source file (no `CNA::Internal::Backends::*` include; identifies its own backend at runtime via `CNA::getCurrentGraphicsBackendName()`) and `cmake/Tests/CrossBackendBenchmark.cmake`, gated on `EMSCRIPTEN` alone so the same source builds under any Emscripten backend configure. Same methodology as HTMLDOM-89: 500 sprites/frame, position and tint both animating every frame, 60 timed frames after one untimed warm-up frame, `performance.now()`-based. Built and run for real in headless Chromium (Playwright) against all three Emscripten-capable backends from three separate `emcmake` configures (`cmake-build-canvas`, `cmake-build-easygl`, `cmake-build-htmldom`): **`HTML_DOM` 3.7–4.3 ms/frame (≈233–270 fps-equivalent)**, **`EASYGL` 4.66 ms/frame (≈215 fps-equivalent)**, **`CANVAS` 27.6–29.1 ms/frame (≈34–36 fps-equivalent)**. `HTML_DOM` is on par with (in this run, marginally faster than) `EASYGL` and roughly 6–7× faster than `CANVAS` for this specific 500-static-texture-sprite workload. Honest caveat: `EASYGL`'s number is software-rasterized (SwiftShader/ANGLE under headless Chromium, no real GPU in this container) — a real hardware WebGL2 context would very likely widen `EASYGL`'s advantage over both 2D backends; this result should be read as "DOM compositing beats Canvas2D redraw by a wide margin, and is competitive with software-rasterized WebGL2," not as a claim that `HTML_DOM` outperforms real hardware-accelerated WebGL2. A real bug was found and fixed while getting `EASYGL`'s number: `CrossBackendBenchmark.cmake` initially omitted `-sMIN_WEBGL_VERSION=2`/`-sMAX_WEBGL_VERSION=2` (present on the pre-existing `cna_house3d_demo` EASYGL/Vulkan target but not copied to this new one), so Emscripten silently created a WebGL1 context despite `EasyGLGraphicsBackend`'s own `SDL_GL_CONTEXT_MAJOR_VERSION=3` request, which then failed to compile EasyGL's GLSL ES 3.0 shaders (`ERROR: unsupported shader version`) and aborted on `glGenVertexArrays`. Fixed by adding the same two link options, conditioned on `CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"`. |

### D11 — Window resize + active scissor rect interaction

| Task | Status | Description |
|---|---|---|
| HTMLDOM-93 | ✅ | Investigated whether a physical window resize with no `GraphicsDevice.Reset()` in between leaves a previously-set `ScissorRectangle`'s `clip-path` insets stale (computed against the old surface size, silently wrong once `#cna-dom-root` itself resizes). Two real findings, one at each layer: **(1) Framework-level: confirmed non-gap.** `GraphicsDevice::UpdateViewportFromWindow()` (called from every `GraphicsDevice::Present()`, for every backend — not HTML_DOM-specific) already resets both `Viewport` and `ScissorRectangle` to the complete new backbuffer the instant it detects any viewport-size change, matching FNA's own real-resize behaviour — so through the public XNA API this staleness can never actually manifest; by the time a game could observe it, `GraphicsDevice` has already reset the scissor itself. Same shape of finding as HTMLDOM-81's `SetViewport` one. Confirmed by first reproducing the scenario through `GraphicsDevice::setScissorRectangleProperty()`/`Present()` and observing the framework's own reset win the race, not assumed. **(2) Backend-level: a real latent gap, fixed.** `HtmlDomGraphicsBackend`'s own contract was still wrong in isolation: `SetScissorRect` computed `clip-path` insets once, using whatever `Module['cnaDomLogicalW'/'H']` held at call time, and never revisited them — a caller reaching the backend directly (bypassing `GraphicsDevice`, as e.g. a future MRT/split-screen path or a test might) would see the clip silently drift after a resize. Fixed in `HtmlDomGraphicsBackend.cpp`: `SetScissorRect` now stores the rect in logical coordinates (`Module['cnaDomScissorRect']`) instead of only its derived insets, and `CNA_HtmlDom_UpdateSurface` re-derives the `clip-path` from that stored rect (via a shared `Module['cnaDomApplyScissorClip']` helper) every time it detects the logical size changed — so the backend's own contract holds regardless of whether `GraphicsDevice` is in the loop. `EnsureRoot`/`DestroyRoot` clear the stored rect alongside the rest of the per-root state. Verified in `htmldom_smoke_test.cpp` by calling `HtmlDomGraphicsBackend::SetScissorRect`/`SetVirtualResolution(0,0)`/`Present()` directly (bypassing `GraphicsDevice`, which is what makes the fix observable at all — see the framework-level finding above): a `(5,5,10,10)` rect set against a 64×64 surface correctly re-derives to `(5,113,113,5)` after a real `SDL_SetWindowSize` to 128×128, with no `GraphicsDevice.Reset()` and no scissor reapplication. (Superseded/generalized by HTMLDOM-94 immediately below: the single root-level `Module['cnaDomApplyScissorClip']` this task added was replaced by a per-region `Module['cnaDomApplyRegionClip']`, applied to every region on resize instead of just one; the underlying resize-safety property this task established is unchanged and is what HTMLDOM-93's own two smoke-test checks now verify against a region instead of against root directly.) |

### D12 — True per-batch scissor rect (nested region containers)

Design decision 13/HTMLDOM-80 had shipped a deliberately scoped "whole-surface, current-value"
clip: one `clip-path` on `#cna-dom-root` reflecting whatever `ScissorRectangle` was *most recently*
set, applied to *every* sprite currently on screen — so a game drawing under rect A, then changing
`ScissorRectangle` to rect B and drawing more, would see the LATER rect B retroactively reclip the
EARLIER sprites drawn under rect A too. This phase closes that gap for real.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-94 | ✅ | **Scope, precisely stated first.** Real XNA/FNA `SpriteBatch` in `Deferred` sort mode (this backend's only sort mode) does not itself preserve per-original-`Draw()`-call device state either: `ScissorRectangle` is read by the GPU once the batch's queued draw calls are actually issued, at `End()` — a game that changes `GraphicsDevice.ScissorRectangle` between two `Draw()` calls inside the same still-open `Begin()`/`End()` pair gets the rect current AT `End()` applied to the WHOLE batch, not a per-call value. So the correct, non-overclaiming target is per-**batch** (per `Begin()`/`End()` pair) scissor correctness, matching this backend's own existing one-flush-per-batch architecture — not literally per individual `Draw()` call, which isn't even a real XNA/FNA guarantee for `SpriteBatch.Deferred`. **Implementation.** Replaced the single flat sprite pool under `#cna-dom-root` with a small set of per-scissor-rect DOM "regions" (`Module['cnaDomRegions']`, keyed by `"x,y,w,h"`), each a `<div>` with its own `clip-path` and its own independent sprite pool/recycling state. `CNA_HtmlDom_SetScissorRect` (`HtmlDomGraphicsBackend.cpp`) now only records the current rect (`Module['cnaDomScissorRect']`) — it no longer touches the DOM. `CNA_HtmlDom_FlushSprites` (`HtmlDomSpriteBatchBackend.cpp`, called once per `SpriteBatch::End()`) resolves that rect to a region via `Module['cnaDomGetRegion']` and appends/recycles that batch's sprites into it, so an EARLIER batch's region is never touched by a LATER batch's different rect. A rect that currently covers the whole surface (checked fresh against `Module['cnaDomLogicalW'/'H']` on every call, so this stays correct across resizes with no special-casing) collapses to the SAME default `'full'` region whose container IS `#cna-dom-root` itself — so the overwhelmingly common no-scissor case costs exactly what it always did (confirmed: `cna_test_htmldom_stress`'s HTMLDOM-89 number is unchanged within noise, 3.5-3.9 ms/frame). Non-default regions persist across frames (like the sprite pool always has) and are LRU-evicted at a 16-region cap if a game genuinely cycles through many distinct rects, mirroring the texture-variant cache's own eviction shape. `Clear()`/`PresentFrame()` iterate every region instead of a single pool. `CNA_HtmlDom_UpdateSurface` re-derives EVERY region's clip on resize (generalizing HTMLDOM-93's fix from one root-level clip to N per-region ones). **Documented trade-off, not a regression.** Sprites in different regions paint in the order their regions were first CREATED, not necessarily interleaved per-frame draw order across regions — cross-region z-order is stable frame-to-frame but doesn't reconstruct true draw order for a game that interleaves draws across different scissor rects in a different sequence each frame. This is an accepted, honestly-documented boundary: before this task there was no way to keep two different scissor rects from clipping each other's sprites AT ALL, so this trades an edge-case ordering caveat for real clipping correctness in the overwhelmingly more common case (scissor rects are typically spatially disjoint UI regions that don't visually overlap, where cross-region paint order is inconsequential). **Verified in `htmldom_smoke_test.cpp`** (27 → 32 checks, rewritten HTMLDOM-80a/b/c to draw-and-flush a sprite per rect instead of checking root's clip-path with nothing drawn, since a region now only exists once a batch actually flushes under it): the money check draws sprite A under rect (10,10,20,30) in one batch, then sprite B under a reset-to-full rect in a SECOND batch, and confirms rect A's region STILL has its own correct clip and its sprite is STILL visible — completely unaffected by the second batch's different rect. HTMLDOM-93's own two checks were adapted to the new region shape (`JsRegionClipPathInsetIs` replacing `JsRootClipPathInsetIs`) with no change to what they actually prove. Full regression pass: 32/32 smoke, 11/11 pixel, 3/3 stress (measured 3.857 ms/frame, no perf regression), 33/33 GTest, and the visual demo screenshot unchanged. |

### D13 — Texture/render-target dispose verification

`HtmlDomTextureBackend::~HtmlDomTextureBackend()` (calls `CNA_HtmlDom_DestroyTexture`, deleting
`Module['cnaDomTextures'][id]`) and `HtmlDomRenderTargetBackend::~HtmlDomRenderTargetBackend()`
(unbinds itself if it was the currently-bound target) had never been exercised end-to-end in a real
browser — only reviewed by reading the source. This phase proves the JS-side registry actually
shrinks back down, not merely that the C++ looks correct.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-95 | ✅ | New `examples/htmldom_dispose_test.cpp` / `cna_test_htmldom_dispose` page, 6 checks. **95a**: creating 50 `Texture2D`s registers exactly 50 new `Module['cnaDomTextures']` entries; destroying all 50 (clearing the owning vector) removes exactly those 50, none orphaned. **95b**: same shape for 20 `RenderTarget2D`s (each owns one texture canvas internally). **95c**: the LAST render target is bound (`GraphicsDevice.SetRenderTarget`), drawn into, and then the whole vector — including that still-bound target — is destroyed with **no** explicit unbind first; `CNA::Internal::Backends::HtmlDom::GetBoundRenderTargetIdEXT()` reads back `0` immediately afterward, confirming `~HtmlDomRenderTargetBackend()`'s defensive unbind genuinely fires rather than leaving the draw path pointing at a canvas that no longer exists. A normal `Clear()`+`Draw()` to the ordinary backbuffer right after (the real, observable consequence of 95c holding) completes without throwing or silently doing nothing. **Found and fixed a real bug in the TEST itself, not the backend, while writing this**: destroying a still-bound render target without first calling `GraphicsDevice.SetRenderTarget(null)` leaves `GraphicsDevice`'s own separate `renderTargetBound_` flag (unrelated to the backend's own bound-canvas-id tracking HTMLDOM-95c checks) stuck `true`, and the framework's own `GraphicsDevice::Present()` guard (`"Cannot present while render targets are bound"`) then throws on the very next frame's automatic `Game::EndDraw()`-triggered `Present()` call — a real requirement of the public API (always call `SetRenderTarget(null)` before disposing a bound target), not a defect this task needed to fix. The test now does so, deliberately positioned AFTER the 95c check so that check still exercises the destructor's own defensive unbind in isolation. **95d**: 200 rapid create-then-immediately-destroy cycles (monotonically-increasing, never-reused texture ids) leave the registry exactly where it started — bounded correctness under churn, not just a single before/after snapshot. Full regression pass after adding this: 32/32 smoke, 11/11 pixel, 6/6 dispose. |

### D14 — Closing the last two 🟨 rows

Two capability-matrix rows had never been exercised end-to-end under this backend specifically:
`Texture2D::GetData` on a plain (non-render-target) texture, and `Texture2D::FromStream` decode.
Both are entirely shared/backend-agnostic C++ (the former reads `Texture2D`'s own `cpuPixels_`
CPU-side shadow directly, never touching a backend at all; the latter's decode step is `stb_image`
via `SaveAsPng`/`FromStream`, identical on every backend) — so closing them out is specifically
about proving the two backend-touching *ends* of those pipelines behave correctly under this
backend's real Emscripten canvas/PNG-data-URL upload machinery, not about writing new backend code.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-96 | ✅ | Added to `examples/htmldom_pixel_verification_test.cpp` (11 → 13 checks). **96a**: a plain `Texture2D` is created via `CreateFromPixels` with four distinct, unambiguous per-texel colours (including a non-255 alpha, ruling out both a wrong-channel and a wrong-texel-order bug), and `GetData()` is checked byte-exact against the source — `CreateFromPixels`/`GetData` is a lossless path with no codec involved, so no rounding tolerance is needed. **96b**: a source texture is created and uploaded through this backend, encoded to a real PNG via `SaveAsPng` (into a `System::IO::MemoryStream`), decoded back via `Texture2D::FromStream` (re-uploading through this backend's real canvas/data-URL machinery a second time, exercising the SAME upload path a fresh game asset load would), and read back matching the original colour within PNG's own lossless-but-8-bit-rounded tolerance. Both `docs/html-dom-backend.md` rows move from 🟨 to ✅ — this closes the last two 🟨 rows on that page. Full regression pass: 32/32 smoke, 13/13 pixel, 6/6 dispose, 33/33 GTest. |

### D15 — Closing the gap with `EASYGL`, where it's real and worth closing

The owner asked what `HTML_DOM` can't do that `EASYGL` (this project's default, 3D-capable WebGL2
backend) can. Read both backends' real source rather than guessing. Most of the gap is `EASYGL`
being 3D-capable at all — out of scope by explicit owner mandate (Scope, above), not a real gap.
Within the 2D-relevant surface both backends actually share (`SpriteBatch`/`Texture2D`/
`RenderTarget2D`/`BlendState`/`SamplerState`), most of the rest turned out to be **permanent**
architectural gaps (CSS genuinely cannot express what GL can here), not deferred work — separating
those from the two/three items that are real, closeable gaps is the point of this phase.

**Confirmed permanent, not pursued as tasks** (verified by reading `EasyGLGraphicsBackend.cpp`, not
assumed):
- **Real backbuffer readback.** `EasyGLGraphicsBackend::ReadBackbuffer` does a real `glReadPixels`
  (with an MSAA-FBO resolve first). No browser API rasterizes a live DOM subtree — already Known
  Limitation 1, unchanged.
- **Custom `Effect`/shader stage.** `EasyGLSpriteBatchBackend::FlushBatch` genuinely binds and runs
  a compiled custom GLSL program per Task 1077 (not a stub) when one is set. CSS has no
  programmable shader stage for a translated HLSL/GLSL effect to run in — already Known Limitation
  5, unchanged.
- **Arbitrary custom `BlendState`.** `EasyGLGraphicsBackend::ApplyBlendState` accepts any
  `(colorSrcBlend, colorDstBlend, colorBlendFunc, ...)` combination via real
  `glBlendFuncSeparate`/`glBlendEquationSeparate`. Checked whether CSS's other `mix-blend-mode`
  values (`multiply`, `screen`, `darken`, …) could extend `BlendStateToDomCompositeOp`'s existing
  4-tuple table beyond the four already mapped: they can't — those CSS modes are distinct nonlinear
  compositing formulas, not reachable from *any* `(srcFactor, dstFactor, equation)` triple the
  additive `BlendState` model can express, so there is no natural fifth preset to add. Already Known
  Limitation 4, confirmed exhaustively rather than left as an assumption.
- **Anisotropic/finer `TextureFilter` granularity.** `EasyGLGraphicsBackend` reports real
  `GL_EXT_texture_filter_anisotropic` support (Task 918, up to 16x). CSS `image-rendering` is a
  coarse, effectively binary hint (`auto`/`pixelated`/`crisp-edges` — no anisotropic-level control);
  not meaningfully closeable.
- **MSAA render targets.** `EasyGLGraphicsBackend::CreateRenderTarget2D` supports a real multisample
  renderbuffer + resolve. `HTML_DOM` render targets are backed by an ordinary `<canvas>` 2D context,
  which has no analogous MSAA render-target concept — architecturally different, not a deferred
  feature.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-97 | ✅ | **`TextureAddressMode` coverage when `sourceRectangle` exceeds the texture.** Two real, closeable sub-gaps found in `HtmlDomState.cpp`'s `ValidateAddressModes`: (1) symmetric `Mirror` (`addressU == addressV == Mirror`) threw unconditionally. **Correction to this task's own original framing**: XNA's `SamplerState` class has NO built-in Mirror preset at all (confirmed by reading `SamplerState.hpp` — only `Point`/`Linear`/`AnisotropicClamp`/`Wrap` exist as statics); a game always constructs a custom `SamplerState` to use Mirror, and setting both axes the same way is the natural, common way to do that — not a claim about a nonexistent preset. `EASYGL` supports it for real via native `GL_MIRRORED_REPEAT`. (2) mixed per-axis modes (`addressU != addressV`) threw unconditionally even when NEITHER axis was `Mirror` (e.g. U=Wrap, V=Clamp) — but CSS `background-repeat`/`CanvasPattern` both natively accept independent per-axis repetition. **Implemented.** `ValidateAddressModes` now only throws for the one genuinely unsupported combination: Mirror mixed with a DIFFERENT mode on the other axis. `BuildDrawCommandEXT` (`HtmlDomSpriteBatchBackend.cpp`) computes `tiledU`/`tiledV` independently (each axis clamps or stays full on its own), plus a `mirror` bool (only ever true when both axes are Mirror, which `ValidateAddressModes` guarantees). Two new flag bits: `FlagWrapV` (independent V-axis tiling, alongside the existing `FlagWrap` for U) and `FlagMirror` (select the mirrored variant). Symmetric Mirror is realized via a new `Module['cnaDomGetMirrorVariant']` (`HtmlDomTextureBackend.cpp`) — a lazily-built, cached 2x2 canvas where the source variant is drawn once per quadrant with the other three quadrants flipped horizontally/vertically/both, cached in the SAME `entry.variants`/LRU-256 map as ordinary variants (no separate bookkeeping); tiling THAT image with ordinary `'repeat'` reproduces mirror-repeat exactly, by construction — the same general technique `CANVAS`'s own `CanvasSpriteBatchBackend` already proved out for its equivalent gap (`plan_canvas.md` CANVAS-44), adapted to this backend's existing variant-cache architecture rather than invented fresh. Mixed non-Mirror axes map to `background-repeat`'s two-value shorthand (DOM path) or `CanvasPattern`'s `'repeat-x'`/`'repeat-y'`/`'repeat'` repetition string (Canvas2D path) directly. **Verified in a real browser, both code paths.** Pixel-exact (Canvas2D render-target path, `htmldom_pixel_verification_test.cpp`, 13→15 checks): a 2x2 four-distinct-colour source tiled 2x2 under symmetric Mirror reads back the full hand-derived reflected grid, pixel-for-pixel (97a) — genuinely different from Wrap's plain-repeat pattern, not just "didn't throw"; the same source under mixed U=Wrap/V=Clamp tiles its U extent exactly like Wrap while its V extent clamps independently (narrows to the source height, does not stretch to fill the unclamped destination height) (97b). Structural (DOM path, `htmldom_smoke_test.cpp`, 32→35 checks): a symmetric-Mirror sprite gets a real PNG data-URL background and `background-repeat: repeat` (97c); a mixed U=Wrap/V=Clamp sprite gets `background-repeat: repeat-x` — caught by the real browser run, not assumed: the CSSOM serializes the two-value `'repeat no-repeat'` back as the single-keyword shorthand `'repeat-x'` (`repeat-x` is literally defined as shorthand for exactly that pair), the same kind of normalization-not-a-bug finding `JsRootClipPathInsetIs` hit earlier in this file (97d). GTest coverage extended for both the pure `ValidateAddressModes` function (symmetric Mirror and mixed-non-Mirror now accepted; Mirror-mixed-with-a-different-axis-mode still throws) and `BuildDrawCommandEXT`'s per-axis clamp/tile math (symmetric Mirror flags all three bits and keeps the full rect; mixed Wrap-U/Clamp-V tiles only U and narrows only V). Full regression pass: 35/35 smoke, 15/15 pixel, 3/3 stress, 6/6 dispose, 37/37 GTest — zero regressions. |
| HTMLDOM-98 | ✅ | **`SetViewport` (sub-rectangle `Viewport`).** `EasyGLGraphicsBackend::SetViewport` supports a real sub-region: `EasyGLSpriteBatchBackend::FlushBatch` builds its ortho projection from `Viewport.Width/Height` and leaves a non-full-target `Viewport` as the live GL rasterizer region (Task REMED-GFX-072), enabling split-screen/sub-panel rendering. `HTML_DOM` previously inherited `IGraphicsBackend`'s no-op default, matching every OTHER 2D-only sibling (`SDL_RENDERER`/`CANVAS`/`DX3`) — confirmed non-gap AGAINST those siblings (HTMLDOM-81), but a real, closeable gap against `EASYGL` specifically, since nothing about DOM/CSS compositing rules it out. **Implemented.** `HtmlDomGraphicsBackend::SetViewport` (idempotent, matching `Present()`'s own "only touch the DOM when geometry actually changed" rule) forwards to a new `CNA_HtmlDom_SetViewport`, which stores `Module['cnaDomViewport']` and calls a new shared `Module['cnaDomApplyViewport']` (`CNA_HtmlDom_EnsureRoot`). That function positions/sizes `#cna-dom-root` to the viewport sub-rect (`left`/`top` offset by `Viewport.X/Y`, pre-multiplied by the logical→physical scale since root's own layout position is unscaled; `width`/`height` from `Viewport.Width/Height`) instead of always the full logical backbuffer, falling back to exactly the prior full-surface behaviour when the viewport covers the whole backbuffer (the overwhelmingly common case, confirmed to cost nothing extra) — the same "collapses to the existing default case" shape HTMLDOM-94's regions already used. `CNA_HtmlDom_UpdateSurface` (a real resize) now only records the backbuffer's own geometry and re-delegates to `cnaDomApplyViewport`, so both a resize and an explicit viewport change funnel through the one function that knows how to derive root's box. Sprite-local coordinates needed no change (XNA already expresses them viewport-relative via the projection, not per-sprite). **Scissor/viewport coordinate-space finding, verified by reading `EasyGLGraphicsBackend` rather than assumed**: `GraphicsDevice.ScissorRectangle` and `Viewport` share the same ABSOLUTE render-target pixel space on a real GPU (`EasyGLGraphicsBackend`'s scissor forwards straight to `glScissor`, a window-space call, independent of whatever `glViewport` region is active) — so `cnaDomApplyRegionClip`'s insets now translate a region's stored (absolute) rect into root's own local box by subtracting the active viewport's offset first, and `cnaDomGetRegion`'s "does this rect cover everything" check compares against the offset viewport bounds, not literally `(0,0)`. A bound `RenderTarget2D` already resets `Viewport` to the target's own size at the shared `GraphicsDevice` layer and back to the backbuffer's on unbind (confirmed while implementing this task, not just assumed) — harmless even though `SetViewport` fires during that time, since `#cna-dom-root` is never drawn into while a target is bound regardless of its own CSS sizing. **A real test-premise correction found along the way**: the existing HTMLDOM-93 resize test (which deliberately calls `HtmlDomGraphicsBackend::Present()` directly, bypassing `GraphicsDevice`, specifically to verify the backend's OWN contract in isolation) started failing once Viewport became a real, tracked concept — because real XNA/FNA `Viewport` does NOT auto-track a resized backbuffer on its own (`GraphicsDevice::UpdateViewportFromWindow()` is the thing that resets both `Viewport` and `ScissorRectangle` together on a detected resize); bypassing `GraphicsDevice` means that reset has to be reproduced by hand too now, not just the resize itself. Fixed by adding an explicit `backend.SetViewport(0, 0, 128, 128, 0.0f, 1.0f)` call to the test, matching what a complete real resize flow does. **Verified structurally in a real browser** (the DOM backbuffer itself cannot be read back at all — design decision 11 — so there is no pixel-exact route available here the way the render-target-bound path uses): `htmldom_smoke_test.cpp` (35→38 checks) confirms a `Viewport(4,4,16,16)` sub-rect resizes `#cna-dom-root` to 16×16 (not the full 128×128 backbuffer), repositions it by exactly the expected `+4` offset (verified against the surface's own pre-viewport `left`/`top`, at a logical→physical scale confirmed to be exactly 1 in this scenario, not skipped), and that a sprite drawn at local `(0,0)` under that viewport still reads back `translate(0px,0px)` -- proving positioning comes entirely from root's own repositioning, not a per-sprite offset. New GTest (`SetViewportAcceptsArbitraryValuesAndIsIdempotent`) covers the C++-side contract under `node` (no `__EMSCRIPTEN__`, so no JS-side state to observe there): arbitrary values, a repeated call with the same values, and a subsequent call with different values all complete without throwing. Full regression pass: 38/38 smoke, 15/15 pixel, 3/3 stress, 6/6 dispose, 38/38 GTest — zero regressions. |
| HTMLDOM-99 | ✅ investigated, **not adopted** | **Investigate (implement only if it verifiably wins): SVG `feColorMatrix`-based exact real-time tint**, as a possible alternative to the current per-distinct-colour PNG-variant cache (LRU-capped at 256, confirmed under real eviction pressure by HTMLDOM-90). `EASYGL` tints exactly, every frame, for free in its fragment shader, with no cache/eviction concept at all — a real per-pixel RGB×tint multiply is expressible in SVG via a diagonal `<feColorMatrix>` applied as a CSS `filter: url(#...)`. **Acceptance bar, stated up front so this couldn't be quietly declared a win**: only keep it if a real headless-Chromium measurement showed it was not slower than today's cache for the steady-state case AND faster for the heavy-tint-churn case HTMLDOM-90 exercises. **Measured, not assumed.** Built a standalone (no Emscripten/CMake needed — pure HTML/JS, since this is a DOM/CSS technique question, not a backend-integration one) 500-sprite experiment mirroring HTMLDOM-89's own methodology (warm-up frame, 60 `requestAnimationFrame`-paced timed frames), comparing today's real technique (per-pixel `getImageData`/`putImageData` tint + `toDataURL`, cached, LRU-256) against a per-sprite `<feColorMatrix>` filter (one dedicated `<filter>` per pooled sprite element, its `values` attribute rewritten via `setAttribute` when that sprite's tint changes, `background-image` never changing at all) in headless Chromium, run 3 times for consistency: **steady-state** (a fixed set of 8 stable, already-cached tints across 500 sprites, position animating every frame, no tint changes at all) — variant-cache **18.6–21.1 ms/frame**, SVG filter **38.2–40.9 ms/frame**, roughly **2× SLOWER**; **heavy churn** (every sprite's tint changes every frame, cycling through hundreds of distinct RGB values, the exact scenario HTMLDOM-90 exercises) — variant-cache **73.0–75.9 ms/frame**, SVG filter **43.4–52.0 ms/frame**, ~35-40% faster. **Verdict: fails the acceptance bar, not adopted.** The churn-case win is real, but the steady-state regression is the disqualifying result: it directly contradicts this backend's core "zero cost when nothing changes" design premise (design decisions section, above) for the OVERWHELMINGLY more common case (most games use a handful of stable tints, not per-frame-per-sprite colour churn) — confirming the concern this task was scoped around from the start, not a surprise. Root cause consistent with the documented risk: `filter:` forces the browser into extra compositor work for that element even when the filter's own parameters are not changing frame to frame, unlike a plain `background-image`/`transform` write. **Kept**: the existing PNG-variant cache, unchanged. This is a genuine, honest "investigated, not adopted" outcome — the acceptance bar existed specifically so this couldn't be quietly declared a win on the strength of the churn number alone. |
| HTMLDOM-100 | ✅ | **Fix: `Opaque` forced result alpha to 255 on the Canvas2D render-target path, diverging from real XNA.** Found while writing `plan_canvas.md` CANVAS-85's own Opaque check for the sibling `CANVAS` backend: this project's own `BlendState.cpp` defines `BlendState::Opaque` with symmetric `One`/`Zero` factors for BOTH colour AND alpha (`colorSrcBlend=One, colorDstBlend=Zero, alphaSrcBlend=One, alphaDstBlend=Zero`), not colour alone — real XNA Opaque replaces the destination pixel with the source pixel EXACTLY, alpha included, never forcing it to 255. This backend's `HtmlDomState.hpp`/`HtmlDomTextureBackend.cpp` design predates that finding and forced alpha to 255 unconditionally on both draw paths, and the existing HTMLDOM-86 test asserted exactly that forced value as correct. **Implemented.** The two draw paths now genuinely differ, both intentionally: the DOM `<div>` backbuffer path still fetches the alpha-stripped (mode 2) variant, since CSS has no operator that can replace what is beneath a stacked, blended element while still showing that element's own partial alpha through it — full opacity is the closest a `<div>` can get, and this is now an explicitly documented, accepted deviation rather than an unexamined default. The Canvas2D render-target-bound path (`CNA_HtmlDom_FlushSprites`'s `targetCtx` branch, `HtmlDomSpriteBatchBackend.cpp`) now fetches the STRAIGHT (mode 0, as-uploaded) variant for Opaque and composites it with Porter-Duff `'copy'` instead of alpha-forced `'source-over'` — clipped first to exactly the sprite's own already-transformed footprint (`ctx.rect(lx,ly,sw,sh)` + `ctx.clip()`, inside the existing `save()`/`restore()` pair), because `'copy'` is evaluated over the WHOLE compositing area rather than just the drawn shape (the identical pitfall `plan_canvas.md` CANVAS-44 found and fixed for the sibling `CANVAS` backend, applied here the same way). `VariantModeFor`/`HtmlDomState.hpp`'s design-decision doc, the `DomCompositeOp::Opaque` enumerator doc, and `HtmlDomTextureBackend.cpp`'s mode-table comment were all corrected to describe the two paths' now-different, both-deliberate behaviour instead of asserting one wrong shared rule. HTMLDOM-86 (`htmldom_pixel_verification_test.cpp`, binds a `RenderTarget2D`, so it exercises the Canvas2D path exclusively) rewritten: a semi-transparent source texel `(180,90,40,120)` now reads back at alpha≈120 (its own value), not forced to 255. **Verified in a real browser**: 15/15 pixel (HTMLDOM-86 now asserting the corrected value, read back `(181,89,40,120)` against `~(180,90,40,120)`, within the existing ±1 8-bit-rounding tolerance), 38/38 smoke (including the existing Wrap-under-Opaque render-target check at frame 6 and the Mirror-under-Opaque check at frame 14, both fully-opaque source textures where the mode-0-vs-mode-2 change is a no-op, confirming no incidental regression), 3/3 stress, 6/6 dispose — zero regressions across the full HTML_DOM browser suite. |

### D16 — Independent correctness and verification audit (2026-08-03)

An independent source-and-browser audit found that several earlier ✅ rows satisfy their structural
tests but not the XNA/rendered-output contract those rows claim. Under this plan's own status legend,
the affected earlier rows must be treated as reopened until the tasks below pass their acceptance
criteria. Confirmed defects are distinguished from explicitly labelled audit risks; neither category
may be closed by asserting an inline CSS value or by repeating the current implementation's formula
as the test oracle.

While this audit was being prepared, HTMLDOM-100 independently fixed the Canvas2D render-target
half of the Opaque-alpha defect. The audit tasks therefore start at HTMLDOM-101; HTMLDOM-105 retains
only the still-open DOM-backbuffer Opaque deviation and the other blend-equation gaps.

| Task | Status | Description |
|---|---|---|
| HTMLDOM-101 | ⬜ | **Give every non-default scissor region a real surface-sized box and verify actual painting (reopens HTMLDOM-80/93/94). Confirmed defect.** `cnaDomGetRegion` currently creates a container with only `position:absolute;left:0;top:0`; its absolutely-positioned sprite children do not contribute intrinsic size, so the region's border box is 0×0. A focused headless-Chromium probe using this exact layout measured `offsetWidth=0`, `offsetHeight=0`, and confirmed the clipped child was not hit/painted. The current smoke test only reads `style.clipPath` and counts children whose inline `display` is not `none`, so it reports PASS for an invisible sprite. Fix the region to cover the active viewport (`inset:0`, or explicit width/height kept in sync), then add a browser screenshot/pixel test proving pixels inside the scissor survive and pixels outside it do not. The test must also assert the region's non-zero computed dimensions. |
| HTMLDOM-102 | ⬜ | **Implement scissor semantics on the Canvas2D render-target path and honour `RasterizerState.ScissorTestEnable` (reopens HTMLDOM-80/91/94 and narrows HTMLDOM-51). Confirmed defect/design divergence.** `CNA_HtmlDom_FlushSprites` explicitly states that a bound render target does not consult the scissor rect at all. The DOM path also clips whenever a narrow `ScissorRectangle` is recorded, even when the applied rasterizer state has scissor testing disabled; matching `SDL_RENDERER`'s omission is not XNA fidelity. Track the enable bit, capture the effective state at the same granularity as the draw batch, and use `ctx.save()`/`rect()`/`clip()` on render targets. Pixel-verify enabled vs disabled scissor on both paths, including a changed rectangle between two batches. |
| HTMLDOM-103 | ⬜ | **Restore true painter's order across scissor/viewport regions and make region eviction non-destructive (reopens HTMLDOM-31/76/94). Confirmed defect.** Sprites in different region containers paint by persistent DOM-container creation/allocation history, not current draw order; the default `full` pool is the root itself, so direct sprite nodes and nested region containers are interleaved by allocation history rather than even the documented region-creation order. Creating a 17th non-default region can LRU-remove the oldest container while its sprites are still part of the current frame, making submitted draws disappear. Use same-level batch/region wrappers with an explicit per-flush order (or equivalent z-order that preserves blend semantics), and never evict a region active in the current frame. Add overlapping A/B/A region tests, a full-region/non-default interleave test, and 17+ distinct scissor rectangles in one frame. |
| HTMLDOM-104 | ⬜ | **Implement real sampler `Clamp` for out-of-bounds source coordinates and correct the existing test oracle (reopens HTMLDOM-35/45/97). Confirmed semantic defect.** The current encoder narrows the source rectangle into the texture and leaves the removed destination area transparent. XNA clamp sampling instead repeats the nearest edge texel outside the texture; it does not crop sprite geometry. The current HTMLDOM-97b test explicitly expects transparent rows and therefore validates the wrong result. Implement edge extension (for example a cached padded source, or centre/edge/corner draws) independently per axis on DOM and Canvas2D paths. Pixel-verify negative left/top, positive right/bottom overflow, a rectangle entirely outside the texture, mixed Wrap+Clamp, scaling, rotation, tint, and both point and linear filtering. Until exact support exists, reject these draws rather than silently returning cropped output. |
| HTMLDOM-105 | ⬜ | **Re-derive the remaining standard blend semantics from their colour AND alpha equations; stop calling the current mapping pixel-exact (reopens HTMLDOM-41/42/43/84/85/86/87 and follows HTMLDOM-100). Confirmed semantic defect.** HTMLDOM-100 correctly changed Opaque on the Canvas2D render-target path to clipped `copy`, but the DOM backbuffer still strips source alpha and explicitly accepts a result that differs from XNA's `(One,Zero)` RGBA copy. `source-over` does not reproduce `NonPremultiplied`'s XNA alpha equation, and `lighter`/`plus-lighter` does not reproduce `Additive`'s alpha equation for translucent sources. A headless-Chromium Canvas2D probe of two 50%-alpha `lighter` draws returned `[128,0,128,255]`, whereas the XNA factors produce approximately `[128,0,128,128]`. Define expected raw RGBA algebra independently of browser compositing, retain the now-correct HTMLDOM-100 Canvas2D path, and either implement exact semantics on each remaining path or reject/document the unsupported combination as an architectural limitation. Tests must cover semi-transparent source AND destination, tint alpha, zero alpha, and raw RGBA readback for every preset; opaque-only Additive tests are insufficient. |
| HTMLDOM-106 | ⬜ | **Audit premultiplied-alpha representation across render-target write, readback, and render-target-as-texture sampling (reopens HTMLDOM-52/53/83/85). High-risk confirmed coverage gap.** Canvas2D `getImageData()` exposes straight/un-premultiplied bytes after compositing, while the backend's AlphaBlend variant generator assumes bytes that need un-premultiplication. A semi-transparent render target sampled later under AlphaBlend can therefore be divided by alpha a second time; the existing render-target round-trip uses opaque content and cannot detect it. Establish the canonical byte representation at every boundary, avoid double conversion, and pixel-verify a semi-transparent premultiplied sprite rendered to RT A, read back, sampled from A to RT B under each preset, and read back again over transparent and non-transparent destinations. |
| HTMLDOM-107 | ⬜ | **Make viewport state per draw batch instead of retroactively resizing the one global root; support it on render targets (reopens HTMLDOM-98). Confirmed defect.** `cnaDomApplyViewport` moves/resizes `#cna-dom-root`, which contains every previously submitted sprite. Drawing viewport A and then setting viewport B in the same frame therefore moves and clips A's sprites retroactively, so the claimed split-screen/sub-panel use case is not implemented. The Canvas2D target path ignores viewport clipping/offset entirely, and shrinking the root also exposes page background outside it even though `Clear` is render-target-wide and not viewport-scoped. Keep a full-backbuffer root and capture viewport placement/clip per batch (or an equivalent ordered wrapper), implement Canvas2D viewport semantics, and add two-overlapping-viewports-in-one-frame, viewport+scissor, Clear-with-subviewport, resize, and render-target viewport pixel tests. |
| HTMLDOM-108 | ⬜ | **Implement every `CnaPresentationMode`, validate mode values, and make input transforms use the identical geometry (reopens HTMLDOM-13/14). Confirmed defect.** The current backend only has correct `FixedHeightDynamicWidth`-style uniform height scaling. `Letterbox`, `Overscan`, `Stretch`, and `NativeBackBuffer` all fall through to a height-derived uniform `scale()` with no centring/cropping offsets; `TransformWindowToLogical`/`TransformLogicalToWindow` repeat the same assumption, and `SetPresentationMode` accepts invalid ordinals. Share a tested logical-viewport computation with a complete backend (or move it to Common), implement per-axis Stretch and centred Letterbox/Overscan, define NativeBackBuffer exactly, return the correct outside-letterbox result for input mapping, and browser-test every mode under matching and mismatched aspect ratios. |
| HTMLDOM-109 | ⬜ | **Replace the variant-cache FIFO with a real, lifecycle-safe LRU (reopens HTMLDOM-23/90). Confirmed defect.** `cnaDomGetVariant` returns a cache hit without touching its position, so eviction is FIFO, not LRU. `UpdatePixels`, render-target bind invalidation, texture destruction, and backend/root destruction clear variant maps or registries without removing the corresponding global LRU records; stale `(id,key)` entries can later target a regenerated variant with the same key and cause needless deletion/recreation. Implement O(1) hit promotion and removal (or an insertion-ordered per-entry map with a global budget), clean all lifecycle paths, and test hot-entry survival, `SetData` regeneration of the same key, render-target rebinding, texture destruction, backend reset, and sustained churn. Assert exact capacity and real eviction identity, not merely `length <= 256`. |
| HTMLDOM-110 | ⬜ | **Correct the retained/static-frame performance claim and investigate a real unchanged-batch fast path (reopens the design premise and HTMLDOM-32/89). Confirmed documentation/measurement defect.** A normal XNA game must resubmit its static sprites every frame; `End()` still crosses into JS and walks every command, and `Present()` walks every region/pool. Per-property diffing can yield zero CSS writes/repaint, but it is not "zero cost — no JS runs". First instrument JS calls, command visits, style writes, layout, paint, and composite work for identical resubmitted frames. Then either implement a safe command/batch hash or generation scheme that skips identical replay without breaking Clear, order, scissor, viewport, texture invalidation, or disappearing sprites, or revise all claims to the measured narrower guarantee. |
| HTMLDOM-111 | ⬜ | **Replace submission-only benchmarks with reproducible CPU-submission and end-to-end frame/render measurements (reopens HTMLDOM-89/92/99 conclusions). Confirmed methodology defect.** Both benchmarks stop their timer immediately after `SpriteBatch::End()`, before `Present()` and before Chromium's deferred style/layout/paint/compositor work. This structurally favours DOM style submission over Canvas2D/WebGL work and cannot support `fps-equivalent` or an end-to-end claim that HTML_DOM is faster than another backend. Report submission CPU separately; measure real frame cadence and browser rendering through `requestAnimationFrame` fences and/or Chrome tracing/PerformanceObserver, include warm/cold stable-tint and heavy-churn workloads, record browser/build/hardware flags, and publish raw machine-readable samples. Do not compare hardware WebGL with software SwiftShader without labelling them as different environments. Check the HTMLDOM-99 experiment and raw results into the repository so its conclusion is reproducible. |
| HTMLDOM-112 | ⬜ | **Make all browser pages runnable by one official command and execute them in CI (reopens HTMLDOM-70/71/72 and D13/D14 verification claims). Confirmed tooling gap.** `run-htmldom-browser-test.sh` hard-codes `cna_test_htmldom_smoke.html`; comments in the pixel/stress/dispose sources say to pass that page to the harness, but the shell interface accepts only a build directory. Add a page/target argument or a suite runner, build and run smoke + pixel + stress + dispose (and relevant benchmark correctness gates), allocate collision-free ports, propagate browser/page errors, and register a CI job/custom target that archives logs/screenshots/traces. The documented three-command recipe must reproduce every claimed PASS count, not only smoke. |
| HTMLDOM-113 | ⬜ | **Strengthen browser tests so they prove rendered semantics rather than internal implementation state (reopens all affected ✅ verification rows). Confirmed coverage defect.** Preserve HTMLDOM-100's corrected Opaque render-target oracle and correct the Clamp expected values. Replace scissor's `style.clipPath`/`display`-only assertions with actual pixels/screenshots; add computed region dimensions. Change stress assertions from pool `<= 2*peak` and LRU `<=256` to exact identities required by the plan. Add translucent blend alpha, semi-transparent RT resampling, viewport coexistence, render-target scissor/viewport, region ordering/eviction, all presentation modes, unsupported-browser Additive capability handling, `SpriteSortMode::Immediate`, and DOM-vs-Canvas path parity cases. Keep expected values hand-derived from XNA blend/sampler equations, never from the code under test. |
| HTMLDOM-114 | ⬜ | **Remove or explicitly constrain process-global/multi-window state and make reset/destruction complete. Audit risk.** `Module['cnaDomRoot']`, texture helpers/registries/LRU, current composite operation, and bound render-target id are global even though `IGraphicsBackend` has a per-window registry. A second live window/backend, backend reset with surviving resources, or destruction in an unusual order can cross-contaminate state. Decide whether CNA guarantees exactly one live graphics device; if yes, enforce it and document it. Otherwise namespace JS and C++ state per backend/window instance. Test two windows where supported, reset with live textures, destroying a currently-bound target, helper/LRU cleanup, and restoration without stale `GraphicsDevice`/backend binding disagreement. |
| HTMLDOM-115 | ⬜ | **Harden DOM-surface integration with the host page. Audit risks.** Preserve and restore the canvas's pre-existing `visibility` value instead of overwriting it with `hidden` then `""`; detect an existing conflicting `#cna-dom-root`; account for canvas offset-parent, scrolling, CSS transforms, and later layout movement rather than assuming `offsetLeft/offsetTop` stay correct; define z-order relative to page siblings; and use a resize/layout observer or a safe geometry refresh outside the per-sprite hot path. Browser-test nested positioned containers, page scroll, transformed/scaled canvas CSS, a pre-hidden canvas, backend recreation, and ID collision. |
| HTMLDOM-116 | ⬜ | **Measure and bound compositor-layer/DOM memory policy instead of forcing `will-change:transform` on every sprite. Audit risk/performance improvement.** `will-change` is only a hint and thousands of permanent sprite layers can consume more memory and perform worse than browser-managed promotion. Measure layer count, GPU/process memory, paint/composite time, and pool high-water retention over realistic sprite-count distributions. Adopt a configurable/thresholded promotion policy, age out unused high-water elements or document the retained-memory bound, and add a benchmark that includes 5k/10k sprites and long idle periods without treating a non-crash as sufficient evidence. |
| HTMLDOM-117 | ⬜ | **Align documentation and capability reporting with actual timing and architectural limits. Confirmed inconsistencies plus audit risks.** Data URLs are generated lazily on first DOM use, not "once at upload time", so distinguish `SetData` invalidation from the later synchronous encode cost. Do not advertise accessibility benefits without any roles, labels, focus, or semantic API; either add an opt-in metadata/accessibility hook or narrow the claim to DOM inspection/debugging. Reconcile the Scope statement that every 3D creation call throws through `NotYetImplemented` with D7's deliberate inherited `nullptr` defaults for Texture3D/TextureCube/RenderTargetCube/OcclusionQuery/Effect. Expose/query Additive browser support instead of silently degrading on older engines. Update the capability matrix only after the reopened tasks pass. |
| HTMLDOM-118 | ⬜ | **Define and enforce `SpriteSortMode::Immediate` semantics (reopens HTMLDOM-30/76 and the per-batch scissor rationale). Confirmed contract gap.** Shared `SpriteBatch` forwards each Immediate draw directly to the backend, but `HtmlDomSpriteBatchBackend::Draw` still queues it and performs the only JS flush at `End()`. Device/scissor/viewport/texture state changes between Immediate draws therefore cannot take effect immediately, while the documentation alternately claims general sort-mode sequencing and calls Deferred this backend's only mode. Either implement per-draw flush/state capture for Immediate (accepting its wasm→JS cost), or reject Immediate explicitly and document the narrower capability. Add ordering and state-change tests that distinguish real Immediate from Deferred. |
| HTMLDOM-119 | ⬜ | **Close source-rectangle/filtering edge cases beyond the confirmed Clamp defect. Audit risk.** Verify and fix fully-outside rectangles (which can encode zero source width/height and reach Canvas2D `drawImage`), negative origins, independent-axis Wrap/Mirror phase for negative coordinates, mirror periods after tint/blend variant generation, fractional scaling with linear filtering, and sprite-atlas edge bleed from CSS background filtering. Each case needs pixel parity between the DOM screenshot path and Canvas2D render-target path. Throw before JS for any combination that cannot be reproduced exactly. |
| HTMLDOM-120 | ⬜ | **Audit direct-backend validation and replace silent capability drops with explicit policy. Audit risk.** Validate sampler address/filter ordinals, presentation/viewport dimensions and depth ranges, null render-target descriptor arrays with non-zero count, texture/render-target sizes, and readback pointers/regions consistently before JS. Reconsider silently ignoring requested render-target `depthFormat`, mipmaps, multisampling, and preservation flags: either reject unsupported non-default requests with actionable errors or prove the shared layer has already normalized them. Add direct-backend and public-API tests for exception type/message and no partial state mutation. |

---

## What the browser run caught

The first headless-Chromium run reported 3 of 17 checks failing against a backend that was in fact
producing exactly the right DOM. The cause was in the test's own JS: **a backslash inside an `EM_JS`
body does not survive the preprocessor's stringification of that body**, so `/(\d+)/g` silently
became `/(d+)/g` and `/\s+/g` became `/s+/g` — regular expressions that then quietly matched the
wrong thing instead of failing loudly. (The giveaway: the one transform check whose needle contained
no letter `s` passed, while `translate` and `scale` both failed.) Every regular expression in the
test was replaced with `split`/`indexOf` parsing.

Worth knowing project-wide: no `EM_JS` body anywhere in CNA should contain a backslash escape.
`CANVAS`'s own `EM_JS` bodies happen not to, so nothing else is affected today.

Extending the smoke test to cover `SpriteFont::DrawString` (HTMLDOM-38) and `TextureAddressMode::Wrap`
(HTMLDOM-45) caught a second, unrelated test bug the same way: 19 of 22 checks failed with symptoms
(`visible=3` instead of `2`, a glyph landing at index 0 with the WRONG width) that looked exactly
like a backend defect. The cause was in the test's own frame dispatch: the pre-existing `if (frame_
<= 2) {...} else {...}` block that drives frames 3-4's single-sprite recycling check used an
unconditional `else`, so it kept running on frame 5 too — queuing an extra, unplanned sprite *before*
the new glyph/Wrap draws, shifting every sprite index by one. Fixed by bounding that block to `else
if (frame_ <= 4)` so frame 5 owns its own drawing (and its own sprite indices) outright. Two real
bugs found by two different real-browser runs, zero backend defects either time — exactly the
point of running this in an actual browser instead of trusting a structural review.

## Known limitations (honest list)

1. **No backbuffer readback.** No browser API rasterizes a live DOM subtree. Bound render targets
   read back for real; the backbuffer throws.
2. **Texture upload costs a PNG encode.** Fine at load time, bad per frame (design decision 6).
3. **`TextureAddressMode::Mirror` mixed with a DIFFERENT mode on the other axis throws** when the
   source rect exceeds the texture (HTMLDOM-97) — symmetric Mirror and non-Mirror mixed axes are
   both supported now; only Mirror-mixed-with-something-else remains unsupported.
4. **Custom `BlendState`s throw** — only the four standard presets exist in CSS compositing.
5. **No custom `Effect`s** — there is no shader stage.
6. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER`/`CANVAS`.
7. **Sprite count drives DOM size.** Tens of thousands of simultaneously visible sprites will cost
   more in compositor memory than a canvas would; this backend targets normal 2D games, not
   particle storms.
8. **Scissor rect correctness is per-`SpriteBatch` batch, not per-`Draw()` call** (HTMLDOM-94) —
   matching real XNA/FNA `SpriteBatch.Deferred` semantics (`ScissorRectangle` is read once when a
   batch's draw calls are actually issued at `End()`), not a backend-specific shortfall. Sprites in
   DIFFERENT scissor-rect regions paint in the order those regions were first created, not
   necessarily interleaved per-frame draw order across regions — an accepted trade-off for actually
   keeping different scissor rects from clipping each other's sprites, which the backend could not
   do at all before HTMLDOM-94.
