# SVG DOM Graphics Renderer — Implementation Plan


> **Renderer selection.** This document describes the renderer as a compile-time choice
> (`-DCNA_GRAPHICS_RENDERER=...`), which remains the default and recommended mode. Since
> `plan_runtimerenderer.md`, CNA can also be built with several renderers and choose between
> them at runtime — see `docs/runtime-renderer-selection.md`. Nothing below changes in
> single-renderer mode.

> **Reconstructed 2026-08-11.** This file was referenced from the very first commit of the
> `SVG_DOM` renderer (`// plan_svg_dom.md design decision N/6 ...` throughout its source, the same
> convention `plan_html_dom.md` established for `HTML_DOM`) but the plan document itself was never
> actually created/committed — confirmed via `git log --all -- plan_svg_dom.md`, which returns
> nothing. This reconstructs it from the real, current implementation, `docs/svg-dom-renderer.md`,
> and the source's own `plan_svg_dom.md design decision`/`SVGDOM-N` comments, so the six design
> decisions and task IDs below are not invented — they are the ones the code already cites by
> number. Historical framing that would have existed at proposal time (an approval quote, an
> original task numbering rationale) is not reconstructed where the repository has no record of it.
>
> **Status legend** (this project's own convention, matching `plan_html_dom.md`): ✅ implemented
> *and verified against its stated acceptance criteria*; 🟨 code or documentation exists but has
> not met those criteria; ⬜ not implemented.

---

## What this renderer is

`SVG_DOM` renders XNA `SpriteBatch` output as **real SVG namespace DOM elements** — one
`<svg id="cna-svg-dom-root">` surface, containing one document-ordered `<g>` "flush slot" per
distinct clip state a frame actually used, each holding one pooled `<g>` per visible sprite (a
nested `<svg>` cropping an `<image>`, or a `<pattern>`-filled `<rect>` for tiled draws). There is no
`<canvas>` in the sprite path at all — the browser's own SVG renderer does the drawing, the same
way `HTML_DOM`'s compositor draws its pooled `<div>`s.

It is CNA's **forty-second and newest** public renderer identity, and its **third** browser-native
2D-only one, alongside `CANVAS` and `HTML_DOM`:

| | `CANVAS` | `HTML_DOM` | `SVG_DOM` |
|---|---|---|---|
| Draw primitive | `ctx.drawImage()` into one `<canvas>` | one styled `<div>` per sprite | one `<svg>`/`<image>` (or `<pattern>`-`<rect>`) per sprite |
| Who rasterizes | Canvas2D, per frame, every sprite | the browser compositor | the browser's SVG renderer |
| Tint | pre-tinted cached bitmap variant | pre-tinted cached bitmap variant | native `feColorMatrix` filter (no bitmap re-encode per tint) |
| Texture → URL | n/a (raw canvas) | `canvas.toDataURL()`, JS-side | PNG encode in C++ via SDL3_image, no JS canvas round-trip |
| Readback | `getImageData` on the main canvas | impossible (see design decision 5) | impossible (see design decision 5) |

The trade against `HTML_DOM` is the drawing primitive, not the scope: SVG's native filter/pattern
primitives let tint and tiling be expressed *declaratively*, without `HTML_DOM`'s own
pre-tinted-bitmap-variant cache, at the cost of a genuine `<svg>` element per sprite (heavier DOM
footprint than a single `<div>`) and no equivalent to CSS `background-repeat` for plain `Wrap`.

`EASYGL` (WebGL2) remains the default Emscripten renderer for anything beyond 2D; `SVG_DOM` — like
`HTML_DOM` — is for games that want their sprites to genuinely exist as inspectable DOM elements
and want compositing primitives (filters, patterns) expressed declaratively rather than through a
per-frame Canvas2D redraw.

---

## Scope

**2D only.** `SpriteBatch`, `Texture2D`, `SpriteFont`, `RenderTarget2D`. Every direct 3D entry
point this renderer overrides throws `std::runtime_error("SVG_DOM renderer: … not yet
implemented")`: depth/stencil clears, `SetDepthTestEnabled`/`SetBlendEnabled`/
`SetDepthWriteEnabled`, vertex and index buffer creation, both `Draw*Primitives` families, and the
five unsupported resource factories — the same shape `HTML_DOM` established for its own 3D
boundary, unit-tested here as `SvgDom3DSurfaceTest`.

**Emscripten-only**, gated in CMake exactly like `CANVAS`/`HTML_DOM`: `document`, `SVGSVGElement`
and SVG DOM APIs do not exist in a native build. The `.cpp` files still compile natively (every
`EM_JS` block sits behind `#if defined(__EMSCRIPTEN__)`), so the pure-C++ geometry/state logic
stays unit-testable without an Emscripten SDK (`cna_test_svgdom_host`, the `HTML_DOM`-established
`CNA_BUILD_*_HOST_TESTS` pattern).

---

## Design decisions

The six the source cites by number (`plan_svg_dom.md design decision N`):

**1. Reuse SDL3's window; own the SVG overlay.** SDL3's Emscripten video driver already creates
and sizes a `<canvas>`; input, events and `SDL_GetWindowSize` all keep working. This renderer adds
its own `<svg id="cna-svg-dom-root">` positioned over that canvas element and hides the canvas
itself (`visibility:hidden`, not `display:none`, so SDL keeps sizing it). SDL3 stays linked even
though no `SDL_Renderer` is ever created — SDL3_image backs the PNG encode design decision 2 needs.

**2. Textures become PNG data URIs, encoded in C++, not JS.** An SVG `<image>` element's `href`
needs a URL. `HTML_DOM` derives one from a private JS-side canvas via `canvas.toDataURL()`; this
renderer instead already owns the RGBA8 bytes in C++ (required for `GetData`/`UpdatePixels`
regardless) and encodes the PNG itself with SDL3_image — so the data URI is available the instant
it is asked for, with no JS canvas round-trip at all. Straight and un-premultiplied variants are
each encoded at most once per pixel generation and cached until `UpdatePixels` invalidates them.

**3 & 4. Blend/tint via source-pixel preparation, not compositing tricks.** Neither CSS
Compositing's `mix-blend-mode` (which an SVG element also participates in) nor SVG's own
filter/compositing primitives expose a per-channel blend-factor model, so the four standard XNA
`BlendState` presets are reproduced by preparing the *source pixels* instead of the compositing
step — the same strategy `HTML_DOM` uses for its `<div>` path, independently re-derived here for
SVG's own primitives (an `<image>`'s referenced bytes plus an `feColorMatrix` filter, not a
`background-image` variant):
- `Opaque` forces the drawn element's own filter output alpha to a constant 1 (`SvgDomTintFilterEXT`)
  — the closest a single stacked, filtered element can get to a true Porter-Duff "copy" (an
  accepted, documented deviation, matching `HTML_DOM`'s own).
- `AlphaBlend`/`NonPremultiplied` select the un-premultiplied or straight pixel variant to match
  each preset's own `srcBlend` assumption.
- Tint (RGB and A together) is a cached `feColorMatrix` filter, omitted entirely for `Color.White`
  with a non-`Opaque` blend so the overwhelmingly common untinted draw carries zero filter
  overhead.
- `Additive` maps to `mix-blend-mode: plus-lighter` — CSS Compositing's exact `lighter` operator,
  the same one `HTML_DOM`/`CANVAS` use. Any `BlendState` outside the four standard presets throws.

**5. `RenderTarget2D` falls back to a private Canvas2D surface while bound.** An `<svg>`/`<image>`
element cannot be rendered into and read back synchronously — no browser API rasterizes a live
vector subtree to pixels without an async image decode. So, exactly like `HTML_DOM`'s own render
targets, a bound target switches the sprite path over to a private off-screen `<canvas>`/Canvas2D
context for the duration of the binding; the SVG sprite path and `Present()` are untouched by this.
Backbuffer readback is impossible for the same reason and says so: `ReadBackbuffer` throws unless a
render target is bound, in which case it reads that target's real canvas via `getImageData`.

**6. One JS call per batch; pooled elements; collapsed affine matrix; Viewport as outermost
translation.** Matching `HtmlDom::HtmlDomDrawCommand`'s own "one crossing per batch" strategy:
`Draw()` appends one fixed-stride `SvgDomDrawCommand` per sprite into a reusable `std::vector`, and
`End()` hands the whole array to JS in a single call. All geometry (`translate(destX,destY)
rotate(rotation) scale(scaleX,scaleY) [flip] translate(localX,localY)`) is resolved on the C++ side
into one 2×3 affine matrix (the SVG/CSS `matrix(a,b,c,d,e,f)` convention) rather than a
transform-list string — compact for the crossing and directly unit-testable numerically. The JS
side pools/reuses sprite elements across frames and dirty-diffs every attribute/style write against
each element's own last-applied state, the same "pool cursor reset at `Clear()`, reuse by index,
hide the unused tail at `Present()`" shape `HTML_DOM`'s own DOM path uses. `GraphicsDevice.Viewport`
X/Y is composed as the *outermost* translation on top of each sprite's own placement (matching real
XNA/FNA, which applies `Viewport.X/Y` strictly after `SpriteBatch.Begin(transformMatrix)`); Width/
Height enforce an unconditional clip via `ComputeEffectiveClipRectEXT` (SVGDOM-F, see below).

---

## Phases and tasks

### S1 — Build wiring, identity, skeleton

| Task | Status | Description |
|---|---|---|
| SVGDOM-S1a | ✅ | `GraphicsRendererType::SvgDom` public identity registered (41→42), `CNA_GRAPHICS_RENDERER=SVG_DOM` CMake option, `scripts/check_renderer_identities.py` passing. |
| SVGDOM-S1b | ✅ | Emscripten-only gate in `cmake/RendererSelection.cmake`, matching `CANVAS`/`HTML_DOM`. |
| SVGDOM-S1c | ✅ | `modules/renderers/svg-dom/{CMakeLists.txt,include/,src/,tests/,examples/}` physical module skeleton (design decision 1), links `SDL3::SDL3`/`SDL3_image::SDL3_image`. |
| SVGDOM-S1d | ✅ | `SvgDomRenderer` + sibling classes (`SvgDomState`, `SvgDomTextureRenderer`, `SvgDomRenderTargetRenderer`, `SvgDomSpriteBatchRenderer`) implementing `IGraphicsRenderer`/`ITextureRenderer`/`IRenderTargetRenderer`/`ISpriteBatchRenderer`. |
| SVGDOM-S1e | ✅ | `SupportsCapability()` → `false` for every capability except `AdditiveBlending` (real `CSS.supports('mix-blend-mode','plus-lighter')` query); `SupportsDepthStencil()` → `false`. |

### S2 — SVG surface, Clear, Present, viewport/scissor geometry

| Task | Status | Description |
|---|---|---|
| SVGDOM-S2a | ✅ | `CNA_SvgDom_EnsureRoot()` — creates `<svg id="cna-svg-dom-root">` over SDL's canvas; SVGDOM-5 keeps the canvas transparent but hit-testable so SDL input continues to work. |
| SVGDOM-S2b | ✅ | `Clear()` → backbuffer `<rect>` fill, rewinds the flush-slot cursor to 0; routes to the bound render target's canvas when one is bound. |
| SVGDOM-S2c | ✅ | `Present()` hides the flush-slot tail this frame did not reach and, within each active slot, its own unused sprite-pool tail. |
| SVGDOM-S2d | ✅ | `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode` — the same `FixedHeightDynamicWidth` logical-size contract every 2D-DOM CNA renderer implements. |
| SVGDOM-S2e | ✅ | `TransformWindowToLogical`/`TransformLogicalToWindow`, unit-tested against the same `FakeWindow()` harness `HtmlDomRendererTests.cpp` uses. |
| SVGDOM-C | ✅ | **First-frame geometry fix.** `cnaSvgDomLogicalW/H` used to stay `0` until the first `Present()`, misclassifying a pre-`Present()` scissor rect as covering a 0×0 surface. `ApplySurfaceGeometryEXT()` now also runs from the constructor and from `SetVirtualResolution`/`SetPresentationMode`. |

### S3 — Textures

| Task | Status | Description |
|---|---|---|
| SVGDOM-S3a | ✅ | `SvgDomTextureRenderer` — CPU-side RGBA8 buffer as single source of truth, C++-side PNG data-URI encode via SDL3_image (design decision 2), integer id in `Module['cnaSvgDomTextures']`. |
| SVGDOM-S3b | ✅ | `UpdatePixels` re-uploads and invalidates cached variants; mip-level (`level>0`) `SetData` throws — no mip chain, matching `CANVAS`/`HTML_DOM`/`SDL_RENDERER`. |
| SVGDOM-S3c | ✅ | Texture destruction actually deletes its JS registry entry (`CNA_SvgDom_DestroyTexture`) — fixed a genuine, unbounded memory leak found during the correctness remediation pass below; the destructor was `= default` despite its own header doc claiming otherwise. |
| SVGDOM-S3d | ✅ | `GetData`/texture-construction bounds checks compute in `int64_t`, not 32-bit `int` — overflow-safe on Emscripten/wasm32, where `int` and `size_t` are both 32-bit. |

### S4 — SpriteBatch

| Task | Status | Description |
|---|---|---|
| SVGDOM-S4a | ✅ | Fixed-stride `SvgDomDrawCommand` buffer, single `EM_JS` flush per batch (design decision 6). |
| SVGDOM-S4b | ✅ | Sprite geometry: collapsed 2×3 affine matrix, unit-tested numerically (identity, scale, origin-pivot, 90° rotation, flip-flag) in `SvgDomSpriteBatchRenderer::BuildDrawCommandEXT`. |
| SVGDOM-S4c | ✅ | `Texture2D` + in-bounds `sourceRectangle` → nested `<svg>`'s own `width`/`height`/`viewBox` cropping an `<image>`. |
| SVGDOM-S4d | ✅ | `SpriteFont::DrawString` needs no renderer-specific code — every glyph funnels through the same `Draw` overload. |
| SVGDOM-S4e | ✅ | `Begin(transformMatrix)` composed onto each sprite's own collapsed matrix before it reaches JS. |
| SVGDOM-S4f | ✅ | Custom `Effect` via `Begin(effect)` throws — no programmable shader stage exists in SVG/CSS compositing. |
| SVGDOM-1 | ✅ | `TextureAddressMode::Wrap` (both axes) and symmetric `Mirror` (same mode both axes) for an out-of-bounds `sourceRectangle`: a `<rect>` filled by a per-pool-slot `<pattern patternUnits="userSpaceOnUse">`, phase-offset to the requested source origin; `Mirror` reuses `Wrap`'s plain `repeat` against a pre-built quadrant-mirrored texture variant. |
| SVGDOM-2 | ⬜ | Out-of-bounds `Clamp` and mixed per-axis addressing (e.g. `U=Wrap, V=Clamp`) still throw (`ValidateAddressModesEXT`) — a real, narrower boundary than `HTML_DOM`'s own edge-padding/tiling variants. Implementable in principle (an edge-extended texture variant, the technique `HTML_DOM` already uses for its own Clamp overflow); not yet built, not proven infeasible. |
| SVGDOM-4 | ✅ | **Per-flush-isolated scissor regions**, replacing an earlier single global clip — the direct predecessor of the SVGDOM-A architecture below (superseded by it: the original per-clip-identity "region" model was itself replaced by per-flush ordered slots to fix SVGDOM-A's draw-order bug). |

### S5 — Blend and sampler state

| Task | Status | Description |
|---|---|---|
| SVGDOM-S5a | ✅ | `BlendStateToDomCompositeOp`-equivalent mapping (design decisions 3–4): `Opaque`/`AlphaBlend`/`NonPremultiplied` via source-pixel preparation, `Additive` via `mix-blend-mode: plus-lighter`. Custom `BlendState`s and `ColorWriteChannels`/`MultiSampleMask` throw. |
| SVGDOM-S5b | ✅ | `TextureFilter` → `image-rendering: auto` vs `pixelated`, ordinal-validated and unit-tested. |

### S6 — Render targets

| Task | Status | Description |
|---|---|---|
| SVGDOM-S6a | ✅ | `SvgDomRenderTargetRenderer` — real private off-screen `<canvas>` per target (design decision 5), `BindAsRenderTarget`/`UnbindAsRenderTarget`. |
| SVGDOM-S6b | ✅ | `GetData` — real synchronous `getImageData` once the CPU/canvas representations are known coherent (`EnsureFreshEXT`), or an honest `false` when a needed readback cannot run. |
| SVGDOM-S6c | ✅ | `RenderTargetUsage::DiscardContents` (the XNA default) correctly clears to black on every bind, matching FNA's own documented contract; `PreserveContents` targets skip that clear. |
| SVGDOM-S6d | ✅ | `depthFormat`/`mipMap`/`multiSampleCount` at construction throw; `SetRenderTargets` (MRT) throws — one canvas backs each target, inherently single-output. |
| SVGDOM-B | ✅ | **Render-target scissor fix.** The render-target-bound Canvas2D draw path never consulted `RasterizerState.ScissorTestEnable`/`GraphicsDevice.ScissorRectangle` at all. The effective clip (Viewport ∩ Scissor) is now captured once per flush and applied via a `ctx.save()/rect()/clip()/…/restore()` bracket, matching the SVG path's own batch-level timing. |
| SVGDOM-D | ✅ | **`UpdatePixels`/canvas coherence fix.** A `RenderTarget2D::SetData` call used to update only the C++ CPU buffer, never the JS-side canvas, so a subsequent canvas-composited draw could composite against stale pre-`SetData` pixels. `UpdatePixels` now pushes the new pixels into the canvas immediately, and `EnsureFreshEXT()` always re-reads the canvas while the target is the one currently bound. |
| SVGDOM-E | ✅ | **Render-target-as-texture fix.** Drawing a `RenderTarget2D` as an ordinary `SpriteBatch.Draw()` source texture was undefined behaviour in every build: `static_cast<const SvgDomTextureRenderer&>` silently reinterpreted a sibling class's memory as the wrong concrete type, and the JS registry assumed a shape a render target's own entry never had. Replaced with `dynamic_cast`-based dispatch (`AsRenderTargetEXT`/`CanvasIdOfEXT`/`PixelsOfEXT`/`DataUriOfEXT`, the pattern `HTML_DOM` already uses for the identical sibling-class shape) and made the JS registration additive. |

### S7 — Viewport / Rasterizer

| Task | Status | Description |
|---|---|---|
| SVGDOM-F | ✅ | **Viewport Width/Height clip fix.** `SetViewport` used to discard `Width`/`Height`, keeping only the `(X,Y)` translation — violating this project's own cross-renderer `SpriteBatch` contract (`spritebatch_custom_viewport_test.cpp`), which requires a smaller `Viewport` to clip rendering to its own rectangle unconditionally, independent of `RasterizerState.ScissorTestEnable`. `SvgDomState::SetCurrentViewportRectEXT` now records the full rectangle; `ComputeEffectiveClipRectEXT` intersects it with any active scissor rect. |
| SVGDOM-S7a | ✅ | `ApplyDepthStencilState`/`SetReferenceStencil` — a fully-disabled depth/stencil state and reference value zero are accepted; anything else throws (truthful 2D boundary). |
| SVGDOM-S7b | ✅ | `ApplyRasterizerState` — ordinary filled 2D/default-cull states and `scissorTestEnable` accepted; clockwise-face culling, wireframe and non-zero depth bias throw. |

### S8 — Verification and documentation (initial pass)

| Task | Status | Description |
|---|---|---|
| SVGDOM-S8a | ✅ | Native host-contract test suite (`cna_test_svgdom_host`, `CNA_BUILD_SVG_DOM_HOST_TESTS`), the same `HTML_DOM`-established pattern of compiling the implementation itself into a GTest binary with every `EM_JS` body excluded by `__EMSCRIPTEN__`. |
| SVGDOM-S8b | ✅ | `svgdom_smoke_test.cpp` — end-to-end vertical-slice structural/feature smoke test. |
| SVGDOM-S8c | ✅ | `docs/svg-dom-renderer.md` — capability status document, this project's convention. |

### S9 — Deep correctness remediation (SVGDOM-A..H)

A dedicated remediation pass re-derived this renderer's actual behavior from source (not from its
own prior documentation), found and fixed eight named defects, and built a three-page real-browser
verification suite. Findings A, B, D, E, F are cross-referenced in the phases above under their own
letter IDs; this phase lists the pass as a whole.

| Task | Status | Description |
|---|---|---|
| SVGDOM-A | ✅ | **Cross-region draw-order fix.** Paint order used to follow each clip-rect's own first-DOM-creation order, not actual `SpriteBatch` flush order. Replaced clip-identity-keyed regions with per-flush ordered DOM slots: every flush claims the next array slot, appended to `root` only the first time that index is used, never reordered afterward. Consecutive flushes sharing the same effective clip state coalesce into one slot. |
| SVGDOM-G | ✅ | Real-browser verification (structural + pixel), initially blocked by an external, cross-renderer Emscripten defect (see S10 below); completed once that defect was fixed — `cna_test_svgdom_smoke` 13/13, `cna_test_svgdom_pixel_verification` 20/20, `cna_test_svgdom_scissor_order` 1/1 (real screenshot pixel proof of SVGDOM-A). |
| SVGDOM-H | ✅ | `docs/svg-dom-renderer.md` rewritten from verified state only, with a status legend distinguishing native-verified/browser-verified/intentionally-unsupported/not-implemented, and (after S10) its capability tables and "Validation performed" section reflecting real executed browser results. |
| — | ✅ | Two additional defects found and fixed along the way, not part of the original A–H list: the texture-registry destructor leak (S3c above) and overflow-unsafe `GetData`/construction bounds checks (S3d above). |
| — | ✅ | One genuinely contributing, cross-renderer (not `SVG_DOM`-specific) fix found and made along the way: `GameWindow::queryClientBoundsFromSDL()` threw on a real Emscripten SDL3 video-subsystem startup race (`SDL_GetWindowSize` failing for the first one to two event-loop ticks after `SDL_CreateWindow()` returns). Falls back to the last-known bounds for that one tick instead of throwing. |

### S10 — The Emscripten `Game::BeginDraw()` platform gate (found, then fixed in a follow-up pass)

The S9 pass could implement and natively test SVGDOM-A/B/D/E/F but could not run the real-browser
pixel/structural suite it built: `Game::BeginDraw()` crashed under Emscripten before the first
`Draw()`, for *any* renderer (reproduced identically for `HTML_DOM`) — correctly identified and
documented as an external, cross-renderer defect rather than worked around or silently claimed
fixed.

| Task | Status | Description |
|---|---|---|
| — | ✅ | **Root cause fixed in the framework, without a game workaround**: the old `emscripten_set_main_loop(fn, fps, simulateInfiniteLoop=1)` path unwound and destroyed a stack-local `Game` while its browser callback retained the address. CNA now exports one Asyncify-compatible JS exception ABI and `Game::Run()` awaits `requestAnimationFrame()` on the same logical Wasm stack. Stack-local games are valid again; `Run()` returns after `Exit()` and normal destruction follows. The `cna-template` smoke test proves three draws, the return from `Run()`, and destruction in both Firefox and Chromium for `SVG_DOM`, `HTML_DOM`, `CANVAS`, `PIXIJS`, `WEBGL1`, and `WEBGL2`. The historical reproduction remains in `emscripten-mainloop-stack-spike/`; the maintained contract is `docs/emscripten-mainloop-game-lifetime.md`. |
| — | ✅ | Unblocking `Draw()` exposed two pre-existing, never-before-executed bugs in this renderer's own test files (not the implementation): a stale hardcoded sprite child-index in `svgdom_smoke_test.cpp` (the SVGDOM-A coalescing architecture correctly appends later draws as later children of the same flush slot) and an undercounted expected-check total; a `RenderTargetUsage::DiscardContents` mismatch in `svgdom_pixel_verification_test.cpp`'s SVGDOM-D coherence check (the real, correct, XNA-documented discard-on-rebind clear, not a canvas-sync bug). Both fixed. |

### S11 — Real-game input and Firefox performance remediation

| Task | Status | Description |
|---|---|---|
| SVGDOM-5 | ✅ | **Mobile Eggbert integration fix.** `visibility:hidden` removed SDL's exact canvas from browser hit testing while the SVG overlay intercepted pointer events, so the loaded game ignored input. The canvas now stays active at `opacity:0` and the SVG root is pointer-transparent. The same real-game run exposed two Firefox hot paths: alpha animation created/cached a distinct `feColorMatrix` filter for every premultiplied alpha step, and `Present()` reassigned unchanged root geometry every frame. `AlphaBlend` draw RGB is now converted back to straight colour, alpha-only fades use dirty-diffed `<g>` opacity with no SVG filter, filter keys contain RGB only, and unchanged clear colour/surface geometry causes no DOM writes. Host tests cover tint recovery/cache-key stability/mode isolation; browser smoke and pixel suites cover input-surface structure, filter-free opacity fades and the no-double-darkening pixel result. |
| SVGDOM-6 | ✅ | **Canvas-aligned pointer input.** The visible absolute-positioned SVG root used only the renderer-local presentation offset, so on a normal Emscripten shell it appeared near the page origin while SDL's transparent input canvas remained below the logo/status controls. Initially fixed by anchoring root geometry to the canvas's `offsetLeft`/`offsetTop`; SVGDOM-7 supersedes that implementation with a shared in-flow wrapper, preserving the verified rectangle alignment without a per-frame layout read. |
| SVGDOM-7 | ✅ | **Bounded real-game frame/memory cost.** Mobile Eggbert legitimately paints a full-screen background without calling `GraphicsDevice.Clear`; SVGDOM previously reset its retained-frame cursors only from `Clear`, so every frame appended another complete copy of every sprite and the DOM grew without bound. `Present()` now unconditionally establishes the next frame boundary while `Clear()` retains its required mid-frame erase semantics. Canvas and SVG now share an in-flow, positioned wrapper, eliminating the `offsetLeft`/`offsetTop` layout read that SVGDOM-6 performed after all sprite mutations. Texture variants are exposed to sprite `<image>` nodes through one short shared Blob URL (revoked on replacement/destruction) instead of repeating a full base64 atlas URI per node, and stable unused pool tails are removed after 180 presents. The browser smoke suite checks wrapper alignment, Blob URLs and `Present()`-without-`Clear` reset. |

---

## Known limitations (honest list)

1. **No backbuffer readback.** No browser API rasterizes a live SVG subtree synchronously. Render
   into a `RenderTarget2D` and read that, or use `CANVAS`.
2. **Out-of-bounds `Clamp` and mixed per-axis addressing throw** (SVGDOM-2) — implementable in
   principle, not yet built; a real follow-up, not a documented-permanent boundary.
3. **Custom `BlendState`s throw** — only the four standard presets exist in SVG/CSS compositing.
4. **No custom `Effect`s** — there is no shader stage.
5. **No MSAA, no depth, no stencil** — the same 2D-only boundary as `SDL_RENDERER`/`CANVAS`/`HTML_DOM`.
6. **No hierarchical camera/world `<g>` grouping** — a scene where only the camera moves still
   updates every visible sprite's own `transform` rather than a single shared parent transform. A
   real, valuable follow-up for large static scenes (tilemaps, mostly-static UI); not investigated
   for feasibility yet.
7. **The SVG-backbuffer path's own composited output has not been screenshot-diffed pixel-for-pixel
   beyond the SVGDOM-A ordering proof** — a real, narrower gap than the render-target path's
   exhaustive `GetData()`-based coverage (SVGDOM-B/D/E/F), not a blocked one; the tint/blend
   compositing itself is confirmed structurally applied in a live browser and algorithm-tested
   natively.
8. **Effectively one live, actively-driven `GraphicsDevice` per process** — the same
   `emscripten_set_main_loop` constraint every Emscripten CNA renderer shares, not specific to
   `SVG_DOM`. Genuine concurrent multi-window use was not exercised.

See `docs/svg-dom-renderer.md` for the full, currently-maintained capability status document (this
file records how the renderer came to be and its task history; that one records its current
verified state).
