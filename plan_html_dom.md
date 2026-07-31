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
| HTMLDOM-24 | 🟨 | `Texture2D::GetData` on a plain texture is served by the shared CPU shadow — no backend work needed. Not separately exercised by the browser run. |

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
| HTMLDOM-45 | ✅ | `Clamp` is exact and unit-tested; `Mirror` and mixed per-axis modes throw (unit-tested). `Wrap` maps to CSS background repetition on the DOM path — verified in the browser run: a texture drawn with a source rectangle double its own size under `SamplerState.PointWrap` keeps its full (unclamped) element width and gets `background-repeat: repeat`. The render-target path's separate Canvas2D-repeating-pattern implementation is verified too, pixel-exact: a 2x2 source tiled into a 4x4 render target under Wrap reads back with every texel matching `source(x%2, y%2)` exactly. |
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
| HTMLDOM-81 | ✅ | **Confirmed non-gap, documented rather than implemented.** No 2D-only sibling backend implements `SetViewport` either (`SDL_RENDERER`, `CANVAS`, `DX3` all leave it as the inherited no-op; only 3D-capable backends — `EASYGL`, `D3D11`/`D3D12`/`D3D9`, `Bgfx`, `SdlGpu`, `Headless`, `Software` — override it, since `Viewport` is fundamentally the NDC-to-screen transform a rasterizer pipeline needs and this backend has none). Downgraded from "gap" to "confirmed non-gap, matching every comparable sibling" — the reasoning is on record here rather than silently assumed, which is what "done" means for this task. |
| HTMLDOM-82 | ✅ | Verified pixel-exact in `examples/htmldom_pixel_verification_test.cpp` with two deliberately unambiguous (no rotation sign-convention risk) matrices: a pure translation `Matrix::CreateTranslation(6,8,0)` moves a 1x1 sprite from the origin to exactly (6,8); a pure `Matrix::CreateScale(2)` renders it as a 2x2 block instead of 1x1. Both drawn into a bound render target, exercising `transformMatrix` on the Canvas2D `targetCtx` branch specifically — a separate code path from the DOM branch the smoke test's own CSS-string check covers. |
| HTMLDOM-83 | ✅ | Verified pixel-exact in `examples/htmldom_pixel_verification_test.cpp`: cleared RT A to a distinct colour, unbound it, `Draw()` RT A itself (an ordinary `RenderTarget2D : Texture2D`) onto RT B, unbound, read B back — the sampled region matches RT A's real content exactly (within 1-unit rounding) and the surrounding area stays untouched. Confirms the data-URL regenerated-from-a-dirty-flag path (design decision 10) round-trips real content, not just that the call sequence doesn't throw. |
| HTMLDOM-84 | ✅ | Colour tint verified pixel-exact in the browser: a (200,150,100,255) texel tinted (255,128,64,255) under `AlphaBlend` over a transparent target reads back (200,75,25) — exactly `round(src*tint/255)` per channel, cross-checked against the same formula computed independently in C++, not restated from the JS. See `examples/htmldom_pixel_verification_test.cpp`. |
| HTMLDOM-85 | ✅ | **The highest-risk item on this list, now closed.** A hand-premultiplied texel (straight colour (255,100,50) at alpha 128, uploaded as (128,50,25,128), mirroring `SDL_RENDERER`'s own Task 697 test) drawn under `AlphaBlend` reads back (255,100,52,128) — within the expected double-rounding tolerance (this backend's own division, plus the browser's own internal premultiply/un-premultiply round trip on readback) of the exact expected (255,100,50,128). The un-premultiply maths reconstructs the original straight colour correctly; it is not double-dividing, not skipping the divide, and not off by a swapped operand. |
| HTMLDOM-86 | ✅ | Verified with genuinely semi-transparent source data (180,90,40,120) — every prior check used alpha=255 source, making the strip a no-op every time it ran until now. Reads back (181,89,40,255): alpha forced to 255 exactly, RGB within 1-unit rounding of the source, unaffected by the transparent destination it was drawn over — confirming the strip does not blend before forcing alpha. |
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
3. **`TextureAddressMode::Mirror` throws** when the source rect exceeds the texture.
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
