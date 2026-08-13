# SVG DOM Renderer — Capability Status

`SVG_DOM` is CNA's vector-DOM graphics renderer: Emscripten-only, 2D-only, rendering `SpriteBatch`
output as real SVG namespace elements — one `<svg id="cna-svg-dom-root">` surface, containing one
document-ordered `<g>` "flush slot" per distinct clip state a frame actually used, each holding one
pooled `<g>` per visible sprite (a nested `<svg>` cropping an `<image>`, or a `<pattern>`-filled
`<rect>` for tiled draws) — rather than rasterizing into a `<canvas>` (`CANVAS`) or CSS-transforming
pooled `<div>`s with `background-image` (`HTML_DOM`). Per-sprite RGB tint is applied with a native
SVG `feColorMatrix` filter instead of a pre-tinted cached bitmap variant; alpha is applied through
the sprite group's `opacity`, so alpha-only animation stays off the SVG-filter path.

**Status legend** (this project's convention, refined for this remediation pass):

- ✅ **verified natively** — a pure-C++ algorithm/state machine exercised by real, passing GTest
  assertions in `cna_test_svgdom_host`, with no DOM/browser involved.
- 🌐 **verified in browser** — actually executed in a real, running headless Chromium instance via
  Playwright and confirmed either structurally (real DOM inspection) or by pixel (a real
  `RenderTarget2D::GetData()` readback or an actual page screenshot). Nothing in this document
  carries this mark unless a browser genuinely ran it in this remediation pass.
- ⛔ **intentionally unsupported** — a real capability boundary (SVG/CSS compositing has no
  equivalent primitive), documented and unit-tested as a deterministic throw.
- ⬜ **not implemented** — a real, acknowledged gap.

A status here is never upgraded on the strength of the *old* document's own claims. Every row below
was re-derived from the current implementation, the shared `GraphicsDevice`/`SpriteBatch` contracts,
this project's own cross-renderer tests (e.g. `spritebatch_custom_viewport_test.cpp`), and — where
browser behaviour matters — actual execution.

---

## What changed in this remediation pass

A systematic audit (native source review + a real Emscripten/Playwright browser environment,
neither of which had been used against this renderer before) found and fixed the following defects
at their root cause. Each is referenced by its investigation code below.

| ID | Defect | Root cause | Fix |
|---|---|---|---|
| SVGDOM-A | Cross-region scissor/clip paint order followed each clip-rect's own first-DOM-creation order, not actual `SpriteBatch` flush order — a batch drawn 4th could wrongly stay under a batch drawn 1st. | The `'full'` (unscissored) container was created once, eagerly, at renderer construction, permanently first in DOM order regardless of when it was actually drawn to; named-rect regions were appended later and so were *always* document-order-after it. | Replaced clip-identity-keyed regions with **per-flush ordered DOM slots**: every flush claims the next array slot, appended to `root` only the first time that slot index is used, never reordered afterward — DOM order is `SpriteBatch` flush order by construction. Consecutive flushes sharing the same effective clip state coalesce into one slot (same pooling economics as before). |
| SVGDOM-B | The render-target-bound Canvas2D draw path never consulted `RasterizerState.ScissorTestEnable`/`GraphicsDevice.ScissorRectangle` at all. | `CNA_SvgDom_DrawSpriteToTarget` had no scissor parameters and no clip bracket. | Effective clip (see SVGDOM-F) is now captured once per flush and applied via a `ctx.save()/rect()/clip()/…/restore()` bracket around the whole render-target-bound flush, matching the SVG path's own batch-level timing. |
| SVGDOM-C | `cnaSvgDomLogicalW/H` stayed `0` until the first `Present()`, so a scissor rect on the very first draw (before any `Present()`) was misclassified as covering a 0×0 surface and collapsed to "no clip". | Geometry was only pushed to JS from `Present()`. | `SvgDomRenderer` now calls `ApplySurfaceGeometryEXT()` from its constructor and from `SetVirtualResolution`/`SetPresentationMode`, not only `Present()`. |
| SVGDOM-D | A `RenderTarget2D::SetData` call updated only the C++ CPU buffer, never the JS-side canvas; a canvas-composited draw made after `SetData` could composite against stale pre-`SetData` pixels. | `SvgDomRenderTargetRenderer::UpdatePixels` never touched `Module['cnaSvgDomTextures'][id].canvas`. | `UpdatePixels` now pushes the new pixels into the canvas immediately (`CNA_SvgDom_WriteTargetPixels`), and `EnsureFreshEXT()` always re-reads the canvas while the target is the one *currently bound* (not only when a `dirty_` flag says so), so repeated modify/sample cycles on the same target can never observe stale data regardless of call order. |
| SVGDOM-E | Drawing a `RenderTarget2D` as an ordinary `SpriteBatch.Draw()` source texture was undefined behaviour in **every** build (not only Emscripten): `SvgDomSpriteBatchRenderer::QueueDraw`/`Flush` used `static_cast<const SvgDomTextureRenderer&>(texture)`, silently reinterpreting a `SvgDomRenderTargetRenderer`'s memory (a sibling class, not a subclass) as the wrong concrete type. On the SVG backbuffer path this additionally crashed in JS: `CNA_SvgDom_RegisterTextureVariant` assumed a pre-existing registry entry always had `variants`/`variantDims`, which a render target's own `{canvas, ctx, isRenderTarget}` entry never had. | Two independent bugs stacked on the same feature: an unsafe C++ downcast, and a JS registry shape mismatch. | Replaced the `static_cast` with the `dynamic_cast`-based dispatch pattern `HtmlDom` already uses for the identical sibling-class shape (`AsRenderTargetEXT`/`CanvasIdOfEXT`/`PixelsOfEXT`/`DataUriOfEXT`). Made `CNA_SvgDom_RegisterTextureVariant` additively extend whatever entry shape already exists instead of assuming one. Added `SvgDomRenderTargetRenderer::EnsureFreshEXT()`-gated `GetPixelsEXT()`/`GetDataUriEXT()` passthroughs so sampling a render target as a texture is never stale. |
| SVGDOM-F | `SvgDomRenderer::SetViewport` discarded `Width`/`Height`, keeping only the `(X,Y)` translation — violating this project's own cross-renderer `SpriteBatch` contract (`spritebatch_custom_viewport_test.cpp`), which requires a smaller `Viewport` to clip rendering to its own rectangle unconditionally, independent of `RasterizerState.ScissorTestEnable`. | `Width`/`Height` parameters were explicitly discarded (`(void)w; (void)h;`). | `SvgDomState::SetCurrentViewportRectEXT` now records the full rectangle; `ComputeEffectiveClipRectEXT` intersects it with any active scissor rect into one effective clip, applied identically on both the SVG backbuffer and render-target-bound paths. |
| SVGDOM-5 | A real Mobile Eggbert run loaded but ignored input and made Firefox/the desktop severely sluggish, especially during the rotating alpha-fade loading animation. | `visibility:hidden` removed SDL's canvas from pointer hit testing while the SVG overlay accepted events. Separately, XNA's premultiplied AlphaBlend draw colour was treated as straight RGB+A inside one `feColorMatrix`: every alpha step created another cached SVG filter and darkened the fade twice. `Present()` also reassigned unchanged root geometry every frame, invalidating a large SVG tree. | The SDL canvas now remains hit-testable at `opacity:0`; the SVG root uses `pointer-events:none`. AlphaBlend tint RGB is un-premultiplied once, alpha uses dirty-diffed group `opacity`, and RGB-only filters no longer vary with alpha. Clear colour and surface geometry writes are dirty-diffed, so steady frames do not repeatedly mutate the root. |
| SVGDOM-6 | The game rendered and keyboard input worked, but clicking the visible game did not reach its controls in the stock Emscripten page. | The absolute SVG root used only the presentation-mode offset from the SDL window origin. It did not include the canvas's position in the surrounding HTML page, so the visible SVG and SDL's transparent pointer-event canvas occupied different page rectangles. | Surface geometry now adds the canvas's `offsetLeft`/`offsetTop` before the renderer-local viewport offset and dirty-diffs the combined value each frame. The SVG is therefore painted directly over the exact canvas on which SDL registered pointer handlers, including after ordinary host-page reflow. |
| — | `SvgDomTextureRenderer`'s destructor was `= default` and never deleted its JS registry entry, despite its own header doc claiming it did — a genuine, unbounded memory leak (cached base64 PNG strings) for every destroyed `Texture2D`. | Missing `CNA_SvgDom_DestroyTexture` call. | Added the JS deletion call, mirroring the render-target path's own `CNA_SvgDom_DestroyTargetCanvas`. |
| — | `GetData` bounds checks (`x + w > width_`, `dataLength < w * h * 4`) used plain 32-bit `int` arithmetic that can overflow on caller-controlled input, letting an out-of-bounds region slip past validation — on Emscripten/wasm32 specifically, `int` and `size_t` are both 32-bit, so this is not merely a native-build styling concern. | No overflow-safe arithmetic. | Bounds checks now compute in `int64_t`; texture construction also rejects a `width*height*4` byte count that would not fit a 32-bit `int`, since this renderer's own buffer-size/length surface is `int` throughout. |
| — | **(Cross-renderer, not SVG_DOM-specific)** `GameWindow::queryClientBoundsFromSDL()` threw whenever `SDL_GetWindowSize` failed, including a real Emscripten startup race: SDL3's Emscripten video backend can report "not initialized" for the first one or two event-loop ticks after `SDL_CreateWindow()` already returned a valid window. | The browser-side canvas/context setup `SDL_GetWindowSize` depends on finishes asynchronously, strictly after control has already returned to C++. | Falls back to the last-known bounds for that one tick instead of throwing (`modules/runtime/src/GameWindow.cpp`) — found and fixed while investigating why no CNA Emscripten renderer had ever completed a real browser run before (reproduces identically for `HTML_DOM`). |
| — | **(Cross-renderer, not SVG_DOM-specific)** A follow-up pass found and fixed the actual blocker mentioned above: `Game::BeginDraw()` crashed under Emscripten because every CNA example (including this renderer's own) stack-allocated its `Game` subclass in `main()` — `emscripten_set_main_loop(..., simulateInfiniteLoop=1)`'s JS-level unwind prematurely runs that object's destructor (a proven, wasm-native-exception-handling interaction, not a `GameServiceContainer` defect), deleting the real `GraphicsDeviceManager` through its owning `unique_ptr` and leaving `Game::graphicsDeviceManager_` dangling. | Stack-local `Game` object lifetime versus `emscripten_set_main_loop`'s unwind-via-throw mechanism. | Every SVG_DOM/HTML_DOM/CANVAS example `main()` now heap-allocates its `Game` subclass; see `docs/emscripten-mainloop-game-lifetime.md` and `emscripten-mainloop-stack-spike/` for the full root-cause writeup and proof. This is what unblocked the real-browser verification below. |
| — | Unblocking `Draw()` exposed two pre-existing, never-before-executed bugs in this renderer's *own test files* (not the renderer implementation): `svgdom_smoke_test.cpp` checked a hardcoded sprite index (`0`) for two draws that the SVGDOM-A coalescing architecture correctly appends as later children of the same flush slot, and undercounted its own expected-check total by one; `svgdom_pixel_verification_test.cpp` built its SVGDOM-D coherence check's render target with the default `RenderTargetUsage::DiscardContents`, so the real, correct, XNA-documented discard-on-rebind clear (not a canvas-sync bug) was clearing the target before the check's own second draw. | Stale index/count assumptions and a render-target-usage mismatch in test code, not renderer defects. | Both files corrected: real per-child indices, correct expected-check count, and `RenderTargetUsage::PreserveContents` on the target that actually needs content preservation across a rebind (matching the same fix already established in `ascii_offscreentarget_test.cpp`). |

---

## How it draws

One `<svg id="cna-svg-dom-root">` is created over the `<canvas>` SDL3 already owns and anchored to
that canvas's host-page layout position. The canvas stays in layout and browser hit testing at
`opacity:0`, while the visible SVG root is pointer-transparent; SDL therefore keeps sizing the
canvas and receives mouse/touch input on the element where its
Emscripten driver installed handlers. The full pipeline this section describes — root creation, the standard `Game` loop
reaching `Draw()`, `SpriteBatch` output landing as real per-flush-slot `<g>` elements, tint,
blending, scissor/viewport clipping, and render-target round-trips — has run end-to-end in headless
Chromium via the `smoke`/`pixel`/`scissor-order` test pages; see "Validation performed" below for
the distinction between the previously executed baseline and the new SVGDOM-5 checks.

| XNA concept | SVG realization |
|---|---|
| destination position, rotation, scale, flip | one collapsed affine `transform="matrix(a,b,c,d,e,f)"` on the wrapping `<g>` (computed in C++, ✅ unit-tested numerically — `SvgDomSpriteBatchRenderer::BuildDrawCommandEXT`) |
| `Texture2D` + in-bounds `sourceRectangle` | the nested `<svg>`'s own `width`/`height`/`viewBox` crop an `<image>` sized to the full texture |
| `Texture2D` + out-of-bounds `sourceRectangle` under `Wrap`/symmetric `Mirror` | a `<rect>` filled by a per-pool-slot `<pattern patternUnits="userSpaceOnUse">`, phase-offset to line up with the requested source origin; `Mirror` reuses `Wrap`'s plain `repeat` against a pre-built quadrant-mirrored texture variant |
| tint RGB | a cached RGB-only `feColorMatrix` filter, omitted entirely for straight-white tint with a non-`Opaque` blend |
| tint alpha | dirty-diffed `opacity` on the sprite `<g>`; an AlphaBlend alpha-only fade uses no filter and cannot grow the filter cache |
| `BlendState.Additive` | `mix-blend-mode: plus-lighter` on the sprite's own wrapping `<g>` |
| `BlendState.Opaque` | the filter's alpha row forced to `0 0 0 0 1` (accepted alpha-stripped deviation — SVG/CSS compositing has no per-element Porter-Duff "replace") |
| `SpriteBatch` draw order | **DOM document order — now provably equal to actual flush order for every clip-state interleaving (SVGDOM-A)**, not merely the common unscissored case |
| `Clear` | the backbuffer `<rect>`'s own `fill`, plus rewinding the flush-slot cursor to 0 |
| `GraphicsDevice.Viewport` / `RasterizerState.ScissorTestEnable` | one effective clip rect (Viewport ∩ Scissor, SVGDOM-F), realized as a per-flush-slot `<clipPath>` |

Flush slots are **pooled and reused across frames**: `Clear()` rewinds the flush cursor to 0 (not a
teardown; a bumped "frame token" lets each slot cheaply tell whether it was already touched this
frame), each flush claims the next slot (creating one only the first time that index is needed, or
coalescing into the previous flush's slot when the clip state is unchanged), and every
attribute/style write is dirty-checked against that element's own last-applied state. `Present()`
hides (not removes) both the slot tail this frame did not reach and, within each active slot, its own
unused sprite-pool tail — the same "pool cursor + high-water hide" shape `HTML_DOM`'s own DOM path
uses. A whole batch crosses the wasm/JS boundary in **one** call.

---

## 1. SpriteBatch

| Feature | Status | Notes |
|---|---|---|
| `Draw(texture, x, y)` / full overload with rotation/origin/effects | ✅ (geometry) | Pivot/rotation/flip placement is unit-tested numerically (identity, scale, origin-pivot, 90° rotation, flip-flag). |
| Colour + alpha tint | ✅ (algorithm) | AlphaBlend's premultiplied XNA tint is converted to straight RGB; RGB uses `feColorMatrix` and A uses group `opacity`. The identical straight-tint input reaches the render-target Canvas2D path (`PrepareSpritePixelsEXT`) and is unit-tested. |
| `transformMatrix` in `Begin()` | ✅ (composition) | Composed onto each sprite's own collapsed matrix before it reaches JS, so two batches with different transforms coexist in one frame. |
| Custom `Effect` via `Begin(effect)` | ⛔ throws-by-design | Neither SVG nor CSS compositing has a programmable shader stage. |
| `SpriteSortMode` sequencing, `Immediate` mode | ✅ | Renderer-agnostic sort in shared `SpriteBatch.cpp`; document order realizes the result. `Immediate` flushes each `Draw()` as its own single-sprite flush — correctly participates in the SVGDOM-A ordered-slot/coalescing scheme like any other flush. |
| `SpriteFont` (`DrawString`) | ✅ | No renderer-specific code; every glyph funnels through the same `Draw` overload. |
| Cross-flush draw order across interleaved clip states | ✅ | **SVGDOM-A fix.** Structural regression test (`svgdom_scissor_order_test.cpp`) plus a Playwright pixel proof against a real screenshot. |
| `TextureAddressMode::Wrap` / symmetric `Mirror` for an out-of-bounds `sourceRectangle` | ✅ | `<pattern>` fill (SVG path) / direct wrap/mirror texel sampling (render-target path). Out-of-bounds `Clamp` and mixed per-axis addressing still throw deterministically (`ValidateAddressModesEXT`) — a real, narrower boundary than `HTML_DOM`'s own edge-padding/tiling variants, not yet investigated for feasibility in this pass. |

## 2. Texture2D / RenderTarget2D

| Feature | Status | Notes |
|---|---|---|
| `Texture2D` construction / `SetData` / `GetData` | ✅ | CPU-side RGBA8 buffer is the single source of truth. Bounds checks are overflow-safe (this pass). |
| Texture destruction / JS registry lifecycle | ✅ | **Fixed this pass** — the destructor now actually deletes its JS registry entry (previously leaked unconditionally; see table above). |
| Texture upload → SVG `<image href>` | ✅ (encode) | PNG encode runs in C++ (SDL3_image), round-tripped byte-exact through a real SDL3_image decode natively. |
| Mip-level (`level>0`) `SetData` | ⛔ throws-by-design | No mip chain, matching `CANVAS`/`HTML_DOM`/`SDL_RENDERER`. |
| `RenderTarget2D` construction / bind / unbind | ✅ | Backed by a real private off-screen canvas. |
| `RenderTarget2D::SetData` / canvas coherence | ✅ | **SVGDOM-D fix.** SetData is now immediately reflected in the canvas regardless of bind state; `EnsureFreshEXT()` always re-reads the canvas while the target is the currently-bound one. |
| `RenderTarget2D` drawn into while bound | ✅ (geometry + scissor) | **SVGDOM-B fix** for scissor; geometry unchanged (real, correct). |
| `RenderTarget2D` sampled back as a `Draw()` source texture | ✅ | **SVGDOM-E fix.** Was undefined behaviour / a JS crash before this pass; now dynamic_cast-dispatched and freshness-checked. |
| `RenderTarget2D::GetData` | ✅ | Never-bound/`UpdatePixels`-only content reads the CPU buffer directly; once dirty, refreshed via a real `getImageData` readback (Emscripten) or an honest `false` (native, no browser canvas exists). |
| `RenderTargetUsage` | ✅ | Shared `GraphicsDevice` concern; no renderer-specific code. |
| `depthFormat`/`mipMap`/`multiSampleCount` at construction | ⛔ throws-by-design | No depth storage, no mip chain, no MSAA. |
| `SetRenderTargets` (MRT) | ⛔ throws-by-design | One canvas backs each target; inherently single-output. |
| **Backbuffer readback** | ⛔ throws-by-design | No browser API rasterizes a live SVG subtree to pixels synchronously — the same constraint `HTML_DOM`'s own DOM/CSS backbuffer has. Render into a `RenderTarget2D` and read that instead. |

## 3. BlendState

| Feature | Status | Notes |
|---|---|---|
| `Opaque` | ✅ (SVG: accepted deviation; RT: exact) | SVG path forces the filter's alpha row to `0 0 0 0 1` (same accepted deviation `HTML_DOM` documents for its own `<div>` path — neither SVG nor CSS compositing has a per-element Porter-Duff "replace"). Canvas2D render-target path uses real `globalCompositeOperation='copy'`, an exact Porter-Duff replace. |
| `AlphaBlend` | ✅ (un-premultiply math) | Drawn from a lazily generated un-premultiplied pixel copy, unit-tested including `alpha==0`/`alpha==255` edge cases. |
| `NonPremultiplied` | ✅ | Source pixels as uploaded, matching source-over's own assumption. |
| `Additive` | ✅ (native query) | `mix-blend-mode: plus-lighter` (SVG) / `globalCompositeOperation='lighter'` (RT, exact). `SupportsCapability(AdditiveBlending)` queries `CSS.supports(...)` for real. |
| Any other custom `BlendState` | ⛔ throws-by-design | Neither SVG nor CSS compositing has a blend-factor/equation model. |
| `ColorWriteChannels` / `MultiSampleMask` | ⛔ throws-by-design outside defaults | No per-channel write mask or coverage mask exists. |

Real-browser pixel verification of blend presets is confirmed for the render-target path
(deterministic `GetData()` readback, Playwright + headless Chromium — `cna_test_svgdom_pixel_verification`,
20/20 checks passing). The SVG-backbuffer path's own `feColorMatrix` tint filter and
`mix-blend-mode: plus-lighter` are confirmed structurally applied (real DOM attribute/style
inspection in a live browser, `cna_test_svgdom_smoke`, 13/13 checks passing) and the SVGDOM-A
ordering fix has a real screenshot pixel proof (`cna_test_svgdom_scissor_order`, 1/1); the SVG path's
own composited output has not additionally been screenshot-diffed pixel-for-pixel beyond that
ordering proof — a real, narrower gap than the render-target path's exhaustive `GetData()` coverage,
not a blocked one.

## 4. SamplerState

| Feature | Status | Notes |
|---|---|---|
| `TextureFilter` (magnification) | ✅ | `image-rendering: auto` vs `pixelated`. Ordinal validation is unit-tested. |
| `TextureAddressMode` (`Wrap`/symmetric `Mirror`, out-of-bounds `sourceRectangle`) | ✅ | See §1. Out-of-bounds `Clamp` and mixed per-axis addressing still throw — a real, narrower boundary than `HTML_DOM`'s, not yet proven infeasible to close. |

## 5. Viewport / PresentationParameters / Rasterizer

| Feature | Status | Notes |
|---|---|---|
| `GetViewportSize` / `SetVirtualResolution` / `SetPresentationMode` | ✅ (math); geometry-push ✅ | All five `CnaPresentationMode` values share the identical XNA/Windows-Phone geometry contract every 2D-DOM CNA renderer implements. **SVGDOM-C fix:** geometry is now pushed to JS from the constructor and from these two setters immediately, not only `Present()`. |
| `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ | Pure C++, unit-tested against the same `FakeWindow()` harness `HtmlDomRendererTests.cpp` uses. |
| `GraphicsDevice.ScissorRectangle` (`SetScissorRect`) | ✅ | **SVGDOM-A/B fixes.** Per-flush ordered slots; applied on both the SVG and render-target-bound paths, intersected with the active Viewport. |
| `GraphicsDevice.Viewport` (`SetViewport`) | ✅ | **SVGDOM-F fix.** Width/Height now enforce an unconditional clip (independent of `ScissorTestEnable`), matching `spritebatch_custom_viewport_test.cpp`'s own cross-renderer contract; (X,Y) composed as the outermost sprite translation as before. |
| `ApplyDepthStencilState` / `SetReferenceStencil` | ✅ truthful 2D boundary | A fully-disabled depth/stencil state and reference value zero are accepted; anything else throws. |
| `ApplyRasterizerState` | ✅ truthful 2D subset | Ordinary filled 2D/default-cull states and `scissorTestEnable` are accepted; clockwise-face culling, wireframe and non-zero depth bias throw. |

## 6. The 3D surface

Every direct 3D entry point this renderer overrides throws `std::runtime_error("SVG_DOM renderer: …
not yet implemented")`: depth/stencil clears, `SetDepthTestEnabled`/`SetBlendEnabled`/
`SetDepthWriteEnabled`, vertex and index buffer creation, both `Draw*Primitives` families, and the
five unsupported resource factories. ✅ All unit-tested natively (`SvgDom3DSurfaceTest`).

`GraphicsDevice::SupportsCapability()` reports `false` for every capability except
`AdditiveBlending`, which reports the browser's real `mix-blend-mode: plus-lighter` support
(honestly `false` on a native/non-browser build). `SupportsDepthStencil()` is unconditionally
`false`.

---

## Known limitations (intentional, re-verified this pass)

1. **No backbuffer readback** ⛔ — no browser API rasterizes a live SVG subtree synchronously.
   Render into a `RenderTarget2D` and read that, or use `CANVAS`.
2. **Out-of-bounds `Clamp` and mixed per-axis addressing throw** ⬜ — implementable in principle (an
   edge-extended texture variant, the same technique `HTML_DOM` already uses for its own Clamp
   overflow), not yet built. Not proven infeasible; a real follow-up, not a documented-permanent
   boundary.
3. **Custom `BlendState`s throw** ⛔ — only the four standard presets exist in SVG/CSS compositing.
4. **No custom `Effect`s** ⛔ — there is no shader stage.
5. **No MSAA, no depth, no stencil** ⛔ — the same 2D-only boundary as `SDL_RENDERER`/`CANVAS`/`HTML_DOM`.
6. **No hierarchical camera/world `<g>` grouping** ⬜ — a scene where only the camera moves still
   updates every visible sprite's own `transform` rather than a single shared parent transform. A
   real, valuable follow-up for large static scenes (tilemaps, mostly-static UI); out of scope here.
7. **Effectively one live, actively-driven `GraphicsDevice` per process** — the same
   `emscripten_set_main_loop` constraint every Emscripten CNA renderer shares, not specific to
   `SVG_DOM`. The existing ref-counted `EnsureRoot`/`DestroyRoot` pair correctly handles
   construct→destroy→reconstruct on the *same* window; genuine concurrent multi-window use was not
   exercised (CNA's Emscripten renderers share this single-Module-per-process assumption generally).

---

## Formerly a platform gate, now fixed

A previous remediation pass on this renderer left one item genuinely unresolved: `Game::BeginDraw()`
crashed under Emscripten before the first `Draw()`, blocking full pixel/structural browser
verification. That pass correctly identified it as a **pre-existing, cross-renderer defect**
(reproducing identically for `HTML_DOM`), not an `SVG_DOM` defect, and refused to fabricate browser
verification it hadn't actually obtained.

A dedicated follow-up pass root-caused and fixed it. **Summary of the finding** (full write-up:
`docs/emscripten-mainloop-game-lifetime.md`; proof: `emscripten-mainloop-stack-spike/`):

`emscripten_set_main_loop(fn, fps, simulateInfiniteLoop=1)` is implemented by the Emscripten
runtime as a raw JavaScript `throw 'unwind'`. CNA compiles with `-fwasm-exceptions` (native
WebAssembly exception handling), under which the `catch_all`/cleanup landing pad generated for a
local object with a non-trivial destructor genuinely catches *any* exception unwinding through it —
including that foreign JS throw. Every CNA Emscripten example (including this renderer's own)
stack-allocated its `Game` subclass in `main()`. That stack-local object's destructor therefore ran
for real, immediately, at the `emscripten_set_main_loop` call site — deleting the real
`GraphicsDeviceManager` through its owning `unique_ptr` member and leaving `Game::graphicsDeviceManager_`
(a raw, non-owning pointer) dangling. The dangling pointer didn't fault until, frames later, that
freed heap memory had been reused by something else and `Game::BeginDraw()` dereferenced it to make
a virtual call — misreading the corrupted memory as a WebAssembly function-table index. This was
**not** a `GameServiceContainer`/multiple-inheritance defect — that hypothesis was investigated and
ruled out with sanitizer-verified reproductions (native ASan+UBSan+vptr and Emscripten
`-sSAFE_HEAP`) before the real cause was found; see the spike's own `README.md`.

**The fix:** every CNA Emscripten example's `main()` — `SVG_DOM`, `HTML_DOM`, `CANVAS`, and the
general 3D demo — now heap-allocates its `Game` subclass instead of stack-allocating it (`new`,
deliberately never `delete`d, correct for a page-lifetime app object), and `Game::Run()`'s own doc
comment documents the constraint. This is a call-site object-lifetime fix, not a change to `Game`,
`GraphicsDeviceManager`, or `GameServiceContainer` themselves.

With that fix in place, `Game::BeginDraw()`/`Draw()`/`EndDraw()` now run correctly under Emscripten,
and the pixel/structural browser test pages this renderer's earlier remediation pass wrote — but
could not execute — now run for real. See "Validation performed" below for the actual results.

---

## Validation performed

| What | How | Result |
|---|---|---|
| Renderer-identity registry (46 public identities preserved) | `python3 scripts/check_renderer_identities.py` | ✅ `OK: 46 public renderer identities preserved in both registries` |
| Pure-C++ pixel/geometry pipeline, including SVGDOM-5 tint normalization | Native GTest binary, `CNA_BUILD_SVG_DOM_HOST_TESTS=ON` | ✅ **100/100** `cna_test_svgdom_host` cases pass natively |
| Cross-renderer native regression (`CnaTests`, HEADLESS, real X server via `xvfb-run`) | Full native build/run | ✅ 6032/6079 pass, 46 legitimately skipped (unrelated hardware/permission gates), one unrelated pre-existing `HeadlessRenderer` MRT-test failure confirmed unaffected by anything touched here |
| `-DCNA_GRAPHICS_RENDERER=SVG_DOM` Emscripten configure/compile/link, all three test pages | `emcmake cmake` + `cmake --build`, emsdk 6.0.3 verified | ✅ compiles and links cleanly |
| Real SVG DOM root creation, live document insertion | Headless Chromium via Playwright | 🌐 verified |
| Structural smoke checks: `SVGSVGElement` root, preserved and geometrically aligned SDL input surface, tint filter, filter-free alpha opacity, `Additive`, render-target round trip, readback-throws contract | `scripts/run-svgdom-browser-test.sh smoke` (headless Chromium via Playwright) | 🌐 Previous 13-check baseline passed; expanded **16-check SVGDOM-5/6 suite compiles**, browser rerun pending in an environment with an available browser |
| SVGDOM-B/D/E/F/5 pixel-exact verification: RT scissor, coherence, RT-as-texture sampling, viewport clip, AlphaBlend tint | `scripts/run-svgdom-browser-test.sh pixel` (real `RenderTarget2D::GetData()` readbacks) | 🌐 Previous **20/20** baseline passed; expanded 21-check suite compiles, new SVGDOM-5 browser check pending |
| SVGDOM-A cross-region draw-order fix, real screenshot pixel proof | `scripts/run-svgdom-browser-test.sh scissor-order` (real page screenshot, 4-point pixel sampling) | 🌐 **1/1 checks passed** |
| Sibling renderer cross-check: the same Emscripten `Game::BeginDraw()` fix, applied generically (not `SVG_DOM`-specific) | `HTML_DOM`'s full existing browser suite (smoke/pixel/dispose/memory/stress) | 🌐 **137/137 checks passed**, all newly unblocked by the same fix |

Select it with:

```bash
emcmake cmake -S . -B cmake-build-svgdom -DCNA_GRAPHICS_RENDERER=SVG_DOM
cmake --build cmake-build-svgdom --target cna_test_svgdom_smoke cna_test_svgdom_pixel_verification cna_test_svgdom_scissor_order -j4
```

Native host-contract tests (no Emscripten SDK required):

```bash
cmake -S . -B cmake-build-svgdom-host -DCNA_GRAPHICS_RENDERER=HEADLESS \
  -DCNA_BUILD_TESTS=ON -DCNA_BUILD_SVG_DOM_HOST_TESTS=ON
cmake --build cmake-build-svgdom-host --target cna_test_svgdom_host -j4
./cmake-build-svgdom-host/modules/renderers/cna_test_svgdom_host
```

Browser test suite (requires a working Emscripten build; runs to completion, see results above):

```bash
scripts/run-svgdom-browser-test.sh cmake-build-svgdom smoke
scripts/run-svgdom-browser-test.sh cmake-build-svgdom pixel
scripts/run-svgdom-browser-test.sh cmake-build-svgdom scissor-order
```

`CNA_BUILD_SVG_DOM_HOST_TESTS` mirrors `HTML_DOM`'s own `CNA_BUILD_HTML_DOM_HOST_TESTS` precedent —
compiling the SVG_DOM implementation itself into a native GTest executable, where every `EM_JS`
body is excluded by `__EMSCRIPTEN__`. See the **SHARED-HOOK REQUIREMENT** note in
`modules/renderers/CMakeLists.txt` for why this target is defined there rather than inside
`modules/renderers/svg-dom/`.
