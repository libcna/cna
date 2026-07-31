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

**5. One JS call per batch, not per sprite.** `Draw()` appends a fixed-stride 64-byte POD command
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
| HTMLDOM-24 | ✅ | `Texture2D::GetData` on a plain texture is served by the shared CPU shadow — no backend work needed. |

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
| HTMLDOM-38 | ✅ | `SpriteFont::DrawString` verified to need no backend-specific code (design decision 12). |

### D5 — Blend and sampler state

| Task | Status | Description |
|---|---|---|
| HTMLDOM-41 | ✅ | `BlendStateToDomCompositeOp()` — pure function, unit-tested without a DOM; throws for non-preset states (design decision 8). |
| HTMLDOM-42 | ✅ | `Opaque` → alpha-stripped variant; `AlphaBlend` → un-premultiplied variant; `NonPremultiplied` → straight; `Additive` → `mix-blend-mode: plus-lighter`. |
| HTMLDOM-43 | ✅ | Colour tint → cached tinted variant; alpha → `opacity` (free). |
| HTMLDOM-44 | ✅ | `TextureFilter` → `image-rendering: pixelated` vs `auto`, using the same magnification-dominant grouping `SDL_RENDERER` Task 701 and `CANVAS` CANVAS-42 use. |
| HTMLDOM-45 | ✅ | `TextureAddressMode`: `Clamp` exact; `Wrap` via CSS background repetition when the source rect exceeds the texture; `Mirror` throws (CSS has no mirror-repeat for backgrounds and a pre-tiled variant would defeat the DOM path's purpose). |
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
| HTMLDOM-72 | ✅ | **Real browser pixel verification** — the smoke test runs in headless Chromium via Playwright and the resulting DOM is asserted against expected geometry/styles. This is what `CANVAS` could never do in its own dev loop. |
| HTMLDOM-73 | ✅ | `docs/html-dom-backend.md` capability/limitation matrix. |

---

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
