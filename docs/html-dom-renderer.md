# HTML DOM Renderer — Capability Status

`HTML_DOM` is CNA's DOM/CSS graphics renderer: Emscripten-only, 2D-only, rendering `SpriteBatch`
output as pooled `<div>` elements styled with CSS transforms rather than rasterizing into a
`<canvas>`. See `plan_html_dom.md` for the design decisions and task breakdown this document
summarizes.

**Status legend** (this project's convention): ✅ implemented *and verified against its stated
acceptance criteria*; 🟨 code exists but has not met those criteria; ⬜ not implemented.

**What ✅ means here.** Unlike the `CANVAS` renderer's own document, ✅ on this page is backed by a
real Emscripten build (emsdk 6.0.5) and, for everything the test pages below cover, by real runs in
headless Chromium via `scripts/run-htmldom-browser-test.sh`. Six PASS/FAIL pages plus one visual
demo, driven by the same harness, together assert against the actual DOM/pixels/timing/memory the
renderer produced:

| Page | What it checks | Result |
|---|---|---|
| `cna_test_htmldom_smoke` | DOM surface, sprite pool/recycling, `RenderTarget2D` readback, backbuffer refusal, `SpriteFont`, `TextureAddressMode::Wrap`/`Mirror`/mixed-axis, `SetScissorRect` (per-batch region isolation, region container has a real non-zero layout box, true per-flush paint order across regions, non-destructive eviction, honours `RasterizerState.ScissorTestEnable`), resize + scissor interaction, `SetViewport` sub-rectangle (per-batch, root never moves, no cross-batch retroactive offset), every `CnaPresentationMode`'s real scale/offset geometry under a mismatched aspect ratio, `SetPresentationMode` ordinal validation, `TransformWindowToLogical` outside-Letterbox-bar result (HTMLDOM-108), this browser's real `mix-blend-mode: plus-lighter` support (HTMLDOM-113), `SpriteSortMode::Immediate` genuinely applying scissor-rect state per-draw vs. `Deferred`'s single end-of-batch snapshot (HTMLDOM-118) | 68/68 + 2 real screenshot pixel checks |
| `cna_test_htmldom_pixel_verification` | Pixel-exact tint/`AlphaBlend`/`Opaque`/`Additive`, multi-glyph `SpriteFont` (kerning/`\n`/scale/flip), `transformMatrix`, render-target-as-`Draw()`-source, plain-texture `GetData`, `FromStream` decode, `TextureAddressMode::Mirror`/mixed-axis tiling, render-target `Viewport` offset, render-target scissor clip (enabled vs. disabled), render-target `Viewport`+`ScissorRectangle` active simultaneously (HTMLDOM-113), `TextureAddressMode::Clamp` edge-extension (fully outside, point vs. linear, scaled+tinted), `NonPremultiplied`/`Additive` translucent-source alpha (documented gap), zero-alpha, tint alpha including premultiplied `AlphaBlend` alpha-only fades (HTMLDOM-124), translucent render-target write/readback/resample premultiplied-alpha round trip (HTMLDOM-106), `Wrap`/`Mirror` phase alignment for a negative source-rectangle origin, a large-fractional-scale/linear-filtering atlas edge-bleed check on BOTH the Canvas2D and DOM paths (HTMLDOM-119) | 36/36 + 2 real screenshot pixel checks |
| `cna_test_htmldom_stress` | Performance benchmark, 300-frame stability run, variant-cache eviction under sustained churn, deterministic LRU hit-promotion/eviction-identity (HTMLDOM-109), `SetData` cache regeneration, byte-identical static-resubmit flush-call/CSS-write instrumentation (HTMLDOM-110) | 10/10 |
| `cna_test_htmldom_dispose` | Texture/render-target dispose actually shrinks the JS texture registry, bound-target auto-unbind, create/destroy churn, texture destruction and render-target rebinding each shrink the global variant cache by exactly their own contribution (HTMLDOM-109), a second `HtmlDomRenderer` sharing one window is reference-counted and destroying it does not tear down the first, still-alive renderer's shared DOM surface (HTMLDOM-114) | 17/17 |
| `cna_htmldom_visual_demo` | Screenshot-verified visual demo (not a PASS/FAIL page) | — |
| `host-integration` (reuses the smoke page's own `.html`, driven by `scripts/htmldom-host-integration-test.mjs`) | A host page's own pre-existing canvas `visibility` value is captured and (implicitly, by the same code path) restored rather than assumed unset; a pre-existing, host-page-owned element with a colliding id is detected and refused rather than silently adopted (HTMLDOM-115) | 2/2 |
| `cna_test_htmldom_memory` | 5,000/10,000-sprite pool-creation bursts stay within a sane time budget; the sprite pool's compositor-layer/DOM retention genuinely SHRINKS after a sustained settled idle period instead of permanently pinning at its session peak; the pool regrows normally afterward; ordinary fluctuating sprite counts never trigger the shrink (HTMLDOM-116) | 6/6 |

Plus 53 GTest cases for everything pure-C++ (blend mapping, address-mode validation, the sprite
geometry encoder, the 3D throw surface, inert-state-setter audit, `SetViewport`, `SetImmediateMode`,
direct-renderer argument validation for out-of-range ordinals/negative sizes/null pointers)
under `node CnaTests.js`. Rows marked 🟨 are implemented and code-reviewed but not covered by any of
these runs.

Select it with:

```bash
emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_RENDERER=HTML_DOM
cmake --build cmake-build-htmldom --target cna_test_htmldom_smoke -j3
scripts/run-htmldom-browser-test.sh cmake-build-htmldom
```

To build and run all four pages (smoke/pixel/stress/dispose) in one command instead, use the
HTMLDOM-112 suite runner, which also backs `.github/workflows/htmldom-ci.yml`:

```bash
emcmake cmake -S . -B cmake-build-htmldom -DCNA_GRAPHICS_RENDERER=HTML_DOM \
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
| tint alpha | `opacity`; `AlphaBlend` first restores the draw colour's straight RGB, so an alpha-only fade changes no texture-variant key (HTMLDOM-124) |
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
| Scalar / `Vector2` scale overloads | ✅ | Renderer-agnostic — `SpriteBatch.cpp` reduces every overload to the same `(destRect, srcRect, …)` call. |
| `SpriteEffects` flips | ✅ | Mirror about the sprite's own centre, leaving the destination footprint unchanged — real XNA semantics. |
| Colour tint | ✅ | Exact: a cached pre-tinted copy of the texture (RGB multiplied per pixel, alpha untouched). No CSS `filter` approximation. Under `AlphaBlend`, XNA's premultiplied draw-colour RGB is restored to straight RGB before it becomes the variant key/tint, because alpha is applied separately as CSS opacity (HTMLDOM-124). Pixel-verified: a known texel under a known tint reads back within 1 unit of `round(src*tint/255)` per channel. **A real-time `feColorMatrix` filter alternative was measured and rejected (HTMLDOM-99)**: ~2× slower for the common steady-state case (a handful of stable, already-cached tints) despite being ~35-40% faster for heavy per-frame tint churn — the steady-state regression conflicts with this renderer's core "zero cost when nothing changes" premise, so the PNG-variant cache stays. |
| Tint alpha | ✅ | `opacity`, free at composite time. An `AlphaBlend` alpha-only fade now retains white RGB at every alpha, reuses one cached variant, and changes only opacity instead of creating and PNG-encoding a new full-texture tint (HTMLDOM-124). |
| `transformMatrix` in `Begin()` | ✅ | A leading CSS `matrix(M11,M12,M21,M22,M41,M42)` per sprite, so two batches with different transforms coexist in one frame. Omitted entirely for Identity. Pixel-verified with translation and scale matrices on the render-target draw path. |
| Custom `Effect` via `Begin(effect)` | ✅ throws-by-design | CSS compositing has no programmable shader stage. |
| `SpriteSortMode` sequencing | ✅ | Ordering itself is renderer-agnostic: sorting happens in shared `SpriteBatch.cpp`, and DOM document order realizes the result. **`Immediate`'s own distinct TIMING semantics were a separate, real gap, now fixed (HTMLDOM-118)**: `SpriteBatch.cpp` already forwarded each Immediate `Draw()` straight to the renderer (bypassing its own queue), but `HtmlDomSpriteBatchRenderer` had no way to know it — `ISpriteBatchRenderer` carried no sort-mode signal at all — so it queued every draw regardless and flushed once at `End()`, identical to `Deferred`. A device state change (e.g. `SetScissorRect`) between two `Draw()` calls in one Immediate `Begin()/End()` block was therefore silently NOT reflected per sprite. Fixed via a new, purely additive `SetImmediateMode(bool)` on the interface (default no-op — every other renderer's behavior is unchanged); HTML_DOM now flushes each Immediate sprite to the DOM the instant it is drawn, under whatever state is current at that exact moment. Verified in-browser with a real scissor-rect-interleaving test. **Not fixed by this task, and not claimed to be**: `EasyGL` and `SdlGpu` (and, by the same batching architecture, likely every other GPU-command-buffer renderer) have the identical Immediate-behaves-like-Deferred gap — confirmed while researching this task, out of scope for it, and not yet tracked by its own ticket in either of those renderers' own plans. |
| `SpriteFont` (`DrawString`, kerning, `\n`, flip, rotation) | ✅ | Needs zero renderer-specific code — every glyph funnels through the same `Draw` overload. Verified in-browser: a `DrawString` glyph lands in the DOM as a real, correctly-sized, PNG-textured element. Kerning/`\n`/flip themselves are shared `SpriteFont`/`SpriteBatch` logic, not re-verified per renderer. |
| Element pooling / recycling across frames | ✅ | Verified in-browser: two sprites then one leaves one visible and two retained. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData` | ✅ | Private off-screen canvas per texture; CSS-usable PNG data URLs derived from it on demand. |
| Texture upload cost | ⚠️ by design | **HTMLDOM-117 correction**: the **PNG encode** is not paid at upload time -- `cnaDomEnsureUrl` runs from the sprite-FLUSH path, not from texture creation/`SetData`, so the cost is genuinely lazy (paid the first time a DRAW actually needs a given texture/tint/mode variant's URL, then cached) and a texture that is uploaded but never drawn never pays it at all. Fine once per variant at load time; expensive every frame for anything re-uploaded AND re-drawn every frame. Use `CANVAS` or `EASYGL` for per-frame `SetData`. |
| Mip-level (`level>0`) `SetData` | ✅ throws-by-design | No mip chain exists here — the same boundary `CANVAS` and `SDL_RENDERER` draw. |
| `RenderTarget2D` construction / bind / unbind | ✅ | Backed by a real off-screen canvas; while bound, draws replay into its Canvas2D context. |
| `RenderTarget2D` used as a `Draw()` source texture | ✅ (fixed for translucent content) | Sampling a target's own rendered content back into an ordinary sprite draw (render-to-texture) — pixel-verified: content cleared into RT A and later `Draw()`n from RT A onto RT B reads back on B exactly, confirming the data-URL-regenerated-from-a-dirty-flag path round-trips real content. HTMLDOM-106: a render target's own canvas is always straight (non-premultiplied) alpha on any Canvas2D read — `getImageData`'s own contract, regardless of which `BlendState` drew it — unlike an UPLOADED texture's bytes, which `AlphaBlend` assumes are already premultiplied. Sampling a render target under `AlphaBlend` previously divided its already-straight bytes by alpha a SECOND time; the original round-trip test used only fully opaque content (alpha=255), where that division is a structural no-op, so it could never have caught this. `cnaDomGetVariant` now tags each texture entry `isRenderTarget` at creation and downgrades a requested un-premultiply to a plain straight fetch for any entry so marked. Pixel-verified with a genuinely translucent render target sampled under `AlphaBlend` onto both a transparent and a non-transparent destination, matching the browser's real `source-over` algebra by hand in both cases. |
| `RenderTarget2D` readback (`GetData`) | ✅ | Real synchronous `getImageData`, with the full REMED-GFX-127 argument-validation contract. Verified in-browser. |
| `RenderTarget2D`/`Texture2D` construction with a zero/negative width or height | ✅ throws (HTMLDOM-120) | Neither `Texture2D.cpp` (upper-bound-only) nor `RenderTarget2D.cpp` (no size validation at all — it never even reaches `Texture2D`'s own checks) rejects this anywhere in the shared layer; `HtmlDomTextureRenderer`'s own constructors (used by both `Texture2D` and, via `HtmlDomRenderTargetRenderer`, `RenderTarget2D`) are the only place in the whole chain that caught it, now throwing `System::ArgumentOutOfRangeException` before a degenerate size would otherwise reach `new OffscreenCanvas(w,h)`/`canvas.width=w`, whose own behavior there is browser-implementation-defined. |
| `RenderTargetUsage` | ✅ | A shared `GraphicsDevice` concern; no renderer-specific code. `RenderTargetUsage.PreserveContents`/`DiscardContents` is genuinely normalized upstream (`GraphicsDevice::SetRenderTarget` issues its own `Clear()` on `DiscardContents`) — this renderer's own constructor parameter for it is correctly unused, not a silent drop. |
| `depthFormat`/`mipMap`/`preferredMultiSampleCount` requested at `RenderTarget2D` construction | ⚠️ silently ignored (audited, HTMLDOM-120) | Unlike `RenderTargetUsage`, these three reach `HtmlDomRenderer::CreateRenderTarget2D` and are then genuinely discarded — this renderer has no depth storage, no mip chain and no MSAA to configure, the same boundary `SDL_RENDERER`/`CANVAS` share (byte-identical signatures and behavior in both). `multiSampleCount` is at least reported honestly afterward (`RenderTarget2D`'s own `MultiSampleCount` property is corrected to the renderer's real `GetMultiSampleCount()`, always 0 here); `DepthFormat`/`LevelCount` are NOT similarly corrected anywhere in the shared layer, so a caller reading those properties back after construction sees whatever was *requested*, not what the renderer actually has — a real, if narrow, honesty gap in the shared `RenderTarget2D.cpp`, not fixed here since correcting it is a cross-renderer decision affecting every 2D-only renderer's reported properties, not something to change unilaterally for HTML_DOM alone. |
| `Texture2D::GetData` on a plain (non-render-target) texture | ✅ | Entirely shared/renderer-agnostic code (reads from `Texture2D`'s own `cpuPixels_` CPU-side shadow, never touching the renderer) — but had never been exercised end-to-end under this renderer specifically until now. Verified in-browser byte-exact against four distinct per-texel colours (including non-255 alpha), ruling out both a wrong-channel and a wrong-texel-order bug. |
| `Texture2D::FromStream` decode | ✅ | The decode itself (`stb_image`, via `SaveAsPng`/`FromStream`) is fully renderer-agnostic shared code; what had never been proven under HTML_DOM specifically were the two renderer-touching ends of that pipeline. Verified in-browser: a source texture created and uploaded through this renderer is encoded to a real PNG via `SaveAsPng`, decoded back via `FromStream` (re-uploading through this renderer's real canvas/data-URL machinery a second time), and read back matching the original colour. |
| `SetRenderTargets` (MRT) | ✅ throws-by-design | One canvas backs each target; it is inherently single-output. `GraphicsDevice` (the only caller a real game goes through) always pairs a null descriptor array with `count=0`; a direct call bypassing it with a non-null count now throws `System::ArgumentNullException` instead of dereferencing a null pointer (HTMLDOM-120). |
| **Backbuffer readback** | ✅ throws-by-design | **The one thing `CANVAS` can do and this renderer cannot.** No browser API rasterizes a live DOM subtree. The exception says so and points at `RenderTarget2D`. Verified in-browser. A null destination pointer or a non-positive/negative region — reachable only via a direct renderer call, not the public API — now throws before crossing into JS rather than risking heap corruption (HTMLDOM-120). |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ (fixed) | HTMLDOM-100: `BlendState::Opaque` uses symmetric `One`/`Zero` factors for BOTH colour and alpha (confirmed via this project's own `BlendState.cpp`), i.e. the destination is replaced by the source pixel INCLUDING its own alpha, not forced to 255. The two draw paths reproduce this differently. Canvas2D render-target path: STRAIGHT (as-uploaded) pixels composited with Porter-Duff `'copy'`, clipped to exactly the sprite's own footprint first (`'copy'` is evaluated over the WHOLE compositing area, not just the drawn shape — the same pitfall `plan_canvas.md` CANVAS-44 found and fixed, applied here identically). Pixel-verified with a genuinely semi-transparent source: reads back at the source's own alpha, not 255. DOM `<div>` backbuffer path: still drawn from an alpha-stripped copy — CSS has no operator that can replace what's beneath a stacked element while still showing a partial source alpha through it, so full opacity is the closest a `<div>` can get; this is an accepted, documented deviation for that path only, not a bug. An earlier version used the alpha-stripped copy unconditionally on BOTH paths, forcing alpha to 255 even on the Canvas2D path where a correct replacement was possible. |
| `AlphaBlend` | ✅ | Drawn from an un-premultiplied copy: `srcBlend=One` assumes already-premultiplied source, while CSS composites straight alpha and premultiplies internally. Pixel-verified with a hand-premultiplied texel (mirroring `SDL_RENDERER`'s own Task 697 test) — the exact category of algebra bug `CANVAS`'s external review found genuinely wrong in that sibling renderer, confirmed correct here rather than assumed from the ported logic alone. The un-premultiply step applies only to UPLOADED texture bytes; a `RenderTarget2D` used as the source is never un-premultiplied a second time (HTMLDOM-106 — see the `RenderTarget2D`-as-`Draw()`-source row above). |
| `NonPremultiplied` | ✅ colour; ⬜ alpha (translucent source) | Pixels as uploaded — exactly what CSS assumes natively, for the COLOUR channels, in every case. `alphaSrcBlend=SourceAlpha` (`BlendState.cpp`) means the real per-factor alpha equation SQUARES the source alpha's own contribution (`result_a = src_a² + dst_a*(1-src_a)`); CSS's `source-over` alpha (`src_a + dst_a*(1-src_a)`, no squaring) cannot reproduce that — no per-channel blend-factor model exists in CSS to express it with (HTMLDOM-105). Coincides exactly with the real XNA/D3D result whenever the SOURCE is opaque (`src_a=255` makes `src_a²==src_a`), which is why this went unnoticed until translucent-source-and-destination raw RGBA readback was tested. Documented architectural limitation, not a silent drop — measured and pixel-verified against real translucent data, not assumed. |
| `Additive` | ✅ colour; ⬜ alpha (translucent source) | `mix-blend-mode: plus-lighter`, CSS Compositing's exact `lighter` operator — for the COLOUR channels, in every case. Pixel-verified: two overlapping opaque colours read back as their exact channel-wise sum (clamped), not a plain overwrite or an average. The SAME `alphaSrcBlend=SourceAlpha` alpha-squaring gap `NonPremultiplied` has applies here too (HTMLDOM-105): two sequential 50%-alpha draws measure alpha `255` (CSS-native Porter-Duff "plus", `min(1,src_a+dst_a)`) where XNA's own squared formula, applied recursively, would give `~128` — the exact scenario and numbers this project's own audit first found. Documented architectural limitation for translucent sources; exact for opaque ones. On a browser without `mix-blend-mode: plus-lighter` support, the CSS value is silently ignored and `Additive` renders as ordinary source-over blending instead — **queryable, not just documented (HTMLDOM-117)**: `GraphicsDevice::SupportsCapability(GraphicsCapability::AdditiveBlending)` reports the running browser's REAL support for it (memoized `CSS.supports('mix-blend-mode', 'plus-lighter')`), the one capability this renderer answers for real rather than a blanket `false`. |
| Any other custom `BlendState` | ✅ throws-by-design | CSS has no blend-factor or blend-equation model to approximate one with. |
| `ColorWriteChannels` / `MultiSampleMask` | ✅ throws-by-design outside defaults | No per-channel write mask or coverage mask exists in CSS compositing. All-channel writes with the default all-samples mask are accepted; any other mask now throws before drawing instead of being silently dropped (HTMLDOM-121). |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | `image-rendering: auto` vs `pixelated`, using the same magnification-dominant grouping `SDL_RENDERER` (Task 701) and `CANVAS` (CANVAS-42) use. |
| `TextureAddressMode::Clamp` | ✅ | Implemented for real (HTMLDOM-104): out-of-bounds sampling repeats the nearest EDGE TEXEL — it does not crop the sprite's own destination geometry. A cached, edge-extended ("padded") texture variant is generated once per (texture, tint, rounded-overflow-amount) and reused via the same single-`background-image`/single-`drawImage` sprite pipeline every other variant uses; overflow beyond 256 texture pixels on any one edge is rejected with a clear error rather than silently cropped. An earlier version narrowed the source rectangle into the texture and shifted the sprite's local box to compensate, which cropped geometry instead — the same defect the corrected `HTMLDOM-97b` test (below) used to assert as correct. |
| `TextureAddressMode::Wrap` | ✅ | CSS `background-repeat: repeat` on the DOM path; a repeating Canvas2D pattern on the render-target path (two separate code paths). Only differs from Clamp when the source rectangle leaves the texture. Both verified in-browser: the DOM path keeps its full (unclamped) element width and gets `background-repeat: repeat`; the render-target path is checked pixel-exact — a 2x2 source tiled into a 4x4 target reads back with every texel matching `source(x%2, y%2)`. |
| `TextureAddressMode::Mirror` (symmetric) | ✅ | Implemented via a cached, lazily-built 2x2 pre-tiled-and-mirrored variant (the base variant drawn once per quadrant, alternate quadrants flipped), the same technique `CANVAS` proved out for its own equivalent gap. Tiling that image with ordinary CSS `background-repeat: repeat`/`CanvasPattern('repeat')` reproduces mirror-repeat exactly, by construction. Verified pixel-exact in a real browser: a 2x2 four-colour source tiled 2x2 reads back the full hand-derived reflected grid (HTMLDOM-97a). Note: `SamplerState` has no built-in Mirror preset at all — a game always constructs a custom one, and symmetric U/V is simply the natural way to do that, not a claim about a specific preset. |
| Mixed per-axis modes, out of bounds | ✅ (non-Mirror); Mirror-mixed-with-a-different-axis still throws | Non-`Mirror` mixed axes (e.g. U=Wrap, V=Clamp) now tile/clamp each axis independently via `background-repeat`'s two-value shorthand (DOM path) or `CanvasPattern`'s `repeat-x`/`repeat-y` repetition string (Canvas2D path) — verified pixel-exact (HTMLDOM-97b) and structurally (HTMLDOM-97d, where the CSSOM serializes the two-value form back as the `repeat-x` shorthand). Mirror combined with a genuinely different mode on the other axis remains a narrower, documented residual throw — real extra complexity for a combination no realistic `SamplerState` configuration produces. |

## 5. Viewport / PresentationParameters / Rasterizer

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | ✅ | HTMLDOM-108: every `CnaPresentationMode` — `Letterbox`, `Overscan`, `Stretch`, `NativeBackBuffer`, `FixedHeightDynamicWidth` — implemented for real via `HtmlDomRenderer::ComputeLogicalViewport`, ported from `SdlGpuRenderer`'s own reference implementation rather than re-derived from scratch. An earlier version only had correct geometry for `FixedHeightDynamicWidth`; every other mode fell through to that same height-derived uniform scale, silently wrong whenever the virtual/physical aspect ratios did not already coincide. `SetPresentationMode` now validates its ordinal and throws `std::out_of_range` for an invalid one, rather than storing it unchecked. |
| Logical→physical scaling | ✅ | HTMLDOM-108: per-`CnaPresentationMode` offset **and** scale (independent per-axis under `Stretch`; uniform, centred, under `Letterbox`/`Overscan`) applied to `#cna-dom-root`'s CSS `left`/`top`/`transform: scale()` — previously a single height-derived uniform `scale()` regardless of mode. Root now lives inside a dedicated `#cna-dom-viewport` wrapper, sized to the physical `<canvas>`'s own bounds with its own `overflow:hidden`, needed for `Overscan`'s deliberately-oversized root (extends past the canvas's own edges on purpose) to actually crop at the canvas boundary rather than bleeding into the host page. Applied only when the geometry actually changed — these are the renderer's only layout-affecting writes. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | HTMLDOM-108: uses the same per-mode `ComputeLogicalViewport` geometry `Present()` renders with, not a single uniform scale. Correctly returns false for a window coordinate inside a `Letterbox` bar (outside the scaled content rectangle), matching `SdlGpuRenderer`'s own contract, while still returning false whenever no virtual resolution is configured at all (a distinct, pre-existing, still-tested case). Exact inverses otherwise; correct mouse mapping under every mode's own scaling. |
| `Present()` | ✅ | Hides only the pool elements this frame did not use (high-water tracked). There is nothing to swap — the compositor presents. |
| `GraphicsDevice.ScissorRectangle` (`SetScissorRect`) | ✅ | Real `clip-path: inset(...)` on the DOM path, and (HTMLDOM-102) a real `ctx.save()/rect()/clip()` on the Canvas2D render-target path — both gated on `RasterizerState.ScissorTestEnable`, captured once per `SpriteBatch` flush; a disabled scissor test means `SetScissorRect`'s recorded rectangle clips nothing at all on either path, matching real XNA (an earlier version applied it unconditionally on the DOM path and not at all on the render-target path). Note that `SpriteBatch::Begin()` always applies EITHER the `RasterizerState` passed to it OR `RasterizerState::CullCounterClockwise` (`ScissorTestEnable=false`) as its own default — a game (or test) that wants scissor clipping under `SpriteBatch` must pass a `RasterizerState` with `ScissorTestEnable=true` to `Begin()` itself, the same real XNA/FNA requirement. **Per-`SpriteBatch`-batch correctness (HTMLDOM-94)**: sprites are routed into one of several DOM "regions", one per distinct scissor rect a batch flushed under, each with its own independent clip-path and sprite pool — so a LATER batch's different scissor rect never retroactively reclips an EARLIER batch's sprites, and vice versa. A rect currently covering the whole surface collapses to the same zero-cost default region every sprite used before this existed, so the common no-scissor case costs nothing extra. Scoped honestly at batch granularity (matching this renderer's one-flush-per-`Begin()`/`End()` architecture and real XNA/FNA `SpriteBatch.Deferred` semantics, where `ScissorRectangle` is likewise read once at flush time, not per original `Draw()` call), not literally per individual `Draw()` call inside one still-open batch. Verified in-browser for an inside-surface rect, a full-surface reset (collapses to the default region), a past-bounds rect (insets clamp to 0), the actual cross-batch isolation itself (an earlier batch's region keeps its own clip after a later batch changes the rect), and the enable bit on both paths (structurally on the DOM path, pixel-exact via real `GetData` on the render-target path). **Survives a physical window resize with no `GraphicsDevice.Reset()`**: each region's rect is stored in logical coordinates and its `clip-path` is re-derived whenever the surface's logical size changes, so the renderer's own contract holds even if reached directly. In practice a game never needs this: `GraphicsDevice::UpdateViewportFromWindow()` already resets `ScissorRectangle` to the full new backbuffer on any detected resize, for every renderer, before a game could observe stale insets either way (HTMLDOM-93). |
| `GraphicsDevice.Viewport` (`SetViewport`) | ✅ | Confirmed non-gap against every 2D-only sibling (`SDL_RENDERER`/`CANVAS`/`DIRECTX3` all leave it as the same inherited no-op) — but `EASYGL` genuinely supports a real GL sub-region `Viewport` (split-screen/sub-panel rendering), and nothing about DOM/CSS compositing rules that out here. **Implemented per-batch (HTMLDOM-107, correcting an earlier per-root-resize approach)**: `#cna-dom-root` always stays at the full backbuffer's own size/position; the active viewport's own `(X,Y)` offset is instead captured once per `SpriteBatch` flush and applied as the outermost translate on that batch's own sprites — on both the DOM `<div>` path and the Canvas2D render-target path (previously ignored there entirely). This means a viewport set later in the same frame can never retroactively move sprites an earlier batch already flushed under a different viewport — the real split-screen/sub-panel case. Scissor-region clip-path insets (below) need no viewport-offset adjustment at all any more, since root never moves. Verified in a real browser: root's own box is provably unaffected by viewport changes; a sprite's CSS transform carries the viewport offset as an added (not substituted) prefix; an earlier batch's sprite keeps its own offset after a later batch sets a different viewport; and a `RenderTarget2D`-bound draw under a sub-rectangle viewport reads back at the correct offset pixel position. |
| `ApplyDepthStencilState` / `SetBlendFactor` / `SetReferenceStencil` | ✅ truthful 2D boundary | A fully-disabled depth/stencil state and reference value zero are accepted. Any enabled depth/stencil operation or non-zero stencil reference throws deterministically. `SetBlendFactor` remains inert only because every blend-factor-dependent custom `BlendState` is rejected first (HTMLDOM-121). |
| `ApplyRasterizerState` | ✅ truthful 2D subset | The ordinary filled 2D/default-cull states and `scissorTestEnable` are accepted. Clockwise-face culling, wireframe, depth bias and invalid enum ordinals throw because this DOM/CSS implementation cannot represent them (HTMLDOM-121). |

## 6. The 3D surface

Every DIRECT 3D entry point this renderer itself overrides throws `std::runtime_error("HTML_DOM
renderer: … not yet implemented")`: depth/stencil clears, `SetDepthTestEnabled`/`SetBlendEnabled`/
`SetDepthWriteEnabled`, vertex and index buffer creation, and both `Draw*Primitives` families.

**HTMLDOM-121 post-audit correction**: all five unsupported resource factories
(`CreateTexture3D`, `CreateTextureCube`, `CreateRenderTargetCube`, `CreateOcclusionQuery`,
`CreateEffectRenderer`) now throw at the HTML DOM renderer boundary. They no longer inherit a null
default that allowed some public wrappers to construct successfully and fail only on later use.
`CreateRenderTarget2D` remains supported only for `SurfaceFormat::Color`, level zero, without mip
maps, depth/stencil or MSAA; every unsupported option throws before allocating a browser object.

`GraphicsDevice::SupportsCapability()` reports `false` for every capability except
`AdditiveBlending` (HTMLDOM-117 — see §3 above), which reports the browser's real support for
`mix-blend-mode: plus-lighter`. `SupportsDepthStencil()` is unconditionally `false`. Both let a
caller branch ahead of time instead of provoking an exception, for the capabilities they cover.

---

## Performance characteristics

**Measured, not just claimed** (`modules/renderers/html-dom/examples/htmldom_stress_test.cpp`, HTMLDOM-89/90): 500 sprites/
frame, tint changing every frame with STATIC position (HTMLDOM-111 correction: despite this
workload's own older "moving sprites" framing, its position formula never actually depends on the
frame number — only tint does, so this is this renderer's own documented heavy-tint-churn WORST
case, not its moving-sprite best one), averaged **2.338 ms/frame** of submission time (Begin/Draw/
End CPU cost only — NOT a frame rate; HTMLDOM-111's own real end-to-end measurement for this same
workload came to ~19 ms/frame, ≈53 real fps, in this same container) over 60 frames after a
warm-up frame — a shared/virtualized CPU that is typically slower than a real user's machine, not
a best-case number. A further 300-frame run with sprite counts oscillating 5-50/frame and tints
cycling through 300+ distinct values confirmed the sprite pool stays exactly bounded at the true
peak (no leak) and the variant LRU cache caps at exactly 256 under sustained real eviction
pressure, not merely "never filled up".

**"Zero cost when nothing changes" (HTMLDOM-110), corrected to what is actually true and
instrumented, not merely asserted**: a real XNA game resubmits its static sprites every frame —
`SpriteBatch.End()` always crosses the wasm/JS boundary and walks every command in the batch
(`Module['cnaDomFlushCallCount']`, one real call per non-empty flush, no fewer) — so "no JS runs"
for an unchanged frame was never literally true. What genuinely reaches zero is CSS property
**writes**: per-property diffing means a value that did not change is never assigned
(`Module['cnaDomStyleWriteCount']`), so nothing changing means no layout, no paint, and no
composite-order work either, since none of those can happen without a write to trigger them.
Measured directly: 200 byte-identical sprites (same position/tint/texture, every single frame, no
variation at all) resubmitted for 30 consecutive frames produced exactly 30 flush calls and exactly
**0** CSS property writes, averaging **0.7-0.8 ms/frame** — real, non-zero, but roughly 30-40%
cheaper per sprite than the 500-*moving*-sprite number above. **The investigation this correction
required found a genuine, previously-undocumented defect while instrumenting**: the default
`'full'`-region sprites' own `z-index` (needed only to interleave paint order against OTHER
regions, HTMLDOM-103) was written on *every single flush unconditionally*, since its own per-flush
paint-order counter never repeats a value — measured at exactly `spriteCount × frameCount` CSS
writes for a scene that never used a scissor rect at all. Fixed: that write is now skipped
entirely for as long as no named region has ever existed this session, which is what let the
static-resubmit measurement above reach a genuine, real zero.

**Fast here:** a static frame (zero CSS writes/layout/paint/composite work — but NOT zero JS, see
above); moving, rotating, scaling or fading sprites (one `transform`/`opacity` write each,
composited on the GPU); large sprite counts that stay stable frame to frame; batches of any size
(one wasm→JS crossing per batch, always, even for unchanged content).

**Slow here:** uploading AND drawing texture pixels (the PNG encode itself is lazy, paid on first
draw after upload, not upload itself -- see the corrected Texture upload cost row above); animating a sprite's tint RGB every
frame (a new cached variant per distinct colour, LRU-capped at 256); render-target-heavy frames
(those fall back to Canvas2D rasterization); very high sprite counts, where per-element compositor
layers cost more memory than a single canvas would.

Rule of thumb: this renderer rewards static sprite sheets and moving sprites, and punishes dynamic
pixel data.

**Comparative benchmark** (`modules/graphics/examples/graphics_renderer_benchmark.cpp`, HTMLDOM-92): the same
500-sprite/frame workload, built and run under all three Emscripten-capable renderers from three
separate `emcmake` configures and measured in the same headless-Chromium harness. **HTMLDOM-111
correction, superseding the table this section used to publish**: the earlier ms/frame numbers here
were submission time only (`SpriteBatch::End()` alone — excludes `Present()` and every deferred
browser layout/paint/composite cost), which structurally favours DOM style writes (cheap, deferred)
over Canvas2D/WebGL work (closer to synchronous) and cannot support an honest cross-renderer "X is
faster" claim. `graphics_renderer_benchmark.cpp` was reworked to report BOTH a submission number
(explicitly labelled as not a frame rate) and a real end-to-end one — the wall-clock gap between
successive `requestAnimationFrame`-paced `Draw()` calls, which can only elapse once the browser
actually finished rendering the previous frame — across TWO workloads (stable tint: position
animates, tint fixed, this renderer's own documented sweet spot; heavy churn: both animate, this
renderer's own documented weak point; the OLD single workload measured only the churn case, despite
being labelled "steady state"). `HTML_DOM`'s own real end-to-end numbers in this container, 500
sprites: **stable tint ~17 ms/frame (~59 real fps)**, **heavy churn ~19 ms/frame (~53 real fps)** —
both close to this machine's own ~60fps vsync floor, meaning the browser's own display cadence, not
either renderer's CPU cost, is the dominant factor at this sprite count. A genuine, re-verified
side-by-side `HTML_DOM`/`CANVAS`/`EASYGL` table under this corrected methodology has not been
re-run as of this correction (the old table's absolute "HTML_DOM 6-7x faster than CANVAS" framing
should not be treated as confirmed under the new, honest methodology until it is) — re-run
`graphics_renderer_benchmark.cpp` per renderer for a current comparison rather than reusing
the old table. The old run's own real, useful finding stands independent of the metric used: this
container has no real GPU (`EASYGL`'s own `WEBGL_debug_renderer_info` query, also added by
HTMLDOM-111, confirms `SwiftShader` software rendering here), so a hardware WebGL2 context would
likely change `EASYGL`'s own relative standing and must never be silently compared against this
software one without saying so.

---

## Known limitations

1. **No backbuffer readback** — no browser API rasterizes a live DOM subtree. Render into a
   `RenderTarget2D` and read that, or use `CANVAS`.
2. **Drawing a texture costs a PNG encode the first time, lazily** — by design; see design
   decision 6 in `plan_html_dom.md` (HTMLDOM-117: the encode is paid on first DRAW, not upload).
3. **`TextureAddressMode::Mirror` mixed with a different mode on the other axis throws** when the
   source rectangle leaves the texture (HTMLDOM-97) — symmetric Mirror and non-Mirror mixed axes
   (e.g. Wrap+Clamp) are both supported now.
4. **Custom `BlendState`s throw** — only the four standard presets exist in CSS compositing.
5. **No custom `Effect`s** — there is no shader stage.
6. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER` and `CANVAS`.
7. **Non-default `ColorWriteChannels` / `MultiSampleMask` are inexpressible and therefore throw.**
8. **Sprite count drives DOM size** — this targets normal 2D games, not particle storms. Every
   pooled sprite `<div>` carries `will-change:transform`, forcing its own permanent compositor
   layer; measured directly (`modules/renderers/html-dom/examples/htmldom_memory_test.cpp`, HTMLDOM-116) rather than left as
   an unmeasured claim: a 5,000/10,000-sprite burst creates that many pool elements in ~44 ms in
   this container (well within a sane budget, no O(n²) blowup). Before HTMLDOM-116, the pool never
   released an element once created — only hidden via `display:none` — so a single transient burst
   permanently retained its peak DOM/layer count for the rest of the session even after usage
   settled back down to a handful. `CNA_HtmlDom_PresentFrame` now ages out the excess once a
   region's sprite count has been genuinely unchanged for 180 consecutive `Present()` calls
   (~3 s at 60 fps) — actually removing (not just hiding) the elements above that settled count and
   shrinking the pool array to match, verified to regrow normally afterward. The 180-frame
   threshold is deliberately longer than any realistic action-to-idle gap between bursts, so
   ordinary fluctuating gameplay (which also dips below its own past peak constantly) never
   triggers it — verified with a 200-frame oscillating-count run that never repeats the same count
   twice in a row.
9. **`mix-blend-mode: plus-lighter` requires a current browser** (Chromium 108+, Safari 16.4+,
   Firefox 122+). On anything older, `Additive` composites as normal alpha blending instead of
   failing — the one place this renderer degrades silently rather than throwing, because the CSS
   value is simply ignored by the engine before any CNA code can observe it. The fallback path
   itself cannot be exercised by an automated CI browser (HTMLDOM-113) — what IS tested is the
   assumption every other Additive test in this suite depends on: `htmldom_smoke_test.cpp` confirms
   via `CSS.supports('mix-blend-mode', 'plus-lighter')` that the CI browser genuinely supports it,
   so those tests are exercising the real blend, not this silent degradation.
10. **`SetScissorRect` correctness is per-`SpriteBatch` batch, not per individual `Draw()` call**
    (HTMLDOM-94) — matching real XNA/FNA `SpriteBatch.Deferred` semantics, not a shortfall specific
    to this renderer. Cross-region paint order (HTMLDOM-103) is a real per-flush `z-index`, matching
    the actual draw sequence across regions, not merely each region's own creation order.
11. **`NonPremultiplied`/`Additive` alpha channel diverges from raw XNA/D3D hardware blending for a
    TRANSLUCENT source** (HTMLDOM-105) — both presets use `alphaSrcBlend=SourceAlpha`, so their real
    per-factor alpha equation squares the source alpha's own contribution; CSS's compositing model
    has no per-channel blend-factor customization to reproduce that with. Colour channels are always
    exact; the alpha channel is exact only when the source is fully opaque (where the squared and
    unsquared formulas coincide).
12. **Effectively one live, actively-driven `GraphicsDevice` per process** (HTMLDOM-114) — not a
    choice this renderer makes, but a real constraint of `emscripten_set_main_loop`'s own
    `simulate_infinite_loop` contract, which `Game::Run()` uses: it never returns to its caller, so
    two independently-driven `Game::Run()` loops cannot coexist in one process. A SECOND
    `HtmlDomRenderer` sharing the same window CAN still be constructed while a first is alive
    (nothing in `IGraphicsRenderer` prevents it, and this is now safe rather than forbidden) — the
    shared DOM surface is reference-counted (`Module['cnaDomRendererRefCount']`), so destroying one
    renderer never tears the surface down out from under another still-live one. Verified in-browser.
13. **A canvas positioned via a CSS `transform` (scale/rotate/skew) is not accounted for**
    (HTMLDOM-115) — `cnaDomApplySurfaceGeometry` positions the DOM surface from the canvas's own
    `offsetLeft`/`offsetTop`, which report LAYOUT position only; CSS transforms are a purely visual,
    post-layout effect neither property reflects at all. A host page that scales/rotates the canvas
    via CSS will see the DOM surface misalign with it. Page SCROLL and the canvas's own offset-parent
    chain are NOT a problem by the same reasoning in reverse: `offsetLeft`/`offsetTop` are relative
    to the nearest positioned ancestor, not the viewport, so both the canvas and the DOM surface's
    own wrapper (a sibling, sharing the same ancestor chain) scroll together and stay aligned without
    any extra handling. A host-page layout reflow that MOVES the canvas without resizing it (a
    responsive breakpoint, an animated `margin`/`left` elsewhere on the page) is caught by a
    `window.resize` listener for the common case (window resize, orientation change); a pure
    CSS-only reposition with no window resize at all is a narrower, accepted gap. Z-order relative
    to other page content is plain DOM order (the wrapper is inserted immediately after the canvas)
    — a host page needing a specific stacking order should give its OWN elements an explicit
    `z-index`, the same as for any two ordinary page elements.
14. **The Canvas2D render-target path bleeds adjacent atlas pixels under linear filtering; the DOM
    path does not** (HTMLDOM-119) — a real, measured divergence between this renderer's two draw
    paths, not a shared characteristic. Drawing an ordinary, IN-BOUNDS source rectangle from a
    larger, unpadded atlas texture (distinct from `TextureAddressMode::Clamp`'s already-fixed
    OUT-of-bounds overflow, HTMLDOM-104) at a large fractional scale under linear filtering: the
    Canvas2D path (active while a `RenderTarget2D` is bound) measurably bleeds the adjacent,
    UNDRAWN atlas region into an edge sample still genuinely inside the drawn source rect — hand-
    derived and confirmed exact (`modules/renderers/html-dom/examples/htmldom_pixel_verification_test.cpp`, HTMLDOM-119):
    sampling 0.397 texels past the last drawn texel's own centre toward the next (undrawn) one
    produces almost precisely the linearly-interpolated blend of the two (measured (147,0,108)
    against a hand-derived (154,0,101)). This is the CORRECT, hardware-matching result — real D3D/
    XNA hardware sampling an unpadded atlas sub-rectangle under `LinearWrap`/`LinearClamp`
    addressing exhibits the identical texel bleed, which is exactly why real games pad their own
    atlases; it is not a CNA-specific corruption. The DOM `<div>` `background-image` path, checked
    identically via a real screenshot, measures ZERO bleed at the same sample point: a
    `background-image` element's own box is a hard clip by CSS spec (`background-repeat:no-repeat`,
    no `background-size` override), structurally unlike `drawImage`'s explicit sub-rectangle-
    extraction-then-resample model, which is exactly where the Canvas2D path's own bleed comes
    from. Not fixed — padding every atlas draw to eliminate the Canvas2D path's bleed would make
    this renderer diverge FROM real hardware behavior for that path, the wrong direction; a game
    that draws the identical sprite through both paths and needs pixel-identical results should pad
    its own atlas (standard practice regardless of renderer) or avoid linear filtering on
    render-target-bound draws specifically.
