# HTML Canvas 2D Graphics Backend — Implementation Plan

> **Status: APPROVED 2026-07-15 — owner explicitly authorized implementation on `feature/canvas`
> (worktree `../cnacanvas`, branched from `develop`).** Implementation proceeds phase by phase per
> this plan; update task statuses (✅/🟨/⬜) as they land. Emscripten SDK confirmed available and
> working in this dev environment (`emcc`/`em++` 6.0.2 via `$HOME/emsdk/emsdk_env.sh`), so unlike
> the original DRAFT's worst-case assumption, real Emscripten builds (not just structural review)
> are possible here — real browser pixel verification (Design decision 9) still is not.
>
> **Status legend** (matches `../cna`'s own convention): ✅ implemented *and verified against its
> stated acceptance criteria*; 🟨 code or documentation exists but has not met those criteria;
> ⬜ not implemented.
>
> **2026-07-15: all 8 phases (C1-C8) implemented and pushed to `feature/canvas`.** Every task is ✅
> or 🟨 — see `docs/canvas-backend.md` for the consolidated completeness status and its manual
> browser verification checklist (CANVAS-82) for what still needs a human + real browser. Nothing
> here has been pixel-verified (Design decision 9 — this dev loop has no real DOM/
> `CanvasRenderingContext2D` at all).
>
> **2026-07-15 (later same day): external code review found 5 real bugs, all fixed** (Opaque's
> `copy` clearing the whole canvas instead of just the sprite; `AlphaBlend`/`NonPremultiplied` wrongly
> treated as identical; RGB tint darkening/lightening semi-transparent edges via an algebra error;
> Wrap/Mirror validation gaps silently skipping the draw instead of throwing; the Mirror-tile cache
> never invalidating on `Texture2D::SetData`) plus a 6th (`Clear()` blending under a leftover
> composite mode and permanently resetting the transform). All verified by deriving the exact
> Porter-Duff/CSS-Compositing math by hand, not just re-asserted — see `docs/canvas-backend.md`'s
> "Bugs found in external review" section for the full detail. This is a real, useful correction to
> the "all 8 phases complete" framing above: implemented and structurally reviewed did **not** mean
> bug-free, and this dev loop's inability to pixel-test in a real browser is exactly why these 5
> slipped through initially.

---

## Why this backend, in the owner's own words

> "podívej se do ../cna a vytvoř v . /plan_canvas.md plán anglicky i s úkoly jak do CnA přidat nový
> backend canvas. bude to HTML canvas 2d only bez 3d podobné jako je SDL renderer v cna 2d only."
> (Look at `../cna` and create, in `./plan_canvas.md`, a plan in English with tasks for how to add
> a new "canvas" backend to CNA. It will be HTML canvas, 2D only, no 3D — similar to how the SDL
> renderer in CNA is 2D-only.)

So the comparison to `SDL_RENDERER` is specifically about **scope** (2D-only: `SpriteBatch`,
`Texture2D`, `SpriteFont`, `RenderTarget2D` used as plain sprite surfaces — no 3D pipeline), not
about platform reach. `SDL_RENDERER` runs on every desktop platform CNA supports; "HTML Canvas" is
a browser DOM API and can only exist inside an Emscripten/WebAssembly build running in an actual
browser. Confirmed with the owner (see Design decision 1): this backend is **Emscripten-only**,
gated the same hard way `plan_dx.md` gates `D3D11`/`D3D12` to Windows-only.

**How this differs from `EASYGL` on Emscripten** (the existing default backend there): `EASYGL`
gets a **WebGL2** context (`canvas.getContext('webgl2')`) and issues real GPU draw calls — it's a
full 3D pipeline that happens to run in a browser. This new `CANVAS` backend gets the browser's
**2D Canvas** context (`canvas.getContext('2d')`) instead and issues `fillRect`/`drawImage`-style
calls — no GPU shader pipeline exists on this path at all, by design, mirroring `SDL_RENDERER`'s
own "no 3D pipeline, no programmable shader stage, no depth/stencil, no MSAA" scope exactly (see
`../cna/docs/sdl-renderer-2d-completeness.md`). Two independent 2D-only backends already exist in
spirit (`SDL_RENDERER` native, `SOFTWARE` CPU-rasterizer-but-3D-capable) — this is CNA's first
**browser-native, GPU-free 2D** backend.

---

## Design decisions (recorded before implementation, confirmed with the owner)

1. **Emscripten-only; hard `FATAL_ERROR` gate outside it.** `CNA_GRAPHICS_BACKEND=CANVAS` fails
   configuration immediately when `NOT EMSCRIPTEN`, mirroring `plan_dx.md`'s Windows-only gate for
   `D3D11`/`D3D12` (`CMakeLists.txt`'s existing `if((CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR ...)
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows") message(FATAL_ERROR ...)` block — same shape, new
   condition). Native desktop builds keep using `EASYGL`/`SDL_RENDERER`/etc.; nothing about this
   plan changes any existing backend's behavior.

2. **Reuses the existing SDL-created window/canvas element — does not create a second one.**
   `GraphicsBackendCreateArgs::window` already carries a real `SDL_Window*`; on Emscripten, SDL3's
   own video driver already creates and owns exactly one DOM `<canvas>` element for that window
   (this is what `EASYGL` gets its WebGL2 context from today). `CANVAS` locates that *same* element
   via the identical JS lookup `EasyGLGraphicsBackend.cpp` already uses for its context-loss
   handling (`Module['canvas'] || document.querySelector('canvas')`) and calls
   `canvas.getContext('2d')` on it instead of `getContext('webgl2')` — no new windowing path, no
   second canvas element, no duplicate input/event-pump wiring. This also means `CANVAS`, unlike
   `HEADLESS`/`SOFTWARE`, needs a **real** SDL window (for input/event-pump purposes) exactly like
   `EASYGL` does — none of `GraphicsDevice`'s `#ifdef CNA_BACKEND_HEADLESS`-style "skip window
   creation" guards apply here.

3. **Every `Texture2D`/`RenderTarget2D` is backed by a private, off-screen `<canvas>` element,
   pixels pushed via synchronous `putImageData()` — not an `Image`/`ImageBitmap`.** XNA's
   `Texture2D.SetData`/`GetData`/`FromStream` and `RenderTarget2D` binding are all fully synchronous
   APIs, but the browser's usual image-decode paths (`Image.onload`, `createImageBitmap()`) are
   async `Promise`s that cannot be shoehorned into a synchronous C++ call without a real
   yield-and-resume mechanism (this project already hit an analogous wall with
   `SystemLink`/`Asyncify` in `CnaTests`' Emscripten build — see `CMakeLists.txt`'s
   `-sASYNCIFY=1` comment — a much bigger hammer than this backend should need). Confirmed with the
   owner: use a private off-screen `<canvas>` (`new OffscreenCanvas(w,h)` where available, falling
   back to `document.createElement('canvas')`) per texture/render target; `SetData`/`FromStream`
   (after CPU-side decode, same as every other backend already does) push pixels via
   `ctx.putImageData(imageData, 0, 0)`, which is genuinely synchronous. Sampling elsewhere is
   `ctx.drawImage(thatOffscreenCanvasElement, sx, sy, sw, sh, dx, dy, dw, dh)` — also synchronous.
   `RenderTarget2D` binding just means "draw calls target this canvas's 2D context instead of the
   main one"; reading it back (`GetBackBufferData`/`ReadBackbuffer`) is `ctx.getImageData(...)`,
   itself synchronous. This one decision is what makes the rest of the backend tractable without
   inventing a new async/threading model.

4. **2D-only scope mirrors `SDL_RENDERER`'s already-audited scope, feature for feature.** Reuse the
   `ThrowNo3D`-style convention verbatim (see `SdlGraphicsBackend.cpp`'s `ThrowNo3D()` helper) for
   every genuinely-3D-only `IGraphicsBackend` entry point: `ClearColorAndDepth`/`ClearDepth`/
   `ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`,
   `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, `CreateVertexBuffer`/
   `CreateIndexBuffer16`, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, and the
   `*Ex`/instanced variants. `SupportsDepthStencil()` returns `false` (same as `SDL_RENDERER`), so
   `GraphicsDevice::Clear(ClearOptions, ...)` masks Depth/Stencil out before ever reaching this
   backend. No MSAA (`GetMultiSampleCount()` stays 0). Do not re-derive these decisions from
   scratch — copy `docs/sdl-renderer-2d-completeness.md`'s already-settled answers wherever the
   underlying limitation (no GPU, no shader stage, no depth buffer) is identical; only *add* new
   decisions where Canvas2D's actual capabilities genuinely differ from SDL3's (see 5–6 below).

5. **Blend mapping targets `globalCompositeOperation`, a much smaller vocabulary than SDL's
   `SDL_ComposeCustomBlendMode`.** Canvas2D has no generic blend-factor/blend-equation model — only
   a fixed list of named composite operations. Real, honest support: `BlendState::Opaque`
   (`source-over` + always-1.0 destination alpha, or an explicit clear-then-draw to guarantee full
   overwrite), `BlendState::AlphaBlend` (`source-over`, requires converting CNA's premultiplied
   source data to straight alpha first since Canvas2D always composites as straight-alpha —
   `SDL_RENDERER` had no such conversion to do, this is a genuinely new wrinkle), `NonPremultiplied`
   (`source-over` directly, no conversion needed), and `Additive` (`lighter`). Every other custom
   `BlendState` (arbitrary `Blend`/`BlendFunction` combinations) throws rather than guessing at a
   `globalCompositeOperation` that doesn't actually match — a real, narrower scope than
   `SDL_RENDERER`'s (which could at least approximate `Subtract`/`Min`/`Max` via
   `SDL_ComposeCustomBlendMode` on some drivers), and must be recorded as such, not silently
   claimed equivalent.

6. **`TextureAddressMode::Wrap` may finally be real here** — `SDL_RENDERER` left this
   **⛔ BLOCKED** (`docs/sdl-renderer-2d-completeness.md` §11, Tasks 686/687) because
   `SDL_RenderTexture` has no native tiling mode. Canvas2D's `ctx.createPattern(source, 'repeat')`
   genuinely tiles, so a `SpriteBatch::Draw()` under `TextureAddressMode::Wrap` can use a
   pattern-fill path instead of `drawImage` when address mode isn't `Clamp`. `Mirror` has no
   matching native repeat-mode (`createPattern` only offers `repeat`/`repeat-x`/`repeat-y`/
   `no-repeat`, none of which flip alternate tiles) — investigate a manual 2×-tile pre-composited
   pattern source as a real implementation before defaulting to throw; record whichever outcome is
   reached honestly (this is exactly the shape of decision `SDL_RENDERER`'s own audit flagged
   ⛔ BLOCKED for owner input — don't silently guess either way here, but do try the pattern-source
   trick first since it's cheap to attempt).

7. **All JS interop via `EM_JS`, following the exact style already established in
   `EasyGLGraphicsBackend.cpp`** (`CNA_DebugLoseWebGLContext`/`CNA_DebugRestoreWebGLContext`) — not
   a new interop convention. Pixel data crosses the boundary via `HEAPU8` pointer + length (already
   proven necessary for uploading pixels from C++ into a JS-visible buffer elsewhere in this
   codebase); no `embind`, no new build-time JS-glue generation step.

8. **CMake integration mirrors `HEADLESS`/`SOFTWARE`'s own `elseif()` block exactly.** Add
   `"CANVAS"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property, a matching `CNA_BACKEND_CANVAS`
   option, a `cna_backend_graphics_canvas` static library target under
   `src/CNA/Internal/Backends/Canvas/`, and `add_compile_definitions(CNA_BACKEND_CANVAS)` — the
   same four-line shape every other backend already follows in `CMakeLists.txt`.

9. **Testing: this dev environment cannot execute real Canvas2D pixel output at all — verification
   here is structural only; real pixel correctness is `needs_human` (browser required).** Confirmed
   with the owner. `CnaTests` under Emscripten already runs via plain `node CnaTests.js`
   (`CMakeLists.txt`'s `-sEXIT_RUNTIME=1`/`-sASYNCIFY=1` comments confirm this), and Node has no
   `document`, no `<canvas>`, no `CanvasRenderingContext2D` — there is no headless-browser harness
   in this repo today (`grep` for `puppeteer`/`playwright`/`chromium` across `CMakeLists.txt` and
   `scripts/` found nothing). What *can* be verified in this environment, honestly: the backend
   configures and builds cleanly under `-DCNA_GRAPHICS_BACKEND=CANVAS` targeting Emscripten,
   argument validation / `ThrowNo3D` coverage / non-JS-touching logic (e.g. blend-mode→
   `globalCompositeOperation` string mapping as a pure function, pivot/flip transform math as pure
   math) via ordinary `CnaTests` GTest cases that don't need a real 2D context, and that the
   generated `EM_JS` snippets are syntactically sound JS (can be lint-checked without a browser).
   Real pixel-correctness verification (analogous to `docs/sdl-renderer-2d-completeness.md`'s
   whole table) requires a human running an actual build in a real browser — record this
   explicitly per task, the same honest way `plan_dx.md` records `DX-90`/`DX-91`/`DX-114` as
   `needs_human`, rather than silently claiming automated coverage that doesn't exist. If a
   headless-browser harness (Puppeteer/Playwright) becomes valuable later, that's new tooling +
   a new third-party dependency and needs its own separate owner approval (`CLAUDE.md`-equivalent
   gate in `../cna`) — out of scope for this plan.

10. **Custom `Effect` (shader source) execution: throws**, same as `SDL_RENDERER` — Canvas2D has no
    programmable shader stage of any kind, so silently ignoring a custom `Effect` would misrender
    with no error (`CreateEffectBackend` returns `nullptr`, `SpriteBatch::Begin(effect)` with a
    non-null custom effect throws; a `nullptr` effect, the common case, is unaffected).

11. **Occlusion queries: `CreateOcclusionQuery()` returns `nullptr`** (the `IGraphicsBackend`
    default) — Canvas2D has no hardware query mechanism, and unlike `HEADLESS`'s "track everything
    for diagnostics" philosophy, there's no meaningful CPU-side occlusion approximation worth
    building for a browser-sprite backend. Matches several existing backends' own defaults.

---

## Active execution order — do this one phase at a time

1. Phase C1 (CMake integration + skeleton) unblocks everything else, exactly as `plan_headless.md`
   Phase N1 and `plan_software.md` Phase S1 did for their backends.
2. Phase C2 (canvas element acquisition + 2D context) must land before anything else touches JS —
   get `Clear()`/`Present()` working against the real DOM canvas first, verified structurally, before
   building texture/draw machinery on top of an unproven JS bridge.
3. Phase C3 (texture/render-target backends: offscreen canvas + `putImageData`) is the architectural
   core (Design decision 3) — every later phase depends on it.
4. Phase C4 (`SpriteBatch`/`Draw` path: `drawImage`, transform/rotation/origin/flip) is the actual
   point of this backend, same as `plan_software.md` Phase S6 was for `SOFTWARE` — verify
   continuously against Phase C3, not left to the end.
5. Phase C5 (blend/sampler-state mapping) builds directly on C4's draw path.
6. Phase C6 (`SpriteFont`) should fall out of C3+C4 almost for free (a `SpriteFont` glyph draw is
   just another textured-quad `Draw()` call against a bitmap-font atlas texture) — confirm this
   rather than assume it, the same way `plan_software.md` design decision 5 called out reusing the
   same rasterizer core for `SpriteBatch`.
7. Phase C7 (`ThrowNo3D` wiring across the full 3D surface + custom-`Effect`/occlusion-query
   defaults) can happen any time after C1, but must be complete before this backend is considered
   feature-complete — don't let unimplemented 3D entry points silently fall through to a base-class
   no-op instead of throwing.
8. Phase C8 (tests + `docs/canvas-backend.md`) — per this family of repos' convention, add test
   coverage in the same task that implements each capability, not bolted on afterward; write the
   completeness doc (mirroring `docs/sdl-renderer-2d-completeness.md`'s table/status-legend
   structure) as capabilities land.

For every task: build the affected target(s) (`-DCNA_GRAPHICS_BACKEND=CANVAS` under an Emscripten
toolchain), run whatever of `CnaTests` can run under `node` (Design decision 9), and do not mark a
task ✅ without both — but do not claim pixel-level ✅ verification that Design decision 9 says is
`needs_human`.

---

## Phase C1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-1 | Add `"CANVAS"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_CANVAS` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/.../`SOFTWARE`/`D3D11`/`D3D12` | ✅ | |
| CANVAS-2 | Hard platform gate: `if(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS" AND NOT EMSCRIPTEN) message(FATAL_ERROR ...)`, mirroring `plan_dx.md`'s D3D11/D3D12 Windows-only gate (Design decision 1) | ✅ | |
| CANVAS-3 | `cna_backend_graphics_canvas` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")` block, mirrors `HEADLESS`'s/`SOFTWARE`'s own) | ✅ | |
| CANVAS-4 | `include/CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp` + `src/CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.cpp`: class implementing every `IGraphicsBackend` pure virtual — real where C1 can make it real (nothing yet but construction/destruction), honest throwing stubs elsewhere until later phases replace them (same bring-up strategy `HEADLESS-3`/`SOFTWARE-3` used) | ✅ | |
| CANVAS-5 | Factory dispatch for `CANVAS` in `CreateGraphicsBackend()` | ✅ | |
| CANVAS-6 | Confirm `CnaTests` (the full pre-existing GTest corpus) links cleanly against the new backend target under an Emscripten configure — same interface-completeness smoke check `HEADLESS-3`/`SOFTWARE-2` used, minus actually running it under `node` for anything that needs a real canvas | ✅ | Real `emcmake`/`emcc` 6.0.2 configure+build; `CnaTests.js` links and a backend-agnostic suite (`RectangleTest.*`, 35/35) genuinely passes under `node`. Along the way, fixed a pre-existing `sharp-runtime` bug blocking *any* Emscripten build (`FileSystemWatcher.hpp` declared 3 inotify fields unconditionally but only used them under `__linux__`, which `emcc` doesn't define) — owner-approved, committed in `sharp-runtime` (not pushed). |

## Phase C2 — Canvas element acquisition and 2D context

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-10 | `EM_JS` function to locate the existing DOM `<canvas>` element (`Module['canvas'] \|\| document.querySelector('canvas')`, same lookup `EasyGLGraphicsBackend.cpp` already uses) and call `getContext('2d')` on it, caching the returned `CanvasRenderingContext2D` on the JS side (e.g. keyed on the canvas element, mirroring how `EASYGL` caches its GL context) | ✅ | `CNA_Canvas2D_EnsureContext()`, caches on `Module['cnaCtx2d']`; called from other Canvas2D `EM_JS` functions directly (confirmed `EM_JS`-to-`EM_JS` calls work under emcc 6.0.2). |
| CANVAS-11 | `Clear(r,g,b,a)`: real `ctx.fillStyle` + `ctx.fillRect(0,0,w,h)` (or `ctx.clearRect` for alpha=0, matching FNA's `Clear` semantics) against the currently-bound context (main canvas or a bound render-target's offscreen canvas — see Phase C3) | ✅ | **Fixed in external review (2026-07-15)**: originally used a plain `fillRect` under whatever `globalCompositeOperation` a previous `SpriteBatch` draw left active (blending instead of an exact overwrite) and permanently reset the transform via `setTransform(identity)` (corrupting an active `SpriteBatch` transform if `Clear()` is called mid-`Begin()`/`End()`). Fixed via `ctx.save()`/explicit `globalCompositeOperation='copy'`/`ctx.restore()`. |
| CANVAS-12 | `Present()`: no-op — the browser compositor presents the canvas automatically on the next paint tick; nothing for CNA to do (unlike `SOFTWARE`'s "no-op because no window", this is "no-op because the DOM already handles it") | ✅ | |
| CANVAS-13 | `GetViewportSize()`/`SetVirtualResolution()`/`SetPresentationMode()`: reuse the same logical-resolution/letterbox math every other backend already shares (backend-agnostic per `IGraphicsBackend`'s own doc comments) — only the actual canvas-element width/height attributes are backend-specific here | ✅ | Verbatim port of `EasyGLGraphicsBackend`'s `getLogicalSize`/`TransformWindowToLogical`/`TransformLogicalToWindow` against `SDL_GetWindowSize(window_,...)` — SDL3 keeps the DOM canvas's width/height attributes in sync with the window it backs, so no extra JS query is needed. |
| CANVAS-14 | `GetWindowInternal()`/`GetRendererInternal()`: return the real `SDL_Window*` from `GraphicsBackendCreateArgs`; `GetRendererInternal()` returns `nullptr` (no `SDL_Renderer*` exists on this backend, same as `EASYGL`) | ✅ | Landed in C1 already. |
| CANVAS-15 | Structural smoke check (`Canvas_Smoke`-equivalent, whatever of it can run under `node` without a real DOM — likely just "configures and builds", see Design decision 9) | 🟨 | `examples/canvas_smoke_test.cpp` + `cna_test_canvas_smoke` target confirmed to configure/build/link. Empirically confirmed (not just assumed) that running it under `node` crashes at `SDL_Init(SDL_INIT_VIDEO)` itself ("window is not defined"), before any Canvas-specific code runs — so it is intentionally *not* registered as a `ctest` (mirrors `cna_diag_software`/`d3d12_swapchain_diag`'s "real executable, not a ctest" precedent). needs_human / a real browser (`emrun`) to actually PASS. |

## Phase C3 — Texture and render-target backends (offscreen canvas + `putImageData`)

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-20 | `CanvasTextureBackend : ITextureBackend` — owns a private off-screen `<canvas>` (`OffscreenCanvas` where available, else a detached `document.createElement('canvas')`) sized to the texture; `UpdatePixels`/constructor-from-`ImageData` push via `putImageData` | ✅ | Registered by integer id in `Module['cnaTextures']` (not keyed on the canvas element itself, since C++ can't hold a JS object handle directly). |
| CANVAS-21 | `UpdatePixelsLevel` (mip level support): decide the same way `SDL_RENDERER` did (Task 681, `docs/sdl-renderer-2d-completeness.md` §2) — Canvas2D has no native mip chain either; level>0 `SetData` likely throws, level=0 unaffected. Record the decision, don't silently drop it | ✅ | Same decision as SDL_RENDERER: level>0 throws with an explanatory message, level=0 delegates to `UpdatePixels`. |
| CANVAS-22 | `CanvasRenderTargetBackend : IRenderTargetBackend` — same off-screen-canvas mechanism as `CanvasTextureBackend`, plus `BindAsRenderTarget()`/`UnbindAsRenderTarget()` that switch which `CanvasRenderingContext2D` subsequent draw calls target | ✅ | Bind sets `Module['cnaCurrentCtx']` to this target's context; Unbind is a genuine no-op (Bind is idempotent/absolute, not incremental — see the file's own comment for why this differs from EasyGL's non-trivial Unbind). |
| CANVAS-23 | `HasRealDepthBuffer()` override → always `false` (no depth buffer exists on any Canvas2D target, same as `SDL_RENDERER`'s override for the same reason — Task 708's precedent) | ✅ | |
| CANVAS-24 | `RenderTargetUsage::DiscardContents` vs `PreserveContents`: `DiscardContents` clears the offscreen canvas to black on bind, `PreserveContents` leaves it untouched — same observable contract `SDL_RENDERER` Task 706 already proved correct there | ✅ | Confirmed this is a framework-layer concern (`GraphicsDevice.cpp`/`RenderTarget2D`), not something any backend's `IRenderTargetBackend` implements itself — zero backend-specific code needed, same as `SDL_RENDERER`. |
| CANVAS-25 | `ReadBackbuffer()`/`GetBackBufferData()`: real `ctx.getImageData(x,y,w,h)` against the currently-bound context (main canvas or bound render target) — genuinely synchronous, no faking needed (Design decision 3's whole point) | ✅ | |
| CANVAS-26 | `SetRenderTargets` with 2+ bindings (MRT): throw — a Canvas2D context is inherently single-target, same conclusion `SDL_RENDERER` Task 709 reached and for the same underlying reason | ✅ | |
| CANVAS-27 | `Texture2D::FromStream` (PNG/JPEG/etc. decode): decode happens on the existing CPU-side path shared with every other backend (unrelated to this backend specifically) before reaching `CanvasTextureBackend`'s `putImageData` upload — confirm no backend-specific async decode is introduced here | ✅ | Confirmed by reading `Texture2D.cpp`: both `FromStream` overloads decode via `DecodeStreamToImageData` (DxtUtil/SDL3_image, fully backend-agnostic) before ever reaching `CreateTexture()`/`SDL_CreateSurfaceFrom`'s CPU-side zoom-resize math — no backend-specific or async decode introduced. |

## Phase C4 — `SpriteBatch`/`Draw` path

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-30 | `CanvasSpriteBatchBackend : ISpriteBatchBackend` skeleton; `Begin()`/`End()` — `End()` needs no explicit flush (each `Draw()` can paint immediately since Canvas2D has no command-buffer batching concept to defer) | ✅ | `End()` also resets `ctx.setTransform` to identity (belt and suspenders alongside `Clear()`'s own defensive reset — see CANVAS-36). |
| CANVAS-31 | Basic `Draw(texture, x, y)` and `Draw(texture, destRect, srcRect, color)` overloads via `ctx.drawImage(sourceCanvas, sx, sy, sw, sh, dx, dy, dw, dh)` | ✅ | Both route through one shared `CNA_Canvas2D_DrawSprite` JS function (see CANVAS-35). |
| CANVAS-32 | Color tint (`Draw`'s `color` param): Canvas2D has no per-draw color-multiply blend natively — needs an explicit tint pass (e.g. draw into a scratch off-screen canvas with `globalCompositeOperation='multiply'`/`'source-atop'` composited against a solid fill, then blit that result) OR accept `color=White` as the only cheap fast-path and implement general tinting via the scratch-canvas trick for anything else — investigate both `SDL_RENDERER`'s (`SDL_SetTextureColorMod`, trivial there) and Canvas2D's real options here; this is a real, non-trivial gap `SDL_RENDERER` didn't have (Task 678's SDL precedent had a native color-mod primitive; Canvas2D doesn't) | ✅ | **Corrected in external review (2026-07-15)**: the original `multiply` fill + `destination-in` scratch-canvas trick was mathematically wrong (verified algebraically against the CSS Compositing spec's blend-mode formula — it produced `Rt*(1-As*(1-Rs))` instead of `Rt*Rs`, a real dark/light-fringing bug at semi-transparent, non-white edge pixels). Replaced with a direct, exact per-pixel `getImageData`/`putImageData` RGB multiply that leaves alpha completely untouched. Alpha is still `ctx.globalAlpha` (free); the per-pixel pass is skipped entirely for `Color.White`. |
| CANVAS-33 | Rotation around `origin`: `ctx.translate(dstX, dstY); ctx.rotate(rotation); ctx.drawImage(..., -origin.X*scale, -origin.Y*scale, ...)` — get the pivot math right against the same known-correct formula `SDL_RENDERER` Task 671 had to fix after finding it wrong, rather than re-deriving and re-discovering the same bug | ✅ | Derived and verified algebraically against the same FNA `GenerateVertexInfo` placement `SDL_RENDERER` Task 671 targets: origin (source-pixel space) maps exactly to `(destX,destY)` invariant under rotation. |
| CANVAS-34 | `SpriteEffects::FlipHorizontally`/`FlipVertically`: `ctx.scale(-1,1)`/`ctx.scale(1,-1)` composed with the same transform stack as rotation/origin above | ✅ | A naive `ctx.scale(-1,1)` mirrors around the *pivot*, not the sprite's own footprint, which is wrong whenever `origin` isn't the rect's center — implemented as translate/scale/translate around the rect's own local center instead, matching real XNA/FNA semantics (flip changes which source corner maps to which *unchanged* destination corner). |
| CANVAS-35 | Scalar / `Vector2` scale overloads | ✅ | Confirmed no separate backend code needed: `SpriteBatch.cpp` (shared, backend-agnostic) converts every scale overload into the same `(destinationRectangle, sourceRectangle, ...)` call this backend already handles. |
| CANVAS-36 | `SetTransformMatrix()` (the `Begin(transformMatrix)` parameter): `ctx.setTransform(a,b,c,d,e,f)` directly supports a full 2D affine matrix — **simpler than `SDL_RENDERER`'s own fix** (Task 675 needed a special non-Identity-only code path via `SDL_RenderTextureAffine`; Canvas2D can just always call `setTransform` per `Begin()`, Identity included, with no separate code path needed) | ✅ | Row-major XNA `Matrix` → `setTransform(M11,M12,M21,M22,M41,M42)`. Sets the *baseline* transform; each `Draw()`'s own `save()`/relative-transform/`restore()` composes on top, matching FNA's real vertex-pipeline order (sprite transform, then camera/world transform). |
| CANVAS-37 | `SpriteSortMode` (`Deferred`/`Texture`/`FrontToBack`/`BackToFront`/`Immediate`): confirm this is already fully handled by shared, backend-agnostic `SpriteBatch` code (same as `SDL_RENDERER`'s Task 677 finding for `Begin`/`End` sequencing) — this backend should need no sort-mode-specific code at all | ✅ | Confirmed structurally: `ISpriteBatchBackend`'s `Begin()` takes no sort-mode parameter at all — sorting/buffering (if any) happens entirely in shared `SpriteBatch.cpp` before it calls this backend's `Draw()`, same conclusion `SDL_RENDERER` reached. |
| CANVAS-38 | Custom `Effect` via `Begin(effect)`: throws for non-null custom effects (Design decision 10) | ✅ | |

## Phase C5 — Blend and sampler state mapping

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-40 | `ApplyBlendState` → `globalCompositeOperation` mapping for `Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive`; throw for every other custom `BlendState` (Design decision 5) | ✅ | `Opaque`→`copy`, `AlphaBlend`/`NonPremultiplied`→`source-over`, `Additive`→`lighter`; matched via the raw `(colorSrcBlend,colorDstBlend,colorBlendFunc)` tuple, requiring `alpha*` fields to match their `color*` counterparts and `BlendFunction::Add` — anything else throws. **`Opaque`'s `copy` needed a clip fix** — see CANVAS-11-adjacent note in §3 below and `docs/canvas-backend.md`. |
| CANVAS-41 | Premultiplied→straight-alpha conversion needed specifically for `AlphaBlend` (Design decision 5) — a genuinely new piece of logic `SDL_RENDERER` never needed | ✅ | **Corrected in external review (2026-07-15)**: an initial version concluded no conversion was needed (reasoning: this backend never produces genuinely premultiplied pixel data anywhere). That reasoning doesn't hold project-wide — `SDL_RENDERER` has a dedicated pixel test (Task 697) that deliberately constructs premultiplied source data specifically to verify `AlphaBlend`, so a game/test *can* legitimately feed premultiplied data through this backend too. Implemented for real: `AlphaBlend` (only) now un-premultiplies (divides RGB by alpha) the source pixels via a per-pixel `getImageData` pass before Canvas2D's own internal premultiply-before-composite step re-derives the correct premultiplied value for `source-over`. `AlphaBlend`/`NonPremultiplied` now map to *different* `CanvasCompositeOp` enum values (`AlphaBlendSourceOver`/`NonPremultipliedSourceOver`) even though both drive the same `globalCompositeOperation` string, specifically so this distinction survives. |
| CANVAS-42 | `ApplySamplerState`'s `TextureFilter` → `ctx.imageSmoothingEnabled`/`ctx.imageSmoothingQuality`: Canvas2D only has a binary smoothing toggle (plus a 3-level quality hint), coarser than SDL's `SDL_ScaleMode` — `Point`/`Nearest`-family values map to `imageSmoothingEnabled=false`, everything else to `true` (mirrors the *shape* of `SDL_RENDERER` Task 701's magnification-only fix, but starting from a coarser native primitive) | ✅ | Implemented on `ISpriteBatchBackend::SetSamplerFilter` (mirroring `SDL_RENDERER`'s own precedent — `IGraphicsBackend::ApplySamplerState` itself is left as the shared no-op default, same as `SDL_RENDERER`), using the identical magnification-family value grouping Task 701 derived. `imageSmoothingQuality` not set (Canvas2D's `low`/`medium`/`high` hint has no natural `TextureFilter` counterpart to derive it from). |
| CANVAS-43 | `TextureAddressMode::Clamp`: verify this is already the natural behavior of plain `drawImage` (a fixed source rect never samples outside itself) — likely ✅ with zero extra code, unlike `SDL_RENDERER`'s own ⚠️-emulated status for the same mode (Task 685) | ✅ | **Revises the DRAFT's guess**: implemented as a real, explicit clamp of the source rect into the texture's own bounds before `drawImage`, rather than relying on `drawImage`'s native out-of-bounds behavior — which this dev loop cannot verify in a real browser at all (Design decision 9), so "assume it's already correct" would have been exactly the kind of silent guess the plan says not to make. The explicit clamp is unconditionally correct regardless of what any given browser's native behavior turns out to be. |
| CANVAS-44 | `TextureAddressMode::Wrap` via `ctx.createPattern(source, 'repeat')`; investigate `Mirror` via a manually pre-composited 2×-tile pattern source before deciding to throw (Design decision 6) | 🟨 | Both implemented for the case that actually distinguishes them from Clamp (`sourceRectangle` exceeding the texture's own bounds): `Wrap` via `createPattern(...,'repeat')`; `Mirror` via a lazily-built, cached 2×2 pre-tiled mirrored canvas as the pattern source (no native mirror-repeat exists), cache invalidated on both `Texture2D::SetData` and `RenderTarget2D::BindAsRenderTarget()` (**fixed in external review, in two passes** — first fixed for `SetData`; a follow-up review round then caught that a bound render target's pixels can also change via direct `Clear()`/`Draw()` calls made against it while bound, never going through `UpdatePixels` at all, so `BindAsRenderTarget()` now also invalidates the cache conservatively on every bind). Both fill via `ctx.fillRect()` under the same transform stack as the plain `drawImage` path, clipped to the exact drawn rect (**fixed in external review** alongside the `Opaque`/`copy` clip bug — same root cause), so rotation/flip apply for free. **Narrower than full generality, by design, not oversight**: mixed per-axis modes (`addressU != addressV`), a tinted draw, and an `AlphaBlend` draw needing un-premultiply of the tiled pattern source, all combined with an out-of-bounds Wrap/Mirror `sourceRectangle`, throw rather than guess — **fixed in external review** to be real `std::runtime_error`s thrown from C++ (`ValidateAddressModeCombination`), not a silently-skipped draw (an earlier version only `console.error`'d and returned). **Still unverified in a real browser** (Design decision 9 — this dev loop has no real `CanvasRenderingContext2D`/DOM at all, confirmed empirically in Phase C1); flagging for owner review/real-browser testing before considering this fully closed. |

## Phase C6 — `SpriteFont`

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-50 | Single glyph at a known position/size — confirm this needs no backend-specific code beyond Phase C4's `Draw()` path (a glyph is just a textured-quad draw against a bitmap-font atlas texture) | ✅ | Confirmed by reading `SpriteBatch::DrawString`/`pushSprite`: every glyph funnels through the exact same `Texture2D`+`Rectangle`+`Color`+rotation/origin/effects queueing path a plain `SpriteBatch::Draw()` call uses, which flushes through the same backend `Draw()` overload Phase C4 already implements. Zero Canvas-specific code needed. |
| CANVAS-51 | Multiple glyphs with spacing/kerning | ✅ | `curOffset`/kerning-table math lives entirely in shared `SpriteBatch.cpp` (`spriteFont.spacing_`/`kerning_[index]`), same finding `SDL_RENDERER` Task 691 reached. |
| CANVAS-52 | `\n` newline advance | ✅ | Handled in shared `SpriteBatch.cpp` (`curOffset.Y += lineSpacing_`) before any backend call. |
| CANVAS-53 | Unknown-character fallback (`defaultCharacter`) | ✅ | Handled in shared `SpriteBatch.cpp` (falls back to `characterIndexMap_[defaultCharacter_]`, throws if unset) before any backend call. |
| CANVAS-54 | `SpriteEffects` flip + rotation/origin/scale with `DrawString` | ✅ | The `axisDirX`/`axisDirY`/`axisIsMirroredX`/`axisIsMirroredY` glyph-order-mirroring fix (`SDL_RENDERER` Task 694) lives entirely in shared `SpriteBatch.cpp` — confirmed already active for every backend, no re-fix needed; per-glyph flip/rotation/origin reaches this backend through the identical `Draw()` overload Phase C4's rotation/flip math already handles correctly. |

## Phase C7 — `ThrowNo3D` wiring and remaining defaults

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-60 | `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` → `ThrowNo3D` | ✅ | Landed in Phase C1 already. |
| CANVAS-61 | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` → `ThrowNo3D` | ✅ | Landed in Phase C1 already. |
| CANVAS-62 | `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateIndexBuffer32` → `ThrowNo3D` | ✅ | `CreateVertexBuffer`/`CreateIndexBuffer16` landed in C1. `CreateIndexBuffer32` needs no override at all: `IGraphicsBackend`'s own shared default delegates to `CreateIndexBuffer16()`, which already throws — confirmed this reaches the same `ThrowNo3D` message via inheritance, matching `SDL_RENDERER`'s own precedent of not overriding it either. |
| CANVAS-63 | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` → `ThrowNo3D` | ✅ | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` landed in C1. `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` need no override: the shared default falls back to the (already-throwing) colored-primitives methods. `DrawInstancedPrimitivesEx` needs no override either: `IGraphicsBackend`'s own shared default already throws unconditionally for every backend that doesn't implement instancing. Confirmed via inheritance, matching `SDL_RENDERER`'s own precedent of not overriding any of the three. |
| CANVAS-64 | `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` → return `nullptr` (matches the `IGraphicsBackend` default; no 3D-only resource types on a 2D-only backend) | ✅ | No override needed — confirmed via inheritance, matching `SDL_RENDERER`'s own precedent of not overriding any of the three. |
| CANVAS-65 | `SupportsDepthStencil()` → `false` | ✅ | The one method in this phase that genuinely needed a new override: the shared default is `true` ("most backends are 3D-capable"), which is wrong for a 2D-only backend. |
| CANVAS-66 | `CreateOcclusionQuery()` → `nullptr` (Design decision 11) | ✅ | No override needed — matches the shared `IGraphicsBackend` default directly (a deliberate, already-recorded choice, note this *differs* from `SDL_RENDERER`'s own choice to override this and throw instead — Design decision 11 is Canvas-specific, not "just inherit and forget"). |
| CANVAS-67 | `CreateEffectBackend()` → `nullptr`; confirm `SpriteBatch::Begin(effect)` throws for non-null custom effects (ties back to CANVAS-38) | ✅ | `CreateEffectBackend()` needs no override (shared default). `SpriteBatch::Begin(effect)` throwing confirmed already working via `CanvasSpriteBatchBackend::SetCustomEffect` (Phase C4/CANVAS-38). |
| CANVAS-68 | `TransformWindowToLogical`/`TransformLogicalToWindow`: implement for real (needed for correct mouse-coordinate mapping under letterboxing, same as every other windowed backend) | ✅ | Landed in Phase C2 already. |
| CANVAS-69 | `DebugSimulateContextLoss`/`DebugRestoreContext`: likely no-op (a 2D canvas context doesn't have WebGL's context-loss failure mode) — confirm rather than assume, since browsers *can* lose canvas 2D contexts in extreme low-memory conditions on mobile | ✅ | Confirmed, not just assumed: unlike WebGL's `WEBGL_lose_context` extension, Canvas2D has no analogous DOM API/event a program can use to *force* or *listen for* a context loss on demand, so there is nothing implementable here to simulate — the shared no-op default is the correct, considered answer, not a gap. A real (unsimulatable) mobile low-memory context loss would surface as `Module['cnaMainCtx']` silently going stale; out of scope for v1 the same way the plan's own Boundaries section already excludes exotic recovery paths. |

## Phase C8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| CANVAS-80 | Structural GTest coverage for everything that doesn't need a real `CanvasRenderingContext2D`: `ThrowNo3D` coverage, blend-mode→`globalCompositeOperation` string mapping as a pure function, pivot/flip/scale transform math as pure math, CMake configure/build check | 🟨 | `tests/CNA/Internal/Backends/Canvas/CanvasGraphicsBackendTests.cpp`: 14 tests, all passing under `node CnaTests.js` (`ThrowNo3D` coverage via a fake-but-never-dereferenced `SDL_Window*` sentinel; `BlendStateToCompositeOp` and `ValidateAddressModeCombination` both extracted as standalone pure functions specifically so they're unit-testable — the latter added during the external-review fix pass, CANVAS-44). Also fixed a real, pre-existing `GraphicsBackendCompileDefinitionTests.cpp` failure this configure exposed (`CNA_BACKEND_CANVAS` was missing from its backend-count `#ifdef` chain). **Not fully ✅**: "pivot/flip/scale transform math as pure math" has no C++-side equivalent to unit test — this backend's rotation/flip/scale composition is expressed entirely as Canvas2D transform-stack calls (`ctx.translate`/`rotate`/`scale`) executed in JS, not standalone C++ arithmetic the way `SDL_RENDERER`'s `sdlCenterX`/`sdlCenterY` was; relies on CANVAS-82's manual checklist instead. The 5 bugs external review found (see `docs/canvas-backend.md`) were all in this untested JS logic, not in anything the original GTest suite covered — a caution about what "structural coverage passing" does and doesn't prove. |
| CANVAS-81 | `docs/canvas-backend.md`: mirror `docs/sdl-renderer-2d-completeness.md`'s table/status-legend structure, one section per feature area (`SpriteBatch`, `Texture2D`, `SpriteFont`, `BlendState`, `SamplerState`, `RenderTarget2D`, `Viewport`/`PresentationParameters`) | ✅ | Written; includes a "Known findings that revise the original DRAFT plan" section covering CANVAS-41/44. |
| CANVAS-82 | Manual browser verification checklist (`needs_human`): a short, repeatable list of things a human should visually/pixel-check in a real browser (basic sprite draw, rotation/origin pivot, flip, blend modes, render-target round-trip, `SpriteFont` text, wrap/mirror addressing) — the closest honest equivalent to `docs/sdl-renderer-2d-completeness.md`'s table, but explicitly marked as not automatable in this dev environment | ✅ | 10-item checklist in `docs/canvas-backend.md`; none of it checked off yet (needs a human + real browser). |
| CANVAS-83 | Update `../cna/plan.md`/`README.md`/`CMakeLists.txt`'s help text (`CNA_GRAPHICS_BACKEND` STRINGS docstring) to list `CANVAS` alongside the other 9 backends | ✅ | `CMakeLists.txt` done in Phase C1 already. `README.md`: added a `CANVAS` bullet to Project Status (matching `D3D11`/`D3D12`'s style) and completed the previously-stale simple backend list (only had 4 of 9 pre-existing backends; now lists all 10). `docs/README.md`: added `canvas-backend.md` to both the "Start here" and "Platform / backend limitations" sections. `plan.md` (repo root): no backend list exists there at all — nothing to update. Also added a small, scoped correction note to `docs/web-emscripten-graphics-limitations.md` (its "no Emscripten build has ever succeeded" claim is now out of date because of this work, though only for the tooling premise — its own EasyGL/WebGL2-specific claims are untouched). |
| CANVAS-84 | Normalize the premultiplied `AlphaBlend` draw colour before Canvas2D's split RGB-tint + `globalAlpha` path | ✅ | Fixed the draw-colour counterpart of CANVAS-41. Source pixels were already converted from premultiplied to straight, but the draw tint was not: `Color::FromNonPremultiplied(..., alpha)` RGB was multiplied into those straight pixels and alpha was then applied again via `globalAlpha`, causing the same double-darkening seen in HTML DOM. A pure C++ helper now recovers straight RGB only for `AlphaBlend`, canonicalizes zero-alpha RGB to white, and leaves all other blend modes byte-exact. The normalized colour drives both the JS per-pixel tint and the C++ tinted-addressing validation. Host tests cover coloured tint recovery, alpha-only fades, zero alpha, and unchanged non-`AlphaBlend` channels. Browser pixel verification remains on CANVAS-82's manual checklist. |
| CANVAS-85 | Keep the `CreateRenderTargetCube` override aligned with the shared renderer interface | ✅ | The verification build exposed that Canvas still implemented the former four-argument signature after `IGraphicsRenderer` gained `preserveContents`; Clang correctly rejected its stale `override`, preventing every Canvas build before the tint change could even compile. Added the ignored (2D backend) `preserveContents` argument to the declaration and definition. The default/null/no-op resource policy is unchanged. |

---

## Boundaries — explicitly out of scope for v1

- **No 3D pipeline of any kind** (Design decision 4) — this is the entire point, not a temporary
  limitation to lift later.
- **No custom `Effect`/shader execution** (Design decision 10) — Canvas2D has no programmable
  shader stage; this is permanent, not a v1-only gap.
- **No native desktop support** — Emscripten-only (Design decision 1); revisit only if the owner
  later wants a bundled software-canvas library for native platforms, a separate, bigger decision.
- **No headless-browser automated pixel testing in this iteration** (Design decision 9) — real
  pixel verification is `needs_human` until/unless the owner separately approves adding a
  Puppeteer/Playwright-style harness.
- **Mip levels (`level>0` `SetData`)** — confirmed (CANVAS-21) a permanent throw, matching
  `SDL_RENDERER`'s Task 681 precedent; Canvas2D has no native mip chain to store into, so there is
  no cheaper real option.
- **Mixed per-axis `TextureAddressMode` (`addressU != addressV`) and a tinted or `AlphaBlend` draw
  combined with an out-of-bounds Wrap/Mirror `sourceRectangle`** (CANVAS-44) — narrow, deliberate
  throws rather than silently-wrong output; not pursued further for v1.
- **Automated pixel/visual verification of anything in this backend** — the plan's Design decision 9
  constraint held throughout implementation: this dev loop never had a real
  `CanvasRenderingContext2D`/DOM to test against, confirmed empirically (not just assumed) as early
  as Phase C1. `docs/canvas-backend.md`'s 10-item manual browser verification checklist is the
  concrete, current stand-in for this — genuinely open, `needs_human`, not merely deferred busywork.
