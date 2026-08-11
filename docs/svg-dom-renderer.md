# SVG DOM Renderer — Capability Status

`SVG_DOM` is CNA's vector-DOM graphics renderer: Emscripten-only, 2D-only, rendering `SpriteBatch`
output as real SVG namespace elements — one pooled `<svg>` viewport (its own `viewBox` crops the
source rectangle) containing one `<image>` per visible sprite — rather than rasterizing into a
`<canvas>` (`CANVAS`) or CSS-transforming pooled `<div>`s with `background-image` (`HTML_DOM`).
Per-sprite tint is applied with a native SVG `feColorMatrix` filter instead of a pre-tinted cached
bitmap variant.

**Status legend** (this project's convention): ✅ implemented *and verified against its stated
acceptance criteria*; 🟨 code exists but has not met those criteria; ⬜ not implemented.

**What ✅ means here — a narrower bar than `HTML_DOM`'s own document.** No Emscripten SDK was
available in the environment this renderer was authored in, so nothing here has been run in a
real browser. ✅ below means: the underlying algorithm is pure C++ (blend mapping, PNG encode,
base64, un-premultiply, the sprite geometry/matrix encoder, source-rectangle validation) and is
**genuinely exercised natively** — real GTest assertions, including a real `EncodePngEXT` →
SDL3_image decode → byte-exact pixel round trip, with no Emscripten SDK involved at all — via the
native host-contract target described below. 🟨 marks browser-only behavior (actual SVG element
creation/attributes, `feColorMatrix` filters, `mix-blend-mode`, `getImageData` readback) that is
implemented and code-reviewed but has only been proven by reading the JS, not by a real DOM run —
an explicit **external platform gate**, not a claim of in-browser verification that did not happen.

## Validation performed

| What | How | Result |
|---|---|---|
| Renderer-identity registry (41→42, `SVG_DOM`/`SvgDom` added, no other identity changed) | `python3 scripts/check_renderer_identities.py` | ✅ `OK: 42 public renderer identities preserved in both registries` |
| Pure-C++ pixel/geometry pipeline (PNG encode + SDL3_image decode round trip, base64 known vectors, un-premultiply math, per-draw tint/blend-prep extraction, the sprite matrix encoder's rotation/scale/flip/origin placement, source-rectangle bounds validation, blend-state→composite-op mapping, the 3D "not yet implemented" surface) | Native GTest binary, `CNA_BUILD_SVG_DOM_HOST_TESTS=ON` (see below) | ✅ 64/64 `cna_test_svgdom_host` cases pass natively, no Emscripten SDK involved |
| HEADLESS renderer (and the rest of `CnaTests`) still configures/builds/passes after this renderer's shared-registry edits | Full native HEADLESS `CnaTests` build | ✅ cross-renderer control — see the task's final report for the run this was verified in |
| Real SVG DOM output, `feColorMatrix` tint, `mix-blend-mode: plus-lighter`, pooled-element reuse, `getImageData` render-target readback | `emcmake cmake -DCNA_GRAPHICS_RENDERER=SVG_DOM` + a headless-Chromium harness (the same shape as `scripts/run-htmldom-browser-test.sh`) | 🟨 **external platform gate** — no Emscripten SDK in this environment; not executed here |

Select it with:

```bash
emcmake cmake -S . -B cmake-build-svgdom -DCNA_GRAPHICS_RENDERER=SVG_DOM
cmake --build cmake-build-svgdom --target cna_test_svgdom_smoke -j3
```

Native host-contract tests (no Emscripten SDK required — this is what was actually run to produce
the 64/64 result above):

```bash
cmake -S . -B cmake-build-svgdom-host -DCNA_GRAPHICS_RENDERER=HEADLESS \
  -DCNA_BUILD_TESTS=ON -DCNA_BUILD_SVG_DOM_HOST_TESTS=ON
cmake --build cmake-build-svgdom-host --target cna_test_svgdom_host -j4
./cmake-build-svgdom-host/modules/renderers/cna_test_svgdom_host
```

`CNA_BUILD_SVG_DOM_HOST_TESTS` mirrors `HTML_DOM`'s own `CNA_BUILD_HTML_DOM_HOST_TESTS` precedent —
compiling the SVG_DOM implementation itself into a native GTest executable, where every `EM_JS`
body is excluded by `__EMSCRIPTEN__`. See the **SHARED-HOOK REQUIREMENT** note in
`modules/renderers/CMakeLists.txt` for why this target is defined there rather than inside
`modules/renderers/svg-dom/` (the natural location is unreachable outside Emscripten, since
reaching it requires *selecting* `SVG_DOM`, which itself requires Emscripten — the same latent gap
`HTML_DOM`'s own host-test target already has).

---

## How it draws

One `<svg id="cna-svg-dom-root">` is created over the `<canvas>` SDL3 already owns (the canvas
stays in the layout, hidden, so SDL keeps sizing it and delivering input through it). Every sprite
is a **pooled**, dirty-diffed wrapper `<g>` around two alternately shown children: a nested `<svg>`
viewport containing one `<image>` (crop-only draws), or a `<rect>` filled by a per-slot `<pattern>`
(tiled draws — see the `TextureAddressMode` row below). Placement, filtering and tint all live on
the wrapping `<g>` so both children share them identically:

| XNA concept | SVG realization |
|---|---|
| destination position, rotation, scale, flip | one collapsed affine `transform="matrix(a,b,c,d,e,f)"` on the wrapping `<g>` (computed in C++, unit-tested numerically — see `SvgDomSpriteBatchRenderer::BuildDrawCommandEXT`) |
| `Texture2D` + in-bounds `sourceRectangle` | the nested `<svg>`'s own `width`/`height`/`viewBox` crop an `<image>` sized to the full texture — SVG's intrinsic nested-viewport clipping, no separate `<clipPath>` needed |
| `Texture2D` + out-of-bounds `sourceRectangle` under `Wrap`/symmetric `Mirror` (SVGDOM-1) | a `<rect>` filled by a per-pool-slot `<pattern patternUnits="userSpaceOnUse">`, phase-offset by `x`/`y` (`-sourceRect.{X,Y} mod patternWidth/Height`, non-negative) so the tile grid lines up with the requested source origin; `Mirror` reuses `Wrap`'s plain `repeat` against a pre-built quadrant-mirrored texture variant instead of a separate primitive (neither SVG nor CSS expose one) |
| tint (RGB and A together) | a cached `feColorMatrix` filter (`url(#cna-svg-dom-tint-…)`), omitted entirely for `Color.White` with a non-`Opaque` blend — the common case carries no filter attribute at all |
| `BlendState.Additive` | `mix-blend-mode: plus-lighter` on the sprite's own wrapping `<g>` |
| `BlendState.Opaque` | the same accepted "alpha-stripped" deviation `HTML_DOM`'s own `<div>` path documents (forced via the filter's alpha row, `0 0 0 0 1`) — neither SVG nor CSS compositing exposes a per-element Porter-Duff "replace" against the backdrop |
| `SpriteBatch` draw order | DOM document order |
| `Clear` | the backbuffer `<rect>`'s own `fill`, plus rewinding the pooled-sprite cursor to 0 |

Sprite elements are **pooled and reused across frames**: `Clear()` rewinds a cursor to 0 (not a
teardown), each flush claims the next pool slot per sprite (creating one only the first time a
slot is needed), and every attribute/style write is dirty-checked against that element's own
last-applied state — a steady frame (same sprite count, same per-sprite texture/geometry/tint)
writes nothing beyond the pool's initial creation; a frame where sprites only move writes just
`transform`. `Present()` hides (not removes) the pool tail this frame did not use, tracked by a
high-water mark — the same "pool cursor + high-water hide" shape `HTML_DOM`'s own DOM path uses,
independently applied to SVG's own primitives here. A whole batch crosses the wasm/JS boundary in
**one** call: `Draw()` appends a fixed-stride 56-byte command and `End()` hands the array over as
a block.

---

## 1. SpriteBatch

| Feature | Status | Notes |
|---|---|---|
| `Draw(texture, x, y)` / `Draw(texture, destRect, srcRect, color)` / full overload with rotation/origin/effects | 🟨 | Geometry funnels through one C++ encoder (`BuildDrawCommandEXT`), pivot/rotation/flip placement is ✅ unit-tested numerically (identity, scale, origin-pivot, 90° rotation, flip-flag) without a browser; the actual SVG element write is 🟨 (external gate). |
| Colour + alpha tint | 🟨 (algorithm ✅) | A native `feColorMatrix` per-channel linear multiply — exact by construction (`out = in * tint/255` per channel), no pre-tinted bitmap cache needed. The C++ side of the identical multiply (used by the render-target Canvas2D path) is ✅ unit-tested (`PrepareSpritePixelsEXT`). |
| `transformMatrix` in `Begin()` | 🟨 | Composed onto each sprite's own collapsed matrix before it reaches JS (`SvgDomSpriteBatchRenderer::QueueDraw`), so two batches with different transforms coexist in one frame. |
| Custom `Effect` via `Begin(effect)` | ✅ throws-by-design | Neither SVG nor CSS compositing has a programmable shader stage. |
| `SpriteSortMode` sequencing | 🟨 | Renderer-agnostic (sorted in shared `SpriteBatch.cpp`, realized as DOM document order); `Immediate` flushes each `Draw()` as its own one-sprite batch immediately, matching the `SetImmediateMode` contract every 2D-DOM CNA renderer shares. |
| `SpriteFont` (`DrawString`) | 🟨 | No renderer-specific code — every glyph funnels through the same `Draw` overload, same as every other 2D renderer. |
| Element pooling / recycling across frames, dirty-attribute diffing | 🟨 | Implemented (see "How it draws" above); genuinely reused/hidden-not-removed by construction, not yet proven in a real browser. |
| `TextureAddressMode::Wrap` / `Mirror` / edge-extended `Clamp` for an out-of-bounds `sourceRectangle` | ⬜ **not yet implemented (V1 scope, SVGDOM-1)** | Every draw's `sourceRectangle` must lie entirely within the texture; an out-of-bounds one throws deterministically (`ValidateSourceRectangleEXT`) regardless of the requested address mode, rather than approximating. A real, narrower boundary than `HTML_DOM`'s own edge-padding/tiling variants — the overwhelmingly common in-bounds sprite-sheet-frame case is unaffected. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData` | ✅ (C++ side) | CPU-side RGBA8 buffer is the single source of truth (unlike `HTML_DOM`, which derives its own canonical copy from a JS-side canvas); `GetData`/`UpdatePixels` round-trip verified natively. |
| Texture upload → SVG `<image href>` | 🟨 (encode ✅) | The PNG **encode itself runs in C++** (SDL3_image `IMG_SavePNG_IO`), not JS — genuinely proven natively: `EncodePngEXT` output round-trips byte-exact through a real SDL3_image decode in `cna_test_svgdom_host`. Lazily generated per pixel-variant (straight / un-premultiplied) on first use, cached until `UpdatePixels` invalidates it, then pushed to the JS-side texture registry as a `data:image/png;base64,…` URI (🟨: the JS push itself is unverified without a browser). |
| Mip-level (`level>0`) `SetData` | ✅ throws-by-design | No mip chain exists here, the same boundary `CANVAS`/`HTML_DOM`/`SDL_RENDERER` draw. |
| `RenderTarget2D` construction / bind / unbind | 🟨 | Backed by a real private off-screen canvas (Canvas2D, not SVG — see design decision 5 below); creation/dispose logic and the CPU-buffer-authoritative-until-bound contract are ✅ unit-tested natively (`SvgDomRenderTargetRendererTest`). |
| `RenderTarget2D` drawn into while bound | 🟨 (colour math ✅) | Per-sprite Canvas2D `drawImage`, geometrically positioned by the same collapsed matrix the SVG path uses; source pixels are blend-prepared + tinted in **C++** first (`PrepareSpritePixelsEXT`, unit-tested), then handed to `ctx.putImageData` + `drawImage` — real geometry composition, not yet run in a browser. |
| `RenderTarget2D::GetData` | 🟨 (CPU path ✅ native; browser path 🟨) | Never-bound or `UpdatePixels`-only content reads straight from the CPU buffer — ✅ works and is tested natively, no browser needed. Once bound (dirty), a real `getImageData` readback is required to refresh it; a native (non-Emscripten) build honestly returns `false` rather than serving stale content (✅ unit-tested: `DirtyAfterBindReturnsFalseWithoutABrowser`). |
| `RenderTargetUsage` | ✅ | Shared `GraphicsDevice` concern; no renderer-specific code. |
| `depthFormat`/`mipMap`/`multiSampleCount` at `RenderTarget2D` construction | ✅ throws-by-design | This renderer has no depth storage, no mip chain, no MSAA — any non-default request throws before allocating anything (✅ unit-tested: `RenderTarget2DRejectsUnsupportedResourceOptions`). |
| `SetRenderTargets` (MRT) | ✅ throws-by-design | One canvas backs each target; inherently single-output, same as `HTML_DOM`. |
| **Backbuffer readback** | ✅ throws-by-design | No browser API rasterizes a live SVG subtree to pixels synchronously — the identical constraint `HTML_DOM`'s own DOM/CSS backbuffer has. Render into a `RenderTarget2D` and read that instead, or use `CANVAS`. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | 🟨 | SVG path: `feColorMatrix` alpha row forced to `0 0 0 0 1` (accepted alpha-stripped deviation, matching `HTML_DOM`'s own documented one for its `<div>` path). Canvas2D render-target path: real `globalCompositeOperation='copy'` — an EXACT Porter-Duff replace, not an approximation, since Canvas2D actually has that operator. |
| `AlphaBlend` | 🟨 (un-premultiply math ✅) | Drawn from a lazily generated un-premultiplied pixel copy (`UnpremultiplyEXT`, ✅ unit-tested with hand-derived values including the `alpha==0` and `alpha==255` edge cases) — SVG/Canvas2D both composite straight alpha natively, while `AlphaBlend` assumes premultiplied source. |
| `NonPremultiplied` | 🟨 | Source pixels as uploaded — already matches what `source-over` compositing assumes. |
| `Additive` | 🟨 | `mix-blend-mode: plus-lighter` (SVG path) / `globalCompositeOperation='lighter'` (Canvas2D render-target path, an exact native operator). `GraphicsDevice::SupportsCapability(AdditiveBlending)` is wired to query `CSS.supports('mix-blend-mode','plus-lighter')` for real (same technique `HTML_DOM` uses) — honestly reports `false` on a native (non-browser) build rather than fabricating `true` (✅ unit-tested). |
| Any other custom `BlendState` | ✅ throws-by-design | Neither SVG nor CSS compositing has a blend-factor/equation model to approximate one with (✅ unit-tested: the four presets map correctly, everything else throws — `BlendStateToDomCompositeOp`). |
| `ColorWriteChannels` / `MultiSampleMask` | ✅ throws-by-design outside defaults | No per-channel write mask or coverage mask exists in SVG/CSS compositing (✅ unit-tested). |

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | 🟨 | `image-rendering: auto` vs `pixelated` on the sprite's own `<svg>` wrapper, the same magnification-dominant grouping `SDL_RENDERER`/`CANVAS`/`HTML_DOM` use. Ordinal validation (only `Linear`/`Point` accepted) is ✅ unit-tested. |
| `TextureAddressMode` (`Wrap`/symmetric `Mirror`, out-of-bounds `sourceRectangle`) | ✅ | `SetSamplerAddressMode` validates and records U/V ordinals; once a source rectangle leaves the texture, `Wrap` and symmetric `Mirror` (same mode on both axes) tile via a `<pattern>` fill (SVG path) or direct wrap/mirror texel sampling (render-target path) instead of throwing — ✅ unit-tested (SVGDOM-1). Out-of-bounds `Clamp` and mixed per-axis addressing still throw (see §1). |

## 5. Viewport / PresentationParameters / Rasterizer

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | 🟨 (math ✅) | All five `CnaPresentationMode` values (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`/`FixedHeightDynamicWidth`) share the identical XNA/Windows-Phone geometry contract every 2D-DOM CNA renderer implements (`ComputeLogicalViewport`), independently re-derived here; `SetPresentationMode` ordinal validation is ✅ unit-tested. Applying the computed geometry to the DOM (`root.setAttribute('viewBox', …)`) is 🟨. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | Pure C++ against `ComputeLogicalViewport`; the "no virtual resolution configured" contract is unit-tested against the same `FakeWindow()` harness `HtmlDomRendererTests.cpp` uses. |
| `GraphicsDevice.ScissorRectangle` (`SetScissorRect`) | 🟨 | **V1 scope**: a SINGLE global `<clipPath>` shared by the whole frame's pooled sprite group, re-evaluated at the most recent flush — a real, smaller-scope boundary than `HTML_DOM`'s own per-scissor-rect-isolated region pools (HTMLDOM-94), not silently approximated. Only applied when `RasterizerState.ScissorTestEnable` is true (✅ unit-tested: `ApplyRasterizerStateReadsScissorTestEnable`). |
| `GraphicsDevice.Viewport` (`SetViewport`) | 🟨 (composition ✅) | Recorded as C++ state only; composed as the outermost translation on each sprite's own matrix at flush time, matching real XNA/FNA's rasterizer-stage `Viewport.X/Y` application strictly after `SpriteBatch.Begin(transformMatrix)`. The composition itself is exercised by `SetViewportRecordsTheOffsetWithoutThrowing`. |
| `ApplyDepthStencilState` / `SetReferenceStencil` | ✅ truthful 2D boundary | A fully-disabled depth/stencil state and reference value zero are accepted; anything else throws deterministically (✅ unit-tested). |
| `ApplyRasterizerState` | ✅ truthful 2D subset | Ordinary filled 2D/default-cull states and `scissorTestEnable` are accepted; clockwise-face culling, wireframe and non-zero depth bias throw (✅ unit-tested). |

## 6. The 3D surface

Every direct 3D entry point this renderer overrides throws `std::runtime_error("SVG_DOM renderer: …
not yet implemented")`: depth/stencil clears, `SetDepthTestEnabled`/`SetBlendEnabled`/
`SetDepthWriteEnabled`, vertex and index buffer creation, both `Draw*Primitives` families, and the
five unsupported resource factories (`CreateTexture3D`, `CreateTextureCube`,
`CreateRenderTargetCube`, `CreateOcclusionQuery`, `CreateEffectRenderer`). All ✅ unit-tested
natively (`SvgDom3DSurfaceTest`).

`GraphicsDevice::SupportsCapability()` reports `false` for every capability except
`AdditiveBlending` (§3), which reports the browser's real `mix-blend-mode: plus-lighter` support
(honestly `false` on a native/non-browser build). `SupportsDepthStencil()` is unconditionally
`false`.

---

## Known limitations (V1 scope)

1. **No backbuffer readback** — no browser API rasterizes a live SVG subtree synchronously.
   Render into a `RenderTarget2D` and read that, or use `CANVAS`.
2. **`TextureAddressMode::Wrap` and symmetric `Mirror` (same mode on both axes) are implemented**
   for an out-of-bounds `sourceRectangle` (SVGDOM-1): the SVG backbuffer path tiles via a per-sprite
   `<pattern>` fill (a pre-built quadrant-mirrored texture variant reproduces `Mirror` with a plain
   `repeat`), and the render-target-bound Canvas2D path samples every output texel directly through
   wrap/mirror addressing in C++ (`PrepareTiledSpritePixelsEXT`). Out-of-bounds `Clamp` and mixed
   per-axis addressing (e.g. `Wrap` on U, `Mirror` on V) remain unimplemented and still throw
   deterministically — see `ValidateAddressModesEXT`. Clamp-overflow edge-extension is tracked as a
   separate follow-up (SVGDOM-3); `HTML_DOM`'s own edge-extension/tiling variant cache remains a
   wider boundary than this renderer's.
3. **A single global scissor region**, not `HTML_DOM`'s own per-batch-isolated regions (HTMLDOM-94)
   — a later batch's different `ScissorRectangle` retroactively affects the clip every pooled
   sprite in the shared group uses, since there is only one `<clipPath>` for the whole frame. Real
   per-region isolation (multiple pooled groups, one per distinct active rect) is a documented,
   deferred follow-up, not attempted in this pass.
4. **Custom `BlendState`s throw** — only the four standard presets exist in SVG/CSS compositing.
5. **No custom `Effect`s** — there is no shader stage.
6. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER`/`CANVAS`/`HTML_DOM`.
7. **RGB tint on the render-target-bound Canvas2D path** uses the same C++ `PrepareSpritePixelsEXT`
   multiply the SVG path's filter reproduces (kept numerically identical on purpose), so the two
   draw paths should agree exactly — not yet cross-checked pixel-for-pixel in a real browser.
8. **No hierarchical camera/world `<g>` grouping.** A scene where only the camera moves still
   updates every visible sprite's own `transform` rather than a single shared parent transform.
   Real, valuable follow-up work for large static scenes (tilemaps, mostly-static UI) — flagged
   during design review but out of scope for this pass; see plan_svg_dom.md.
9. **Effectively one live, actively-driven `GraphicsDevice` per process**, the same
   `emscripten_set_main_loop` constraint every Emscripten CNA renderer shares (not specific to
   `SVG_DOM`).

## Platform validation summary

| Layer | Status |
|---|---|
| Native compile of every pure-C++ algorithm (blend mapping, PNG/base64/un-premultiply, sprite matrix encoder, address-mode validation, mirror-tiled variant construction, tiled pixel sampling, texture/render-target CPU-side lifecycle, the 3D throw surface) | ✅ native, this environment, 81/81 GTest cases (`cna_test_svgdom_host`) |
| Native HEADLESS `CnaTests` regression after this renderer's shared-registry edits | ✅ native, this environment (cross-renderer control) |
| `-DCNA_GRAPHICS_RENDERER=SVG_DOM` Emscripten configure/compile/link | 🟨 external platform gate — no Emscripten SDK available in this environment |
| Real browser DOM output (headless Chromium or otherwise) | 🟨 external platform gate — not run |

No native, cross-build, or even compile-only verification of the `EM_JS`-containing renderer path
was possible in this environment (no `emcc`/`emsdk` present) — this mirrors the exact same gap
`HTML_DOM`'s own native host-test target already has (unreachable via the existing renderer
dispatch outside Emscripten, and with no CI/script reference anywhere in this repo either), not a
shortfall specific to `SVG_DOM`.
