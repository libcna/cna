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
| `cna_test_htmldom_smoke` | DOM surface, sprite pool/recycling, `RenderTarget2D` readback, backbuffer refusal, `SpriteFont`, `TextureAddressMode::Wrap`/`Mirror`/mixed-axis, `SetScissorRect` (per-batch region isolation, region container has a real non-zero layout box, true per-flush paint order across regions, non-destructive eviction, honours `RasterizerState.ScissorTestEnable`), resize + scissor interaction, `SetViewport` sub-rectangle (per-batch, root never moves, no cross-batch retroactive offset), every `CnaPresentationMode`'s real scale/offset geometry under a mismatched aspect ratio, `SetPresentationMode` ordinal validation, `TransformWindowToLogical` outside-Letterbox-bar result (HTMLDOM-108), this browser's real `mix-blend-mode: plus-lighter` support (HTMLDOM-113) | 64/64 + 2 real screenshot pixel checks |
| `cna_test_htmldom_pixel_verification` | Pixel-exact tint/`AlphaBlend`/`Opaque`/`Additive`, multi-glyph `SpriteFont` (kerning/`\n`/scale/flip), `transformMatrix`, render-target-as-`Draw()`-source, plain-texture `GetData`, `FromStream` decode, `TextureAddressMode::Mirror`/mixed-axis tiling, render-target `Viewport` offset, render-target scissor clip (enabled vs. disabled), render-target `Viewport`+`ScissorRectangle` active simultaneously (HTMLDOM-113), `TextureAddressMode::Clamp` edge-extension (fully outside, point vs. linear, scaled+tinted), `NonPremultiplied`/`Additive` translucent-source alpha (documented gap), zero-alpha, tint alpha, translucent render-target write/readback/resample premultiplied-alpha round trip (HTMLDOM-106) | 31/31 |
| `cna_test_htmldom_stress` | Performance benchmark, 300-frame stability run, variant-cache eviction under sustained churn, deterministic LRU hit-promotion/eviction-identity (HTMLDOM-109), `SetData` cache regeneration, byte-identical static-resubmit flush-call/CSS-write instrumentation (HTMLDOM-110) | 10/10 |
| `cna_test_htmldom_dispose` | Texture/render-target dispose actually shrinks the JS texture registry, bound-target auto-unbind, create/destroy churn, texture destruction and render-target rebinding each shrink the global variant cache by exactly their own contribution (HTMLDOM-109), a second `HtmlDomGraphicsBackend` sharing one window is reference-counted and destroying it does not tear down the first, still-alive backend's shared DOM surface (HTMLDOM-114) | 17/17 |
| `cna_htmldom_visual_demo` | Screenshot-verified visual demo (not a PASS/FAIL page) | — |

Plus 40 GTest cases for everything pure-C++ (blend mapping, address-mode validation, the sprite
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
| `RenderTarget2D` used as a `Draw()` source texture | ✅ (fixed for translucent content) | Sampling a target's own rendered content back into an ordinary sprite draw (render-to-texture) — pixel-verified: content cleared into RT A and later `Draw()`n from RT A onto RT B reads back on B exactly, confirming the data-URL-regenerated-from-a-dirty-flag path round-trips real content. HTMLDOM-106: a render target's own canvas is always straight (non-premultiplied) alpha on any Canvas2D read — `getImageData`'s own contract, regardless of which `BlendState` drew it — unlike an UPLOADED texture's bytes, which `AlphaBlend` assumes are already premultiplied. Sampling a render target under `AlphaBlend` previously divided its already-straight bytes by alpha a SECOND time; the original round-trip test used only fully opaque content (alpha=255), where that division is a structural no-op, so it could never have caught this. `cnaDomGetVariant` now tags each texture entry `isRenderTarget` at creation and downgrades a requested un-premultiply to a plain straight fetch for any entry so marked. Pixel-verified with a genuinely translucent render target sampled under `AlphaBlend` onto both a transparent and a non-transparent destination, matching the browser's real `source-over` algebra by hand in both cases. |
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
| `AlphaBlend` | ✅ | Drawn from an un-premultiplied copy: `srcBlend=One` assumes already-premultiplied source, while CSS composites straight alpha and premultiplies internally. Pixel-verified with a hand-premultiplied texel (mirroring `SDL_RENDERER`'s own Task 697 test) — the exact category of algebra bug `CANVAS`'s external review found genuinely wrong in that sibling backend, confirmed correct here rather than assumed from the ported logic alone. The un-premultiply step applies only to UPLOADED texture bytes; a `RenderTarget2D` used as the source is never un-premultiplied a second time (HTMLDOM-106 — see the `RenderTarget2D`-as-`Draw()`-source row above). |
| `NonPremultiplied` | ✅ colour; ⬜ alpha (translucent source) | Pixels as uploaded — exactly what CSS assumes natively, for the COLOUR channels, in every case. `alphaSrcBlend=SourceAlpha` (`BlendState.cpp`) means the real per-factor alpha equation SQUARES the source alpha's own contribution (`result_a = src_a² + dst_a*(1-src_a)`); CSS's `source-over` alpha (`src_a + dst_a*(1-src_a)`, no squaring) cannot reproduce that — no per-channel blend-factor model exists in CSS to express it with (HTMLDOM-105). Coincides exactly with the real XNA/D3D result whenever the SOURCE is opaque (`src_a=255` makes `src_a²==src_a`), which is why this went unnoticed until translucent-source-and-destination raw RGBA readback was tested. Documented architectural limitation, not a silent drop — measured and pixel-verified against real translucent data, not assumed. |
| `Additive` | ✅ colour; ⬜ alpha (translucent source) | `mix-blend-mode: plus-lighter`, CSS Compositing's exact `lighter` operator — for the COLOUR channels, in every case. Pixel-verified: two overlapping opaque colours read back as their exact channel-wise sum (clamped), not a plain overwrite or an average. The SAME `alphaSrcBlend=SourceAlpha` alpha-squaring gap `NonPremultiplied` has applies here too (HTMLDOM-105): two sequential 50%-alpha draws measure alpha `255` (CSS-native Porter-Duff "plus", `min(1,src_a+dst_a)`) where XNA's own squared formula, applied recursively, would give `~128` — the exact scenario and numbers this project's own audit first found. Documented architectural limitation for translucent sources; exact for opaque ones. |
| Any other custom `BlendState` | ✅ throws-by-design | CSS has no blend-factor or blend-equation model to approximate one with. |
| `ColorWriteChannels` / `MultiSampleMask` | ⬜ inexpressible | No per-channel write mask and no coverage mask exist in CSS compositing. Documented gap, not a silent drop. |

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
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | ✅ | HTMLDOM-108: every `CnaPresentationMode` — `Letterbox`, `Overscan`, `Stretch`, `NativeBackBuffer`, `FixedHeightDynamicWidth` — implemented for real via `HtmlDomGraphicsBackend::ComputeLogicalViewport`, ported from `SdlGpuGraphicsBackend`'s own reference implementation rather than re-derived from scratch. An earlier version only had correct geometry for `FixedHeightDynamicWidth`; every other mode fell through to that same height-derived uniform scale, silently wrong whenever the virtual/physical aspect ratios did not already coincide. `SetPresentationMode` now validates its ordinal and throws `std::out_of_range` for an invalid one, rather than storing it unchecked. |
| Logical→physical scaling | ✅ | HTMLDOM-108: per-`CnaPresentationMode` offset **and** scale (independent per-axis under `Stretch`; uniform, centred, under `Letterbox`/`Overscan`) applied to `#cna-dom-root`'s CSS `left`/`top`/`transform: scale()` — previously a single height-derived uniform `scale()` regardless of mode. Root now lives inside a dedicated `#cna-dom-viewport` wrapper, sized to the physical `<canvas>`'s own bounds with its own `overflow:hidden`, needed for `Overscan`'s deliberately-oversized root (extends past the canvas's own edges on purpose) to actually crop at the canvas boundary rather than bleeding into the host page. Applied only when the geometry actually changed — these are the backend's only layout-affecting writes. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | HTMLDOM-108: uses the same per-mode `ComputeLogicalViewport` geometry `Present()` renders with, not a single uniform scale. Correctly returns false for a window coordinate inside a `Letterbox` bar (outside the scaled content rectangle), matching `SdlGpuGraphicsBackend`'s own contract, while still returning false whenever no virtual resolution is configured at all (a distinct, pre-existing, still-tested case). Exact inverses otherwise; correct mouse mapping under every mode's own scaling. |
| `Present()` | ✅ | Hides only the pool elements this frame did not use (high-water tracked). There is nothing to swap — the compositor presents. |
| `GraphicsDevice.ScissorRectangle` (`SetScissorRect`) | ✅ | Real `clip-path: inset(...)` on the DOM path, and (HTMLDOM-102) a real `ctx.save()/rect()/clip()` on the Canvas2D render-target path — both gated on `RasterizerState.ScissorTestEnable`, captured once per `SpriteBatch` flush; a disabled scissor test means `SetScissorRect`'s recorded rectangle clips nothing at all on either path, matching real XNA (an earlier version applied it unconditionally on the DOM path and not at all on the render-target path). Note that `SpriteBatch::Begin()` always applies EITHER the `RasterizerState` passed to it OR `RasterizerState::CullCounterClockwise` (`ScissorTestEnable=false`) as its own default — a game (or test) that wants scissor clipping under `SpriteBatch` must pass a `RasterizerState` with `ScissorTestEnable=true` to `Begin()` itself, the same real XNA/FNA requirement. **Per-`SpriteBatch`-batch correctness (HTMLDOM-94)**: sprites are routed into one of several DOM "regions", one per distinct scissor rect a batch flushed under, each with its own independent clip-path and sprite pool — so a LATER batch's different scissor rect never retroactively reclips an EARLIER batch's sprites, and vice versa. A rect currently covering the whole surface collapses to the same zero-cost default region every sprite used before this existed, so the common no-scissor case costs nothing extra. Scoped honestly at batch granularity (matching this backend's one-flush-per-`Begin()`/`End()` architecture and real XNA/FNA `SpriteBatch.Deferred` semantics, where `ScissorRectangle` is likewise read once at flush time, not per original `Draw()` call), not literally per individual `Draw()` call inside one still-open batch. Verified in-browser for an inside-surface rect, a full-surface reset (collapses to the default region), a past-bounds rect (insets clamp to 0), the actual cross-batch isolation itself (an earlier batch's region keeps its own clip after a later batch changes the rect), and the enable bit on both paths (structurally on the DOM path, pixel-exact via real `GetData` on the render-target path). **Survives a physical window resize with no `GraphicsDevice.Reset()`**: each region's rect is stored in logical coordinates and its `clip-path` is re-derived whenever the surface's logical size changes, so the backend's own contract holds even if reached directly. In practice a game never needs this: `GraphicsDevice::UpdateViewportFromWindow()` already resets `ScissorRectangle` to the full new backbuffer on any detected resize, for every backend, before a game could observe stale insets either way (HTMLDOM-93). |
| `GraphicsDevice.Viewport` (`SetViewport`) | ✅ | Confirmed non-gap against every 2D-only sibling (`SDL_RENDERER`/`CANVAS`/`DX3` all leave it as the same inherited no-op) — but `EASYGL` genuinely supports a real GL sub-region `Viewport` (split-screen/sub-panel rendering), and nothing about DOM/CSS compositing rules that out here. **Implemented per-batch (HTMLDOM-107, correcting an earlier per-root-resize approach)**: `#cna-dom-root` always stays at the full backbuffer's own size/position; the active viewport's own `(X,Y)` offset is instead captured once per `SpriteBatch` flush and applied as the outermost translate on that batch's own sprites — on both the DOM `<div>` path and the Canvas2D render-target path (previously ignored there entirely). This means a viewport set later in the same frame can never retroactively move sprites an earlier batch already flushed under a different viewport — the real split-screen/sub-panel case. Scissor-region clip-path insets (below) need no viewport-offset adjustment at all any more, since root never moves. Verified in a real browser: root's own box is provably unaffected by viewport changes; a sprite's CSS transform carries the viewport offset as an added (not substituted) prefix; an earlier batch's sprite keeps its own offset after a later batch sets a different viewport; and a `RenderTarget2D`-bound draw under a sub-rectangle viewport reads back at the correct offset pixel position. |
| `ApplyDepthStencilState` / `SetBlendFactor` / `SetReferenceStencil` | ✅ confirmed inert | All three are inherited no-ops, and all three are provably safe to leave that way: depth/stencil state is meaningless since `SupportsDepthStencil()` is always `false`; `SetBlendFactor`'s colour can only matter for a `Blend.BlendFactor` combination, which `ApplyBlendState` already rejects before this backend could ever read it. GTest-audited with non-default values, not just assumed. |
| `ApplyRasterizerState` | ✅ | `cullMode`/`fillMode`/depth-bias fields have no 2D analogue and stay genuinely inert (GTest-audited). `scissorTestEnable` (HTMLDOM-102) is a real override now, not inherited: it gates whether `SetScissorRect`'s recorded rectangle clips anything at all — see the `ScissorRectangle` row above. |

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
frame, tint changing every frame with STATIC position (HTMLDOM-111 correction: despite this
workload's own older "moving sprites" framing, its position formula never actually depends on the
frame number — only tint does, so this is this backend's own documented heavy-tint-churn WORST
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

**Slow here:** uploading texture pixels (PNG encode per upload); animating a sprite's tint RGB every
frame (a new cached variant per distinct colour, LRU-capped at 256); render-target-heavy frames
(those fall back to Canvas2D rasterization); very high sprite counts, where per-element compositor
layers cost more memory than a single canvas would.

Rule of thumb: this backend rewards static sprite sheets and moving sprites, and punishes dynamic
pixel data.

**Comparative benchmark** (`examples/graphics_backend_benchmark.cpp`, HTMLDOM-92): the same
500-sprite/frame workload, built and run under all three Emscripten-capable backends from three
separate `emcmake` configures and measured in the same headless-Chromium harness. **HTMLDOM-111
correction, superseding the table this section used to publish**: the earlier ms/frame numbers here
were submission time only (`SpriteBatch::End()` alone — excludes `Present()` and every deferred
browser layout/paint/composite cost), which structurally favours DOM style writes (cheap, deferred)
over Canvas2D/WebGL work (closer to synchronous) and cannot support an honest cross-backend "X is
faster" claim. `graphics_backend_benchmark.cpp` was reworked to report BOTH a submission number
(explicitly labelled as not a frame rate) and a real end-to-end one — the wall-clock gap between
successive `requestAnimationFrame`-paced `Draw()` calls, which can only elapse once the browser
actually finished rendering the previous frame — across TWO workloads (stable tint: position
animates, tint fixed, this backend's own documented sweet spot; heavy churn: both animate, this
backend's own documented weak point; the OLD single workload measured only the churn case, despite
being labelled "steady state"). `HTML_DOM`'s own real end-to-end numbers in this container, 500
sprites: **stable tint ~17 ms/frame (~59 real fps)**, **heavy churn ~19 ms/frame (~53 real fps)** —
both close to this machine's own ~60fps vsync floor, meaning the browser's own display cadence, not
either backend's CPU cost, is the dominant factor at this sprite count. A genuine, re-verified
side-by-side `HTML_DOM`/`CANVAS`/`EASYGL` table under this corrected methodology has not been
re-run as of this correction (the old table's absolute "HTML_DOM 6-7x faster than CANVAS" framing
should not be treated as confirmed under the new, honest methodology until it is) — re-run
`graphics_backend_benchmark.cpp` per backend for a current comparison rather than reusing
the old table. The old run's own real, useful finding stands independent of the metric used: this
container has no real GPU (`EASYGL`'s own `WEBGL_debug_renderer_info` query, also added by
HTMLDOM-111, confirms `SwiftShader` software rendering here), so a hardware WebGL2 context would
likely change `EASYGL`'s own relative standing and must never be silently compared against this
software one without saying so.

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
   value is simply ignored by the engine before any CNA code can observe it. The fallback path
   itself cannot be exercised by an automated CI browser (HTMLDOM-113) — what IS tested is the
   assumption every other Additive test in this suite depends on: `htmldom_smoke_test.cpp` confirms
   via `CSS.supports('mix-blend-mode', 'plus-lighter')` that the CI browser genuinely supports it,
   so those tests are exercising the real blend, not this silent degradation.
10. **`SetScissorRect` correctness is per-`SpriteBatch` batch, not per individual `Draw()` call**
    (HTMLDOM-94) — matching real XNA/FNA `SpriteBatch.Deferred` semantics, not a shortfall specific
    to this backend. Cross-region paint order (HTMLDOM-103) is a real per-flush `z-index`, matching
    the actual draw sequence across regions, not merely each region's own creation order.
11. **`NonPremultiplied`/`Additive` alpha channel diverges from raw XNA/D3D hardware blending for a
    TRANSLUCENT source** (HTMLDOM-105) — both presets use `alphaSrcBlend=SourceAlpha`, so their real
    per-factor alpha equation squares the source alpha's own contribution; CSS's compositing model
    has no per-channel blend-factor customization to reproduce that with. Colour channels are always
    exact; the alpha channel is exact only when the source is fully opaque (where the squared and
    unsquared formulas coincide).
12. **Effectively one live, actively-driven `GraphicsDevice` per process** (HTMLDOM-114) — not a
    choice this backend makes, but a real constraint of `emscripten_set_main_loop`'s own
    `simulate_infinite_loop` contract, which `Game::Run()` uses: it never returns to its caller, so
    two independently-driven `Game::Run()` loops cannot coexist in one process. A SECOND
    `HtmlDomGraphicsBackend` sharing the same window CAN still be constructed while a first is alive
    (nothing in `IGraphicsBackend` prevents it, and this is now safe rather than forbidden) — the
    shared DOM surface is reference-counted (`Module['cnaDomBackendRefCount']`), so destroying one
    backend never tears the surface down out from under another still-live one. Verified in-browser.
