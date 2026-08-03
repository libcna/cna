# HTML DOM Backend — Capability Status

`HTML_DOM` is CNA's DOM/CSS graphics backend: Emscripten-only, 2D-only, rendering `SpriteBatch`
output as pooled `<div>` elements styled with CSS transforms rather than rasterizing into a
`<canvas>`. See `plan_html_dom.md` for the design decisions and task breakdown this document
summarizes.

**Status legend** (this project's convention): ✅ implemented *and verified against its stated
acceptance criteria*; 🟨 code exists but has not met those criteria; ⬜ not implemented.

**What ✅ means here.** Unlike the `CANVAS` backend's own document, ✅ on this page is backed by a
real Emscripten build (emsdk 6.0.5) and, for everything the test pages below cover, by real runs in
headless Chromium via `scripts/run-htmldom-browser-test.sh`. Four PASS/FAIL pages plus one visual
demo, driven by the same harness, together assert against the actual DOM/pixels/timing the backend
produced:

| Page | What it checks | Result |
|---|---|---|
| `cna_test_htmldom_smoke` | DOM surface, sprite pool/recycling, `RenderTarget2D` readback, backbuffer refusal, `SpriteFont`, `TextureAddressMode::Wrap`/`Mirror`/mixed-axis, `SetScissorRect` (per-batch region isolation, region container has a real non-zero layout box, true per-flush paint order across regions, non-destructive eviction), resize + scissor interaction, `SetViewport` sub-rectangle | 43/43 + 2 real screenshot pixel checks |
| `cna_test_htmldom_pixel_verification` | Pixel-exact tint/`AlphaBlend`/`Opaque`/`Additive`, multi-glyph `SpriteFont` (kerning/`\n`/scale/flip), `transformMatrix`, render-target-as-`Draw()`-source, plain-texture `GetData`, `FromStream` decode, `TextureAddressMode::Mirror`/mixed-axis tiling | 15/15 |
| `cna_test_htmldom_stress` | Performance benchmark, 300-frame stability run, LRU cache eviction | 3/3 |
| `cna_test_htmldom_dispose` | Texture/render-target dispose actually shrinks the JS texture registry, bound-target auto-unbind, create/destroy churn | 6/6 |
| `cna_htmldom_visual_demo` | Screenshot-verified visual demo (not a PASS/FAIL page) | — |

Plus 38 GTest cases for everything pure-C++ (blend mapping, address-mode validation, the sprite
geometry encoder, the 3D throw surface, inert-state-setter audit, `SetViewport`) under
`node CnaTests.js`. Rows marked 🟨 are implemented and code-reviewed but not covered by any of
these runs.

Select it with:

```bash
emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_BACKEND=HTML_DOM
cmake --build cmake-build-htmldom --target cna_test_htmldom_smoke -j3
scripts/run-htmldom-browser-test.sh cmake-build-htmldom
```

To build and run all four pages (smoke/pixel/stress/dispose) in one command instead, use the
HTMLDOM-112 suite runner, which also backs `.github/workflows/htmldom-ci.yml`:

```bash
emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_BACKEND=HTML_DOM \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
scripts/run-htmldom-test-suite.sh cmake-build-htmldom
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
| Colour tint | ✅ | Exact: a cached pre-tinted copy of the texture (RGB multiplied per pixel, alpha untouched). No CSS `filter` approximation. Pixel-verified: a known texel under a known tint reads back within 1 unit of `round(src*tint/255)` per channel. **A real-time `feColorMatrix` filter alternative was measured and rejected (HTMLDOM-99)**: ~2× slower for the common steady-state case (a handful of stable, already-cached tints) despite being ~35-40% faster for heavy per-frame tint churn — the steady-state regression conflicts with this backend's core "zero cost when nothing changes" premise, so the PNG-variant cache stays. |
| Tint alpha | ✅ | `opacity`, free at composite time. |
| `transformMatrix` in `Begin()` | ✅ | A leading CSS `matrix(M11,M12,M21,M22,M41,M42)` per sprite, so two batches with different transforms coexist in one frame. Omitted entirely for Identity. Pixel-verified with translation and scale matrices on the render-target draw path. |
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
| `RenderTarget2D` used as a `Draw()` source texture | ✅ | Sampling a target's own rendered content back into an ordinary sprite draw (render-to-texture) — pixel-verified: content cleared into RT A and later `Draw()`n from RT A onto RT B reads back on B exactly, confirming the data-URL-regenerated-from-a-dirty-flag path round-trips real content. |
| `RenderTarget2D` readback (`GetData`) | ✅ | Real synchronous `getImageData`, with the full REMED-GFX-127 argument-validation contract. Verified in-browser. |
| `RenderTargetUsage` | ✅ | A shared `GraphicsDevice` concern; no backend-specific code. |
| `Texture2D::GetData` on a plain (non-render-target) texture | ✅ | Entirely shared/backend-agnostic code (reads from `Texture2D`'s own `cpuPixels_` CPU-side shadow, never touching the backend) — but had never been exercised end-to-end under this backend specifically until now. Verified in-browser byte-exact against four distinct per-texel colours (including non-255 alpha), ruling out both a wrong-channel and a wrong-texel-order bug. |
| `Texture2D::FromStream` decode | ✅ | The decode itself (`stb_image`, via `SaveAsPng`/`FromStream`) is fully backend-agnostic shared code; what had never been proven under HTML_DOM specifically were the two backend-touching ends of that pipeline. Verified in-browser: a source texture created and uploaded through this backend is encoded to a real PNG via `SaveAsPng`, decoded back via `FromStream` (re-uploading through this backend's real canvas/data-URL machinery a second time), and read back matching the original colour. |
| `SetRenderTargets` (MRT) | ✅ throws-by-design | One canvas backs each target; it is inherently single-output. |
| **Backbuffer readback** | ✅ throws-by-design | **The one thing `CANVAS` can do and this backend cannot.** No browser API rasterizes a live DOM subtree. The exception says so and points at `RenderTarget2D`. Verified in-browser. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ (fixed) | HTMLDOM-100: `BlendState::Opaque` uses symmetric `One`/`Zero` factors for BOTH colour and alpha (confirmed via this project's own `BlendState.cpp`), i.e. the destination is replaced by the source pixel INCLUDING its own alpha, not forced to 255. The two draw paths reproduce this differently. Canvas2D render-target path: STRAIGHT (as-uploaded) pixels composited with Porter-Duff `'copy'`, clipped to exactly the sprite's own footprint first (`'copy'` is evaluated over the WHOLE compositing area, not just the drawn shape — the same pitfall `plan_canvas.md` CANVAS-44 found and fixed, applied here identically). Pixel-verified with a genuinely semi-transparent source: reads back at the source's own alpha, not 255. DOM `<div>` backbuffer path: still drawn from an alpha-stripped copy — CSS has no operator that can replace what's beneath a stacked element while still showing a partial source alpha through it, so full opacity is the closest a `<div>` can get; this is an accepted, documented deviation for that path only, not a bug. An earlier version used the alpha-stripped copy unconditionally on BOTH paths, forcing alpha to 255 even on the Canvas2D path where a correct replacement was possible. |
| `AlphaBlend` | ✅ | Drawn from an un-premultiplied copy: `srcBlend=One` assumes already-premultiplied source, while CSS composites straight alpha and premultiplies internally. Pixel-verified with a hand-premultiplied texel (mirroring `SDL_RENDERER`'s own Task 697 test) — the exact category of algebra bug `CANVAS`'s external review found genuinely wrong in that sibling backend, confirmed correct here rather than assumed from the ported logic alone. |
| `NonPremultiplied` | ✅ | Pixels as uploaded — exactly what CSS assumes natively. |
| `Additive` | ✅ | `mix-blend-mode: plus-lighter`, CSS Compositing's exact `lighter` operator. Pixel-verified: two overlapping opaque colours read back as their exact channel-wise sum (clamped), not a plain overwrite or an average. |
| Any other custom `BlendState` | ✅ throws-by-design | CSS has no blend-factor or blend-equation model to approximate one with. |
| `ColorWriteChannels` / `MultiSampleMask` | ⬜ inexpressible | No per-channel write mask and no coverage mask exist in CSS compositing. Documented gap, not a silent drop. |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | `image-rendering: auto` vs `pixelated`, using the same magnification-dominant grouping `SDL_RENDERER` (Task 701) and `CANVAS` (CANVAS-42) use. |
| `TextureAddressMode::Clamp` | ✅ | Implemented for real: the source rectangle is narrowed into the texture and the sprite's local box shifted to match — not a reliance on implicit out-of-bounds behaviour. |
| `TextureAddressMode::Wrap` | ✅ | CSS `background-repeat: repeat` on the DOM path; a repeating Canvas2D pattern on the render-target path (two separate code paths). Only differs from Clamp when the source rectangle leaves the texture. Both verified in-browser: the DOM path keeps its full (unclamped) element width and gets `background-repeat: repeat`; the render-target path is checked pixel-exact — a 2x2 source tiled into a 4x4 target reads back with every texel matching `source(x%2, y%2)`. |
| `TextureAddressMode::Mirror` (symmetric) | ✅ | Implemented via a cached, lazily-built 2x2 pre-tiled-and-mirrored variant (the base variant drawn once per quadrant, alternate quadrants flipped), the same technique `CANVAS` proved out for its own equivalent gap. Tiling that image with ordinary CSS `background-repeat: repeat`/`CanvasPattern('repeat')` reproduces mirror-repeat exactly, by construction. Verified pixel-exact in a real browser: a 2x2 four-colour source tiled 2x2 reads back the full hand-derived reflected grid (HTMLDOM-97a). Note: `SamplerState` has no built-in Mirror preset at all — a game always constructs a custom one, and symmetric U/V is simply the natural way to do that, not a claim about a specific preset. |
| Mixed per-axis modes, out of bounds | ✅ (non-Mirror); Mirror-mixed-with-a-different-axis still throws | Non-`Mirror` mixed axes (e.g. U=Wrap, V=Clamp) now tile/clamp each axis independently via `background-repeat`'s two-value shorthand (DOM path) or `CanvasPattern`'s `repeat-x`/`repeat-y` repetition string (Canvas2D path) — verified pixel-exact (HTMLDOM-97b) and structurally (HTMLDOM-97d, where the CSSOM serializes the two-value form back as the `repeat-x` shorthand). Mirror combined with a genuinely different mode on the other axis remains a narrower, documented residual throw — real extra complexity for a combination no realistic `SamplerState` configuration produces. |

## 5. Viewport / PresentationParameters / Rasterizer

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | ✅ | The same `FixedHeightDynamicWidth` logical-size math every other backend uses. |
| Logical→physical scaling | ✅ | One CSS `scale()` on the surface element. Applied only when the geometry actually changed — these are the backend's only layout-affecting writes. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | Exact inverses; correct mouse mapping under scaling. |
| `Present()` | ✅ | Hides only the pool elements this frame did not use (high-water tracked). There is nothing to swap — the compositor presents. |
| `GraphicsDevice.ScissorRectangle` (`SetScissorRect`) | ✅ | Real `clip-path: inset(...)`, applied regardless of `RasterizerState.ScissorTestEnable` — matching `SDL_RENDERER`'s own behaviour there (confirmed by reading its source, not assumed). **Per-`SpriteBatch`-batch correctness (HTMLDOM-94)**: sprites are routed into one of several DOM "regions", one per distinct scissor rect a batch flushed under, each with its own independent clip-path and sprite pool — so a LATER batch's different scissor rect never retroactively reclips an EARLIER batch's sprites, and vice versa. A rect currently covering the whole surface collapses to the same zero-cost default region every sprite used before this existed, so the common no-scissor case costs nothing extra. Scoped honestly at batch granularity (matching this backend's one-flush-per-`Begin()`/`End()` architecture and real XNA/FNA `SpriteBatch.Deferred` semantics, where `ScissorRectangle` is likewise read once at flush time, not per original `Draw()` call), not literally per individual `Draw()` call inside one still-open batch. Verified in-browser for an inside-surface rect, a full-surface reset (collapses to the default region), a past-bounds rect (insets clamp to 0), and the actual cross-batch isolation itself (an earlier batch's region keeps its own clip after a later batch changes the rect). **Survives a physical window resize with no `GraphicsDevice.Reset()`**: each region's rect is stored in logical coordinates and its `clip-path` is re-derived whenever the surface's logical size changes, so the backend's own contract holds even if reached directly. In practice a game never needs this: `GraphicsDevice::UpdateViewportFromWindow()` already resets `ScissorRectangle` to the full new backbuffer on any detected resize, for every backend, before a game could observe stale insets either way (HTMLDOM-93). |
| `GraphicsDevice.Viewport` (`SetViewport`) | ✅ | Confirmed non-gap against every 2D-only sibling (`SDL_RENDERER`/`CANVAS`/`DX3` all leave it as the same inherited no-op) — but `EASYGL` genuinely supports a real GL sub-region `Viewport` (split-screen/sub-panel rendering), and nothing about DOM/CSS compositing rules that out here. Implemented: `#cna-dom-root` itself is positioned/sized to the current viewport sub-rect instead of always the full logical surface, falling back to exactly the prior full-surface behaviour when the viewport covers the whole backbuffer (the common case — confirmed to cost nothing extra). `SetScissorRect`'s region clip-path insets (below) now account for the active viewport's own offset, matching real GL/D3D semantics where scissor and viewport share the same absolute render-target coordinate space. Verified structurally in a real browser (HTMLDOM-98a/b/c): root resizes/repositions exactly to a sub-rect `Viewport`'s own bounds, and a sprite drawn under it keeps unmodified local coordinates. |
| `ApplyRasterizerState` / `ApplyDepthStencilState` / `SetBlendFactor` / `SetReferenceStencil` | ✅ confirmed inert | All four are inherited no-ops, and all four are provably safe to leave that way: depth/stencil state is meaningless since `SupportsDepthStencil()` is always `false`; `SetBlendFactor`'s colour can only matter for a `Blend.BlendFactor` combination, which `ApplyBlendState` already rejects before this backend could ever read it; `ApplyRasterizerState`'s cull/fill/depth-bias fields have no 2D analogue. GTest-audited with non-default values, not just assumed. |

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

**Measured, not just claimed** (`examples/htmldom_stress_test.cpp`, HTMLDOM-89/90): 500 sprites/
frame, both position AND tint changing every frame (the genuine steady-state case, animated after a
separate warm-up frame that pays the one-time pool-creation cost), averaged **2.338 ms/frame
(~428 fps-equivalent)** over 60 frames in a headless-Chromium container — a shared/virtualized CPU
that is typically slower than a real user's machine, not a best-case number. A further 300-frame run
with sprite counts oscillating 5-50/frame and tints cycling through 300+ distinct values confirmed
the sprite pool stays exactly bounded at the true peak (no leak) and the variant LRU cache caps at
exactly 256 under sustained real eviction pressure, not merely "never filled up".

**Fast here:** a static frame (zero cost — no JS runs and nothing repaints); moving, rotating,
scaling or fading sprites (one `transform`/`opacity` write each, composited on the GPU); large
sprite counts that stay stable frame to frame; batches of any size (one wasm→JS crossing per batch).

**Slow here:** uploading texture pixels (PNG encode per upload); animating a sprite's tint RGB every
frame (a new cached variant per distinct colour, LRU-capped at 256); render-target-heavy frames
(those fall back to Canvas2D rasterization); very high sprite counts, where per-element compositor
layers cost more memory than a single canvas would.

Rule of thumb: this backend rewards static sprite sheets and moving sprites, and punishes dynamic
pixel data.

**Comparative benchmark** (`examples/graphics_backend_benchmark.cpp`, HTMLDOM-92): the identical
500-sprite/frame workload above, built and run under all three Emscripten-capable backends from
three separate `emcmake` configures and measured in the same headless-Chromium harness:

| Backend | ms/frame | fps-equivalent |
|---|---|---|
| `HTML_DOM` | 3.7 – 4.3 | ~233 – 270 |
| `EASYGL` (WebGL2) | 4.66 | ~215 |
| `CANVAS` (Canvas2D) | 27.6 – 29.1 | ~34 – 36 |

`HTML_DOM` is on par with (in this run, marginally faster than) `EASYGL` and roughly 6–7× faster
than `CANVAS` for this specific workload. Honest caveat: this container has no real GPU — `EASYGL`'s
number is software-rasterized (SwiftShader via ANGLE); a real hardware WebGL2 context would very
likely widen `EASYGL`'s advantage over both 2D backends. Read this as "DOM compositing beats
Canvas2D redraw by a wide margin, and is competitive with software-rasterized WebGL2," not as
evidence that `HTML_DOM` outperforms real hardware-accelerated WebGL2.

---

## Known limitations

1. **No backbuffer readback** — no browser API rasterizes a live DOM subtree. Render into a
   `RenderTarget2D` and read that, or use `CANVAS`.
2. **Texture upload costs a PNG encode** — by design; see design decision 6 in `plan_html_dom.md`.
3. **`TextureAddressMode::Mirror` mixed with a different mode on the other axis throws** when the
   source rectangle leaves the texture (HTMLDOM-97) — symmetric Mirror and non-Mirror mixed axes
   (e.g. Wrap+Clamp) are both supported now.
4. **Custom `BlendState`s throw** — only the four standard presets exist in CSS compositing.
5. **No custom `Effect`s** — there is no shader stage.
6. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER` and `CANVAS`.
7. **`ColorWriteChannels` / `MultiSampleMask` are inexpressible.**
8. **Sprite count drives DOM size** — this targets normal 2D games, not particle storms.
9. **`mix-blend-mode: plus-lighter` requires a current browser** (Chromium 108+, Safari 16.4+,
   Firefox 122+). On anything older, `Additive` composites as normal alpha blending instead of
   failing — the one place this backend degrades silently rather than throwing, because the CSS
   value is simply ignored by the engine before any CNA code can observe it.
10. **`SetScissorRect` correctness is per-`SpriteBatch` batch, not per individual `Draw()` call**
    (HTMLDOM-94) — matching real XNA/FNA `SpriteBatch.Deferred` semantics, not a shortfall specific
    to this backend. Sprites in different scissor-rect regions paint in the order those regions
    were first created, not necessarily interleaved per-frame draw order across regions — an
    accepted trade-off for keeping different scissor rects from clipping each other's sprites at
    all, which this backend could not do before HTMLDOM-94.
