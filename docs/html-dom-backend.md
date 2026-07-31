# HTML DOM Backend — Capability Status

`HTML_DOM` is CNA's DOM/CSS graphics backend: Emscripten-only, 2D-only, rendering `SpriteBatch`
output as pooled `<div>` elements styled with CSS transforms rather than rasterizing into a
`<canvas>`. See `plan_html_dom.md` for the design decisions and task breakdown this document
summarizes.

**Status legend** (this project's convention): ✅ implemented *and verified against its stated
acceptance criteria*; 🟨 code exists but has not met those criteria; ⬜ not implemented.

**What ✅ means here.** Unlike the `CANVAS` backend's own document, ✅ on this page is backed by a
real Emscripten build (emsdk 6.0.5) and, for everything the smoke test covers, by a real run in
headless Chromium — `scripts/run-htmldom-browser-test.sh` loads the generated page, drives six
frames (24 checks total), and asserts against the DOM/pixels the backend actually produced. Rows
marked 🟨 are implemented and code-reviewed but not covered by that automated run.

Select it with:

```bash
emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_BACKEND=HTML_DOM
cmake --build cmake-build-htmldom --target cna_test_htmldom_smoke -j4
scripts/run-htmldom-browser-test.sh cmake-build-htmldom
```

---

## How it draws

One `<div id="cna-dom-root">` is created over the `<canvas>` SDL3 already owns (the canvas stays in
the layout, hidden, because SDL keeps sizing it and delivering input through it). Every sprite is a
pooled child `<div>`:

| XNA concept | CSS realization |
|---|---|
| destination position, rotation, scale, flip | one `transform` list: `translate() rotate() scale() [mirror] translate()` |
| `Texture2D` + `sourceRectangle` | `background-image: url(data:image/png…)` + `background-position` + element size |
| tint alpha | `opacity` |
| tint RGB | a cached, exactly-computed pre-tinted copy of the texture |
| `BlendState.Additive` | `mix-blend-mode: plus-lighter` |
| `SpriteBatch` draw order | DOM document order (no `z-index` bookkeeping) |
| `Clear` | the surface's `background-color`, plus a frame reset |

Two properties carry the steady state — `transform` and `opacity` — and both are compositor-only, so
a frame in which sprites merely move triggers no layout and no repaint. Sprite *n* of a frame always
lands on pool element *n*, and each element caches its last applied style values, so only genuinely
changed properties are written. A whole batch crosses the wasm/JS boundary in **one** call: `Draw()`
appends a fixed-stride 80-byte command and `End()` hands the array over as a block.

---

## 1. SpriteBatch

| Feature | Status | Notes |
|---|---|---|
| `Draw(texture, x, y)` / `Draw(texture, destRect, srcRect, color)` | ✅ | Both funnel into one command encoder. |
| Rotation around `origin` | ✅ | `origin` (source-pixel space) lands exactly on the destination position for any rotation, matching FNA's `GenerateVertexInfo` placement. Unit-tested numerically. |
| Scalar / `Vector2` scale overloads | ✅ | Backend-agnostic — `SpriteBatch.cpp` reduces every overload to the same `(destRect, srcRect, …)` call. |
| `SpriteEffects` flips | ✅ | Mirror about the sprite's own centre, leaving the destination footprint unchanged — real XNA semantics. |
| Colour tint | ✅ | Exact: a cached pre-tinted copy of the texture (RGB multiplied per pixel, alpha untouched). No CSS `filter` approximation. |
| Tint alpha | ✅ | `opacity`, free at composite time. |
| `transformMatrix` in `Begin()` | ✅ | A leading CSS `matrix(M11,M12,M21,M22,M41,M42)` per sprite, so two batches with different transforms coexist in one frame. Omitted entirely for Identity. |
| Custom `Effect` via `Begin(effect)` | ✅ throws-by-design | CSS compositing has no programmable shader stage. |
| `SpriteSortMode` sequencing | ✅ | Backend-agnostic: sorting happens in shared `SpriteBatch.cpp`, and DOM document order realizes the result. |
| `SpriteFont` (`DrawString`, kerning, `\n`, flip, rotation) | ✅ | Needs zero backend-specific code — every glyph funnels through the same `Draw` overload. Verified in-browser: a `DrawString` glyph lands in the DOM as a real, correctly-sized, PNG-textured element. Kerning/`\n`/flip themselves are shared `SpriteFont`/`SpriteBatch` logic, not re-verified per backend. |
| Element pooling / recycling across frames | ✅ | Verified in-browser: two sprites then one leaves one visible and two retained. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData` | ✅ | Private off-screen canvas per texture; CSS-usable PNG data URLs derived from it on demand. |
| Texture upload cost | ⚠️ by design | Each upload costs a **PNG encode**. Fine once at load time; expensive every frame. Use `CANVAS` or `EASYGL` for per-frame `SetData`. |
| Mip-level (`level>0`) `SetData` | ✅ throws-by-design | No mip chain exists here — the same boundary `CANVAS` and `SDL_RENDERER` draw. |
| `RenderTarget2D` construction / bind / unbind | ✅ | Backed by a real off-screen canvas; while bound, draws replay into its Canvas2D context. |
| `RenderTarget2D` readback (`GetData`) | ✅ | Real synchronous `getImageData`, with the full REMED-GFX-127 argument-validation contract. Verified in-browser. |
| `RenderTargetUsage` | ✅ | A shared `GraphicsDevice` concern; no backend-specific code. |
| `Texture2D::FromStream` decode | 🟨 | Fully backend-agnostic before it reaches this backend's upload. |
| `SetRenderTargets` (MRT) | ✅ throws-by-design | One canvas backs each target; it is inherently single-output. |
| **Backbuffer readback** | ✅ throws-by-design | **The one thing `CANVAS` can do and this backend cannot.** No browser API rasterizes a live DOM subtree. The exception says so and points at `RenderTarget2D`. Verified in-browser. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ | Drawn from an alpha-stripped copy of the texture, which is exactly what `srcBlend=One`/`dstBlend=Zero` means. Notably this avoids Canvas2D `'copy'`'s whole-surface-clearing pitfall entirely — an opaque sprite covers its own footprint and nothing else. |
| `AlphaBlend` | ✅ | Drawn from an un-premultiplied copy: `srcBlend=One` assumes already-premultiplied source, while CSS composites straight alpha and premultiplies internally. |
| `NonPremultiplied` | ✅ | Pixels as uploaded — exactly what CSS assumes natively. |
| `Additive` | ✅ | `mix-blend-mode: plus-lighter`, CSS Compositing's exact `lighter` operator. |
| Any other custom `BlendState` | ✅ throws-by-design | CSS has no blend-factor or blend-equation model to approximate one with. |
| `ColorWriteChannels` / `MultiSampleMask` | ⬜ inexpressible | No per-channel write mask and no coverage mask exist in CSS compositing. Documented gap, not a silent drop. |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | `image-rendering: auto` vs `pixelated`, using the same magnification-dominant grouping `SDL_RENDERER` (Task 701) and `CANVAS` (CANVAS-42) use. |
| `TextureAddressMode::Clamp` | ✅ | Implemented for real: the source rectangle is narrowed into the texture and the sprite's local box shifted to match — not a reliance on implicit out-of-bounds behaviour. |
| `TextureAddressMode::Wrap` | ✅ | CSS `background-repeat: repeat` on the DOM path; a repeating Canvas2D pattern on the render-target path (two separate code paths). Only differs from Clamp when the source rectangle leaves the texture. Both verified in-browser: the DOM path keeps its full (unclamped) element width and gets `background-repeat: repeat`; the render-target path is checked pixel-exact — a 2x2 source tiled into a 4x4 target reads back with every texel matching `source(x%2, y%2)`. |
| `TextureAddressMode::Mirror` | ✅ throws-by-design | CSS has no mirror-repeat background repetition, and pre-tiling a mirrored copy per draw would defeat the DOM path's purpose. |
| Mixed per-axis modes, out of bounds | ✅ throws-by-design | Both axes derive from one mode here; a mixed request is rejected rather than silently reduced. |

## 5. Viewport / PresentationParameters

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | ✅ | The same `FixedHeightDynamicWidth` logical-size math every other backend uses. |
| Logical→physical scaling | ✅ | One CSS `scale()` on the surface element. Applied only when the geometry actually changed — these are the backend's only layout-affecting writes. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | Exact inverses; correct mouse mapping under scaling. |
| `Present()` | ✅ | Hides only the pool elements this frame did not use (high-water tracked). There is nothing to swap — the compositor presents. |

## 6. The 3D surface

Every 3D entry point throws `std::runtime_error("HTML_DOM backend: … not yet implemented")`:
depth/stencil clears, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, vertex and
index buffer creation, and both `Draw*Primitives` families. `CreateTexture3D`, `CreateTextureCube`,
`CreateRenderTargetCube`, `CreateOcclusionQuery` and `CreateEffectBackend` keep `IGraphicsBackend`'s
own null/throw defaults, the same deliberate choice `CANVAS` made.

`GraphicsDevice::SupportsCapability()` reports `false` for every capability and
`SupportsDepthStencil()` is `false`, so a caller can branch ahead of time instead of provoking an
exception.

---

## Performance characteristics

**Fast here:** a static frame (zero cost — no JS runs and nothing repaints); moving, rotating,
scaling or fading sprites (one `transform`/`opacity` write each, composited on the GPU); large
sprite counts that stay stable frame to frame; batches of any size (one wasm→JS crossing per batch).

**Slow here:** uploading texture pixels (PNG encode per upload); animating a sprite's tint RGB every
frame (a new cached variant per distinct colour, LRU-capped at 256); render-target-heavy frames
(those fall back to Canvas2D rasterization); very high sprite counts, where per-element compositor
layers cost more memory than a single canvas would.

Rule of thumb: this backend rewards static sprite sheets and moving sprites, and punishes dynamic
pixel data.

---

## Known limitations

1. **No backbuffer readback** — no browser API rasterizes a live DOM subtree. Render into a
   `RenderTarget2D` and read that, or use `CANVAS`.
2. **Texture upload costs a PNG encode** — by design; see design decision 6 in `plan_html_dom.md`.
3. **`TextureAddressMode::Mirror` throws** when the source rectangle leaves the texture.
4. **Custom `BlendState`s throw** — only the four standard presets exist in CSS compositing.
5. **No custom `Effect`s** — there is no shader stage.
6. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER` and `CANVAS`.
7. **`ColorWriteChannels` / `MultiSampleMask` are inexpressible.**
8. **Sprite count drives DOM size** — this targets normal 2D games, not particle storms.
9. **`mix-blend-mode: plus-lighter` requires a current browser** (Chromium 108+, Safari 16.4+,
   Firefox 122+). On anything older, `Additive` composites as normal alpha blending instead of
   failing — the one place this backend degrades silently rather than throwing, because the CSS
   value is simply ignored by the engine before any CNA code can observe it.
