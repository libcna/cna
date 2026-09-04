# Three.js Graphics Renderer — Feasibility Analysis and Implementation Plan

> **Status legend** (this project's own convention): ✅ implemented *and verified against its stated
> acceptance criteria*; 🟨 code or documentation exists but has not met those criteria; ⬜ not
> implemented.

## Current status — 2026-09-04

**⬜ NOT STARTED. This is a plan and a feasibility analysis, not authorization to build.**

Exactly as `docs/renderer-expansion-candidates.md` requires of every candidate identity: nothing
here may be implemented because this file exists. `THREEJS` needs a **fresh explicit owner
instruction** before a single line of renderer code is written.

One thing *has* been done, because it is what makes the rest of this document evidence rather than
opinion: the **existence-gate spike** (`spikes/threejs-spike/`, `THREEJS-0`), which answers in a
real browser against the real library what a `THREEJS` renderer could and could not do.

| Evidence | Result |
|---|---|
| `spikes/threejs-spike/spike.html` — can the renderer exist at all? | **22/22 checks pass** |
| `spikes/threejs-spike/spike-batch.html` — what could it honestly claim? | **11/11 checks pass** |
| three.js revision exercised | r185 (`0.185.1`), MIT |
| Browser | headless Chromium on SwiftShader, via `scripts/run_pixijs_browser_tests.mjs` |
| Emscripten link path (`THREEJS-0b`) | **NOT verified — no emsdk in the session that wrote this** |

**Every technical premise this plan depends on is measured and holds.** The renderer is buildable.

**Whether it should be built is a different question, and the answer is no.** See §1 — and §1.4
in particular, which was corrected on 2026-09-04 after the owner pointed out that CNA already runs
WebGPU in the browser. That correction removed the last argument this identity had in its favour.
The conclusion is the reason this file leads with it rather than burying it under a task table.

---

## 1. The decision the owner actually has to make

### 1.1 What happened five days ago

On **2026-08-30** this project removed **eleven** renderer identities in one campaign
(`docs/removed-renderers.md`): `LLGL`, `SKIA`, `SOKOL`, `DILIGENT`, `IGL`, `WICKED`, `MAGNUM`,
`BLEND2D`, `NANOVG`, `OPENVG`, `TINYGL`. The public identity count went from **50 to 39**
(`scripts/check_renderer_identities.py`: *"OK: 39 public renderer identities … over 35
implementation families"*).

The removals were not arbitrary. They record three disqualifying patterns, in the project's own
words:

1. **Wrapping another abstraction.** *"CNA is itself a portable graphics abstraction; wrapping
   another one adds a translation layer that can only lose fidelity and gain bugs, and teaches its
   own API rather than the GPU."* — the stated reason for `LLGL`, and repeated for `SOKOL`,
   `DILIGENT` and `IGL`.
2. **2D-only.** *"It is 2D-only by construction, so it can never satisfy `IGraphicsRenderer`: CNA's
   contract has 102 pure-virtual methods … That is a category error rather than unfinished work."*
   — `SKIA`, `BLEND2D`, `NANOVG`, `OPENVG`.
3. **Adds no platform CNA does not already reach.** Every single one of the eleven.

`WICKED` is the closest precedent of all: *"the most inverted of the layering mistakes: wrapping a
whole game engine's render hardware interface **inside** a game framework."*

### 1.2 Where `THREEJS` lands against that test

| Test | `THREEJS` |
|---|---|
| Is it 2D-only? | **No.** Measured: real depth buffer (P11), render targets (P8), MRT (Q4), instancing (P13), CNA-authored GLSL ES 3.00 (P12). It clears the bar `SKIA`/`BLEND2D`/`NANOVG`/`OPENVG` structurally could not. |
| Does it add a platform? | **No.** The browser is already reached by **seven** identities: `WEBGL1`, `WEBGL2`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `PIXIJS`, and `WEBGPU` (whose Emscripten backend shipped 2026-08-26 — see §1.4). |
| Is it a translation layer over an API CNA reaches natively? | **Yes.** three.js drives WebGL2. `WEBGL2` (EasyGL) reaches WebGL2 natively, on the same platform, in the same browser. |

Two of three disqualifying patterns apply. That is the same score `MAGNUM` had — *"it reached
desktop OpenGL only, which EasyGL already covers … so it added nothing"* — with `WEBGL2`
substituted for EasyGL-on-desktop.

### 1.3 The argument that survives, and the measurement that kills it

The strongest case *for* `THREEJS` is that it is not an RHI like `LLGL`/`SOKOL`/`IGL`. It is a
**retained-mode scene graph with its own material and lighting system** — a genuinely different
execution model, and the 3D counterpart of what `PIXIJS` proved for 2D. `PIXIJS` survived the cull
on exactly that reasoning.

The spike measured what that argument is worth, and the answer is: **not enough.**

`Q3` asked whether XNA's stock effects can be expressed through three.js's materials.
`BasicEffect`'s lighting is `ambient + Σ saturate(dot(N, −L)) · lightDiffuse`. That is not
`MeshPhongMaterial`'s model, not `MeshStandardMaterial`'s, and not `MeshLambertMaterial`'s.
Reproducing XNA faithfully therefore means writing the lighting in **CNA's own GLSL** and running
it through `RawShaderMaterial` — which the spike confirmed works perfectly (Q3, P12).

But once every effect is CNA's own GLSL, and the SpriteBatch is CNA's own dynamic
`BufferGeometry` (Q1), and the blend factors are CNA's own literal tuples (P5), and `blendColor`
and `colorMask` are set on the raw context because three.js has no API for them (P6, P10), and
`flipY`/`colorSpace`/`toneMapped`/`outputColorSpace` are all switched **off** because three.js's
defaults are wrong for XNA (Q2, Q2c) — **what remains of three.js is a WebGL resource and state
manager.** The scene graph, the material system and the lighting model, which are the things that
make three.js a different execution model, are exactly the parts XNA fidelity requires CNA to not
use.

That is the translation layer `docs/removed-renderers.md` rejects, arrived at by measurement rather
than by analogy.

### 1.4 The last argument, and why it does not survive either

**Corrected 2026-09-04 — an earlier revision of this section was wrong, and the correction removes
the only argument that was left in this identity's favour.**

That revision claimed browser WebGPU was a platform CNA does not reach, and that three.js's
`WebGPURenderer` (`three.webgpu.js`, which does ship in the same package) was therefore a route to
something new. **CNA already reaches browser WebGPU, and has since 2026-08-26.** The tree says so
plainly:

- `cmake/ThirdPartyWebGPU.cmake` has an `if(EMSCRIPTEN)` branch linking Emscripten's
  **emdawnwebgpu** port (`--use-port=emdawnwebgpu`), reaching the browser's own `navigator.gpu`.
- `plans/plan_webgpu.md` `WEBGPU-119`–`122` and `WEBGPU-133` are all ✅: `cna_demo_2d` renders 120
  SpriteBatch frames in headless Chrome, `cna_house3d_demo` drives the 3D `BasicEffect` path
  in-browser, and **every** stock-effect shader family — `PbrEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect`, `DualTextureEffect`, `AlphaTestEffect` — compiles and renders there.
- `WEBGPU-123` measured browser WebGPU against native Vulkan on the same scene: **byte-identical,
  max per-channel diff 0.**
- `docs/webgpu-renderer.md`: *"`WEBGPU` is one renderer identity with two backends, not two
  renderers."* Five seams differ behind `#if defined(__EMSCRIPTEN__)`; everything else, including
  every WGSL shader, is shared.

That is the registry's own doctrine applied correctly — `docs/renderer-expansion-candidates.md` §4
already rules that Dawn is *"a **profile** of `WEBGPU`, like the GL profiles"*, and the browser
backend is the same kind of thing.

So the position is worse than the earlier revision described, not better. Browser WebGPU is not a
gap `THREEJS` could fill; it is **a seventh browser route CNA already has**, and one that is
pixel-verified against native Vulkan — a standard of evidence a three.js-mediated route could not
match, because it would be measured against three.js's output rather than against CNA's own.

**Stale documentation, noted but not fixed here** (it belongs to whoever next touches that file):
`docs/renderer-expansion-candidates.md` row `C4 WEBGPU_WEB` still proposes browser WebGPU as a
future identity and argues it is *"identity-worthy, not an alias"*. That row is dated 2026-08-13,
thirteen days before the browser backend shipped, and `docs/webgpu-renderer.md` now contradicts it
directly.

### 1.5 Recommendation

**Do not build `THREEJS` as a renderer identity.** It fails the admission test this project applied
to eleven renderers five days ago, and it fails it for the same reason `MAGNUM` did.

With §1.4 corrected, **no argument in its favour is left standing.** Not 3D capability (measured,
but `WEBGL2` and browser `WEBGPU` both already have it). Not a distinct execution model (measured
away by DD4: XNA fidelity forbids using the scene graph and material system that would make it
distinct). Not a platform (seven browser routes already exist). Not browser WebGPU (shipped, and
pixel-verified against native Vulkan). What remains is a well-understood, cheaply-vendored,
thoroughly-spiked renderer that would duplicate `WEBGL2` and be a candidate for the next cull on
the day it merged.

If the owner authorizes it anyway — which is entirely their call, and this plan is complete enough
to execute on — §3 onward is the real backlog, and §2 records what it would honestly be allowed to
claim. Three alternatives that cost less and deliver more are listed in §9.

---

## 2. Capability boundary, from measurement

What `THREEJS` would report from `SupportsCapability()`. Rows marked **measured** are proven in
`spikes/threejs-spike/`; rows marked *unmeasured* are capabilities the plan must measure in its own
phase before claiming, and which report `false` until then. No capability is claimed on the
strength of "three.js probably supports it".

| `GraphicsCapability` | Verdict | Evidence |
|---|---|---|
| `ThreeD` | `true` | **measured** — P11, depth buffer makes submission order irrelevant |
| `DepthStencilBuffer` | `true` | **measured** — P8 (depth+stencil render target); stencil *operations* are `THREEJS-63` |
| `MultipleRenderTargets` | `true` | **measured** — Q4, `WebGLRenderTarget({count: 2})`, two independent attachments |
| `Instancing` | `true` | **measured** — P13, `InstancedMesh` |
| `CustomEffects` | `true` | **measured** — P12, `RawShaderMaterial` + `glslVersion: GLSL3` |
| `AdditiveBlending` | `true` | **measured** — P5, literal `CustomBlending` factor tuples |
| `MultiStreamVertexInput` | `true` | **measured** — Q1, `BufferGeometry` takes arbitrary named attributes |
| `AnisotropicFiltering` | *unmeasured* | `texture.anisotropy` exists; `THREEJS-58` |
| `MultiSampleAntiAliasing` | *unmeasured* | `WebGLRenderTarget({samples})`; `THREEJS-72` |
| `WireFrame` | *unmeasured* | `material.wireframe`; `THREEJS-59` |
| `OcclusionQuery` | *unmeasured* | WebGL2 `ANY_SAMPLES_PASSED` via the raw context; `THREEJS-84` |
| `Texture3D` | *unmeasured* | `Data3DTexture`; `THREEJS-70` |
| `FloatRenderTargets` / `HalfFloatRenderTargets` | *unmeasured* | `THREE.FloatType`/`HalfFloatType`; `THREEJS-71` |
| `HalfFloatTextureLinearFiltering` | *unmeasured* | `THREEJS-71` |
| `ComputeShaders` | **`false`** | WebGL2 has none. three.js's compute is `WebGPURenderer`-only, out of scope (§6) |
| `IndirectDraw` | **`false`** | No WebGL2 equivalent |

`ExecutesShaderEffectSourceEXT()` would return **`true`** — worth noting, because `SOFTWARE` and
`HEADLESS` report `CustomEffects` while quietly ignoring the source. P12 proves `THREEJS` would
actually execute it, which puts it in the same honest category as EasyGL and Vulkan.

---

## 3. Design decisions

Numbered as `plan_pixijs.md` numbers its own, and referenced from task rows.

**DD1 — Emscripten-only.** three.js needs `document`, an `HTMLCanvasElement` and a WebGL context,
none of which exist in a native build. Same configure-time refusal as `CANVAS`/`HTML_DOM`/
`SVG_DOM`/`PIXIJS`, and the same entry in `CNA_RENDERER_EMSCRIPTEN_ONLY`
(`cmake/RendererCombinations.cmake`).

**DD2 — The platform owns the canvas.** The renderer consumes only the platform-neutral surface
snapshot and never creates or sizes a window. Descriptor identical in shape to
`PixiJsRendererDescriptor.cpp`: `windowKind = Plain`, `needsWindow = true`,
`needsVideoSubsystem = true`, `isAvailable = &AlwaysAvailable`. Measured: P2, `WebGLRenderer`
adopts an existing canvas.

**DD3 — Vendor the ESM build, not the UMD build.** Measured: **r160.1 is the last release shipping
`build/three.min.js`**; it is absent from r161.0 onward, and the r160.1 file itself opens by
printing *"deprecated with r150+, and will be removed with r160"*.

- *Rejected:* pin r160.1 UMD so `--extern-pre-js` works exactly as it does for `PIXIJS`. Zero new
  machinery, but it freezes CNA on a January-2024 build that upstream has declared removed. A
  renderer whose first design decision is "depend on a deleted artifact forever" is not worth
  adding.
- *Chosen:* pin a current release's `three.module.min.js` **and** `three.core.min.js`, and load
  them with a dynamic `import()` from an `--extern-pre-js` prologue, bracketed by Emscripten's
  `addRunDependency`/`removeRunDependency` so `main()` cannot start before `THREE` exists.
  Measured: P1/P1b — dynamic `import()` from a **classic** script works, and the two-file relative
  graph resolves with no import map.

  This is **genuinely new machinery** compared with `PIXIJS`: the two ESM files are fetched by the
  browser's module loader, not by the wasm filesystem, so `--preload-file` is wrong and CMake must
  emit them **beside** the generated `.js`/`.wasm`. `THREEJS-0b` is the unverified half.

  SHA256-pinned, downloaded at configure time from the **npm registry** — `cdn.jsdelivr.net` is
  blocked by the outbound proxy here, which `cmake/ThirdPartyPixiJS.cmake` records hitting too —
  with `CNA_THREEJS_ROOT` for offline and reproducible builds. Never a runtime `<script src="https://…">`.

**DD4 — XNA fidelity comes from CNA's own GLSL, never from three.js materials.** Every effect,
stock and custom, is a `RawShaderMaterial` with `glslVersion: THREE.GLSL3`. Measured: Q3 — XNA
`BasicEffect`'s lighting math runs verbatim; three.js's own materials implement a different
lighting model and cannot be made faithful by parameter-fitting. **This decision is also the core
of §1.3's negative recommendation**, and it must not be quietly reversed later to make the renderer
look more "three.js-native": doing so would trade XNA fidelity for aesthetics.

**DD5 — `SpriteBatch` is one dynamic `BufferGeometry` per flush, committed at every submission
point.** Measured: Q1/Q1b/Q1c/Q1d — `DynamicDrawUsage` attributes + `setDrawRange` + `render()`
produces exactly **one draw call**, accumulates across flushes, and does not draw unused capacity.

This inherits `PIXIJS-87`'s lesson without inheriting its problem. PixiJS forced a retained scene
graph and pooled sprite nodes, and every ordering, state and lifetime defect in that renderer
followed from it. three.js imposes no such thing: `render()` neither re-parents nor resets, so
`End()` (and each `Draw()` in `SpriteSortMode::Immediate`) simply rasterizes. `frustumCulled =
false` on the batch mesh, because clipping is XNA's job.

**DD6 — The renderer owns the winding flip.** XNA client space is y-down. Expressing that as an
orthographic projection reverses handedness, which reverses triangle winding, which back-face-culls
every quad. Measured the hard way: the spike's first run failed **every** geometry check and passed
only the clear/scissor checks — a signature that reads like "drawing is broken" rather than
"culling is on".

`side: DoubleSide` is the spike's workaround and is **not** the design: XNA still expects
`RasterizerState.CullMode` (default `CullCounterClockwiseFace`) to be honoured for 3D. The 2D and
3D paths need separate, explicit answers — `THREEJS-41` and `THREEJS-62`.

**DD7 — three.js's colour and orientation defaults are all wrong for XNA and are switched off
explicitly.** Measured: Q2/Q2c. `texture.flipY = false` (XNA texel (0,0) is top-left),
`texture.colorSpace = NoColorSpace`, `material.toneMapped = false`,
`renderer.outputColorSpace = NoColorSpace`, `premultipliedAlpha: false` on the context. XNA hands
the renderer literal bytes and expects a straight multiply; every one of these defaults would
otherwise silently alter pixels.

**DD8 — State three.js has no API for goes through the raw context, immediately before the render
that consumes it.** Measured: P6 (`gl.blendColor` → `BlendState.BlendFactor`) and P10
(`gl.colorMask` → `ColorWriteChannels`) both survive a `render()`. `PIXIJS-88/89` had to discover
this the expensive way; here it is a design decision from the start.

**DD9 — Blend states are literal XNA factor tuples.** Measured: P5/P5b/P5c — `CustomBlending` with
explicit `blendSrc`/`blendDst`/`blendSrcAlpha`/`blendDstAlpha`/`blendEquation` renders `AlphaBlend`
and `NonPremultiplied` **differently** on the same source. three.js does not rewrite factor tuples
the way PixiJS's `premultiplyBlendMode` did, so the entire `PIXIJS-51` bug class does not exist
here.

**DD10 — Sampler state is per-axis and is never approximated.** Measured: P7 — `wrapS`/`wrapT` are
independent, so a mixed `AddressU`/`AddressV` pair is expressible. `PIXIJS-90` had to *reject* that
combination because a `PIXI.BaseTexture` carries one `wrapMode`. Out-of-range `TextureFilter` /
`TextureAddressMode` values are still rejected rather than defaulted.

**DD11 — Device teardown and recreation are supported.** Measured: P14 — `dispose()` followed by a
**new** `WebGLRenderer` on the same canvas still renders. The `PIXIJS-92` failure mode (a canvas
hands out one WebGL context, and the library's `destroy()` loses it on purpose) does not occur, so
the renderer is scoped to itself and needs no split canvas-scoped/renderer-scoped ownership.

**DD12 — Renderer-local, always.** `IGraphicsRenderer` is touched only if a common change is
genuinely required *and* re-verified across the established renderers
(`docs/renderer-expansion-candidates.md` §5.2). This plan currently anticipates **no** common
change.

---

## 4. What it would cost

| | |
|---|---|
| Effort class | **L** — comparable to the Vulkan/Skia campaigns, not to `TINYGL` |
| Comparable modules | `WICKED` 6,978 lines · `MAGNUM` 9,472 lines · `PIXIJS` 3,465 lines (2D-only) |
| Realistic size | **~7,000–10,000 lines**, because unlike `PIXIJS` this is 2D **and** 3D |
| Tasks below | **112** |
| Dependency cost | **Low** — one MIT-licensed JS package, two files, no build step, no system headers, no pinned git checkout, no patch. This is the cheapest dependency of any library-backed renderer CNA has had. |
| Integration surface | ~50 files outside the module (measured by enumerating every non-module file mentioning `PIXIJS`) |

The dependency being cheap is the one place `THREEJS` clearly beats `MAGNUM` (two pinned repos +
system GL/X11 headers) and `WICKED` (a pinned commit **plus** a CNA-authored patch that only
applies to that revision).

---

## 5. Tasks

### Phase 0 — Existence gate

| ID | Task | Status |
|---|---|---|
| THREEJS-0 | Browser existence-gate spike: 33 checks across `spike.html` + `spike-batch.html`, proving DD2–DD11. `spikes/threejs-spike/` | ✅ |
| THREEJS-0b | **Emscripten existence gate.** Prove `--extern-pre-js` + `addRunDependency` holds `main()` until the dynamic `import()` resolves, and that CMake can emit both ESM files beside the bundle so the module loader finds them. **Blocking: no phase below may start until this passes.** Needs an emsdk. | ⬜ |

### Phase 1 — Identity registration (14)

| ID | Task |
|---|---|
| THREEJS-1 | `GraphicsRendererType::ThreeJs` appended to `modules/core/include/CNA/GraphicsRendererType.hpp` (slot 40 in this tree) |
| THREEJS-2 | `getGraphicsRendererName()` → `"THREEJS"`; parse the selector back |
| THREEJS-3 | Enum-bound loops derive from the real last enumerator, not a hard-coded one (`IGL`'s removal uncovered exactly this bug in `GraphicsBackendCategoryTests`/`GraphicsBackendMaturityTests`) |
| THREEJS-4 | `GraphicsBackendCategory` classification (web / scene-graph) |
| THREEJS-5 | `GraphicsBackendMaturity` = experimental |
| THREEJS-6 | `CNA_RENDERER_THREEJS` option + selector dispatch in `cmake/RendererSelection.cmake` (both the `STRINGS` list and the per-identity branch) |
| THREEJS-7 | Emscripten-only configure refusal, DD1 |
| THREEJS-8 | `CNA_RENDERER_EMSCRIPTEN_ONLY` entry in `cmake/RendererCombinations.cmake` |
| THREEJS-9 | `cmake/RendererRegistry.cmake` registration |
| THREEJS-10 | C ABI: `CNA_GRAPHICS_RENDERER_THREEJS` in `modules/c-api/include/CNA/C/graphics.h` + `CnaCApiGraphics.cpp` |
| THREEJS-11 | `tools/c-api/abi_baseline.json` and `release_gate.json` updated |
| THREEJS-12 | `scripts/check_renderer_identities.py` recounts cleanly (39 → 40) |
| THREEJS-13 | `scripts/check_renderer_descriptors.py` and `check_runtime_renderer_discipline.py` pass |
| THREEJS-14 | Every prose count updated in the docs the gate checks; **also** fix the pre-existing drift in `docs/renderer-registry.md` and `CLAUDE.md`, which still say 49 after the 2026-08-30 removals took the tree to 39 |

### Phase 2 — Module skeleton and lifecycle (8)

| ID | Task |
|---|---|
| THREEJS-15 | `modules/renderers/threejs/{CMakeLists.txt,include,src,tests,examples}` via `cna_add_renderer()` |
| THREEJS-16 | `ThreeJsRendererDescriptor.cpp`, DD2 |
| THREEJS-17 | `CNA::Internal::Renderers::ThreeJs::CreateGraphicsRenderer` in the family's own namespace (multi-renderer link requirement) |
| THREEJS-18 | `ThreeJsRenderer` class + `IGraphicsRenderer` skeleton; every unimplemented override routes to `NotYetImplemented("THREEJS", …)` — never a silent no-op |
| THREEJS-19 | Construction creates the `WebGLRenderer` against the platform canvas; `autoClear = false`, DD7 defaults applied |
| THREEJS-20 | Every `EM_JS` entry point returns a status the C++ side turns into a real exception (`PIXIJS-91`'s lesson: never `console.error(); return;`) |
| THREEJS-21 | Renderer-scoped JS state under `Module['cnaThree']`; teardown releases it, DD11 |
| THREEJS-22 | Destroy/recreate a `GraphicsDevice` with no state carried across |

### Phase 3 — Vendoring and JS bootstrap (8)

| ID | Task |
|---|---|
| THREEJS-23 | `cmake/ThirdPartyThreeJs.cmake`: `CNA_THREEJS_VERSION`, `CNA_THREEJS_ROOT`, `CNA_THREEJS_AUTO_DOWNLOAD`, two SHA256 pins, DD3 |
| THREEJS-24 | Download both ESM files from the npm registry with `EXPECTED_HASH`; loud failure on mismatch |
| THREEJS-25 | Emit both files beside the generated `.js`/`.wasm` — **not** `--preload-file` |
| THREEJS-26 | `--extern-pre-js` prologue (verbatim, **never** `--pre-js`: `PIXIJS-86` proved `-O3` dead-code-eliminates a bundle spliced into the module body) |
| THREEJS-27 | `addRunDependency`/`removeRunDependency` bracket around the dynamic `import()`, DD3 |
| THREEJS-28 | A failed import fails the page loudly instead of leaving `main()` running against an absent `THREE` |
| THREEJS-29 | Flag applied whether `THREEJS` is the default renderer or merely one member of a multi-renderer set |
| THREEJS-30 | Record the pin's provenance in the CMake file itself, including which CDN was and was not reachable |

### Phase 4 — Presentation (7)

| ID | Task |
|---|---|
| THREEJS-31 | `Clear` / `ClearColorAndDepth` / `ClearDepth` / `ClearStencil` / the three combined forms |
| THREEJS-32 | `Present()` |
| THREEJS-33 | `GetViewportSize` / `GetDefaultViewportRect` from the platform surface snapshot |
| THREEJS-34 | `SetViewport`, including `minDepth`/`maxDepth` |
| THREEJS-35 | `SetScissorRect`, measured P9 |
| THREEJS-36 | `SetVirtualResolution` / `SetPresentationMode` |
| THREEJS-37 | `ReadBackbuffer` with the WebGL bottom-left → XNA top-left flip (`preserveDrawingBuffer: true`) |

### Phase 5 — Texture2D (7)

| ID | Task |
|---|---|
| THREEJS-38 | `CreateTexture` from `ImageData` via `DataTexture`, DD7 |
| THREEJS-39 | `SetData` / `GetData` / sub-rect updates |
| THREEJS-40 | Mip chain generation and level-0-only uploads |
| THREEJS-41 | **2D winding decision**, DD6 |
| THREEJS-42 | Surface-format mapping; `ClassifyRenderTargetFormatEXT` refuses what it cannot do |
| THREEJS-43 | Compressed-texture formats via WebGL2 extensions, or an explicit refusal |
| THREEJS-44 | Texture disposal; a texture destroyed after `End()` (which `SpriteBatch` permits) must not take a live GPU resource with it |

### Phase 6 — SpriteBatch (12)

| ID | Task |
|---|---|
| THREEJS-45 | Dynamic `BufferGeometry` + interleaved position/uv/colour attributes, DD5 |
| THREEJS-46 | `setDrawRange` bounds the flush, Q1c |
| THREEJS-47 | The SpriteBatch `RawShaderMaterial` (CNA's own GLSL), DD4 |
| THREEJS-48 | `Begin`/`End`; `End()` rasterizes |
| THREEJS-49 | `SpriteSortMode::Immediate` — each `Draw()` rasterizes |
| THREEJS-50 | `Deferred`, `Texture`, `BackToFront`, `FrontToBack` sort modes |
| THREEJS-51 | Rotation, origin, scale, `SpriteEffects` |
| THREEJS-52 | Source-rectangle UVs |
| THREEJS-53 | Per-batch state captured at `Begin()`, applied immediately before the render that consumes it, DD8 |
| THREEJS-54 | Geometry growth beyond the initial capacity |
| THREEJS-55 | Two `Begin`/`End` pairs in one frame accumulate (the `PIXIJS-87` regression test) |
| THREEJS-56 | Draw-call count asserted via `renderer.info.render.calls`, Q1d — batching proven, not assumed |

### Phase 7 — Render state (13)

| ID | Task |
|---|---|
| THREEJS-57 | `ApplySamplerState`: filter + per-axis address, DD10 |
| THREEJS-58 | `AnisotropicFiltering` — measure, then claim or refuse |
| THREEJS-59 | `ApplyRasterizerState`: cull mode, fill mode, `WireFrame` — measure |
| THREEJS-60 | Depth bias / slope-scale depth bias |
| THREEJS-61 | `ApplyBlendState` from literal factor tuples, DD9 |
| THREEJS-62 | **3D winding / `CullMode` decision**, DD6 |
| THREEJS-63 | `ApplyDepthStencilState` incl. stencil ops; measure before claiming `StencilBuffer` |
| THREEJS-64 | `SetReferenceStencil` |
| THREEJS-65 | `SetBlendFactor` → `gl.blendColor`, DD8 |
| THREEJS-66 | `ColorWriteChannels` → `gl.colorMask`, DD8 |
| THREEJS-67 | `MultiSampleMask` — honour it or reject it explicitly |
| THREEJS-68 | `ApplySamplerMipState` / `ApplySamplerAddressW` |
| THREEJS-69 | `SetDepthTestEnabled` / `SetDepthWriteEnabled` / `SetBlendEnabled` |

### Phase 8 — Resources and 3D draws (12)

| ID | Task |
|---|---|
| THREEJS-70 | `CreateTexture3D` — measure `Data3DTexture`, then claim or refuse |
| THREEJS-71 | Float / half-float render targets and linear filtering — measure |
| THREEJS-72 | MSAA via `WebGLRenderTarget({samples})` — measure |
| THREEJS-73 | `CreateVertexBuffer` / `SetData` / `SetVertexDeclaration` |
| THREEJS-74 | `CreateIndexBuffer16` / `CreateIndexBuffer32`, Q1e |
| THREEJS-75 | `VertexDeclaration` → named `BufferAttribute` mapping |
| THREEJS-76 | `DrawColoredPrimitives` / `DrawIndexedColoredPrimitives` |
| THREEJS-77 | `DrawPrimitivesEx` / `DrawIndexedPrimitivesEx` |
| THREEJS-78 | Every `PrimitiveType`; refuse what WebGL2 has no topology for |
| THREEJS-79 | Multi-stream vertex input; `HasMultipleVertexStreams()` throws before submission if the combined layout cannot be expressed |
| THREEJS-80 | `DrawInstancedPrimitivesEx` via `InstancedBufferGeometry`, P13 |
| THREEJS-81 | Per-instance streams (`instanceFrequency > 0`), captured by value — never re-read from `GraphicsDevice` at replay |

### Phase 9 — Stock effects (10)

All are `RawShaderMaterial` + CNA's own GLSL ES 3.00, DD4.

| ID | Task |
|---|---|
| THREEJS-82 | `BasicEffect`: 3 directional lights, ambient, diffuse, specular, per-vertex and per-pixel, Q3 |
| THREEJS-83 | Fog (`fogEnabled`, `fogVector`, `fogColor`) — XNA's fog, not three.js's |
| THREEJS-84 | Occlusion queries via raw WebGL2 `ANY_SAMPLES_PASSED` — measure |
| THREEJS-85 | `AlphaTestEffect` |
| THREEJS-86 | `DualTextureEffect` |
| THREEJS-87 | `EnvironmentMapEffect` + `TextureCube` |
| THREEJS-88 | `SkinnedEffect` (72 bone transforms, `weightsPerVertex`) |
| THREEJS-89 | `SpriteEffect` / the 2D colour-matrix path (`cpu2DColorMatrixEnabled`) |
| THREEJS-90 | Pixel-parity comparison of every stock effect against `WEBGL2` (EasyGL) — the only meaningful fidelity oracle |
| THREEJS-91 | XNA pixel-centre convention (`spikes/xna-pixel-center-spike/` applies) |

### Phase 10 — Render targets (9)

| ID | Task |
|---|---|
| THREEJS-92 | `CreateRenderTarget2D` / `…EXT`, P8 |
| THREEJS-93 | `SetRenderTarget2D` bind/unbind; back buffer restored intact, P8b |
| THREEJS-94 | `preserveContents` (XNA `RenderTargetUsage`) |
| THREEJS-95 | `SetRenderTargets` (MRT), Q4 — mind that `readRenderTargetPixels`'s attachment index is the **8th** argument |
| THREEJS-96 | `GetMaxRenderTargetsForProfileEXT` from the real `MAX_DRAW_BUFFERS` |
| THREEJS-97 | `CreateRenderTargetCube` / `SetRenderTargetCubeFace` |
| THREEJS-98 | Render-target readback |
| THREEJS-99 | A render target destroyed while bound restores the back buffer and warns |
| THREEJS-100 | Depth/stencil formats per target; `GetAppliedDepthStencilFormatEXT` |

### Phase 11 — Custom effects (6)

| ID | Task |
|---|---|
| THREEJS-101 | `CreateEffectRenderer` from CNA GLSL, P12 |
| THREEJS-102 | `IEffectRenderer::CompileProgram` reports the real compile log through `GetCompileError()` |
| THREEJS-103 | Uniform binding: scalars, vectors, matrices, samplers |
| THREEJS-104 | `ExecutesShaderEffectSourceEXT()` → `true` |
| THREEJS-105 | `CompiledEffects` (`.fxb`) — implement or refuse explicitly (`plans/plan_fx.md`) |
| THREEJS-106 | `CNA_CNAEXT` engine-layer interop: `SupportsShadowSamplingEXT` / `SupportsImageBasedLightingEXT` / `SupportsComputeShadersEXT` answered truthfully |

### Phase 12 — Tests, examples, docs (6)

| ID | Task |
|---|---|
| THREEJS-107 | `modules/renderers/threejs/tests/` — browser-independent contracts as native GTest, the `cna_test_pixijs_host` pattern (`CNA_BUILD_THREEJS_HOST_TESTS=ON`) |
| THREEJS-108 | Browser pixel suite driven by `scripts/run_pixijs_browser_tests.mjs`; every capability claimed in §2 has a check |
| THREEJS-109 | `cna_demo_2d` and a 3D demo render and **display** under `THREEJS` in a real browser — `WICKED` was removed partly for shipping zero examples |
| THREEJS-110 | Multi-renderer set (`THREEJS;WEBGL2;CANVAS`) configures and links; `THREEJS` non-default still reaches the link line |
| THREEJS-111 | `docs/threejs-renderer.md` capability boundary; five platform gates + `hot_path_lint.py` pass |
| THREEJS-112 | Manual browser pass on a real GPU — the item `PIXIJS-82` still owes and this plan must not repeat |

---

## 6. Explicitly out of scope

- **`three.webgpu.js` / `WebGPURenderer`.** Out of scope because it is *redundant*, not merely
  expensive (§1.4): CNA's `WEBGPU` identity already runs in the browser through Emscripten's
  emdawnwebgpu port, with every stock effect verified in headless Chrome and byte-identical output
  to native Vulkan. Building a second, three.js-mediated browser-WebGPU route would add a shader
  language (WGSL/TSL), a resource model and an async bring-up, to reach somewhere CNA already is.
- **three.js's own materials, lights, shadows, post-processing and loaders.** DD4 rules them out
  for fidelity reasons; `modules/graphics-ext/` is where CNA's engine-layer equivalents live.
- **Native (non-browser) builds.** DD1.
- **`node`-side headless rendering.** No DOM; the same boundary `docs/canvas-backend.md` records.

---

## 7. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| `THREEJS-0b` fails — Emscripten cannot hold `main()` for the dynamic import, or cannot emit the ESM files where the module loader finds them | **High** | Blocking gate. Fallback is DD3's rejected option (pin r160.1 UMD), which is bad enough that failing here should end the effort instead. |
| three.js's ESM layout changes again | Medium | It already changed twice (UMD removed at r161; `three.core.js` split by r185). Pin exactly, and treat an upgrade as a task, not a bump. |
| Effect fidelity drifts from `WEBGL2` | Medium | `THREEJS-90` makes EasyGL the oracle and compares pixels. |
| three.js state management fights CNA's | Medium | Measured safe for `blendColor` and `colorMask` (P6, P10); anything else set on the raw context needs its own check. |
| Bundle size: ~750 KB of JS beside the wasm | Low | Documented in the capability boundary; the page fetches it once and caches it. |
| The renderer is removed in the next cull for the reasons in §1 | **High** | Not a technical risk and not mitigable by better code. It is the decision in §1.5. |

---

## 8. Licensing

three.js is **MIT** (`SPDX-License-Identifier: MIT`, verified in the r185 build header) — compatible
and non-disqualifying, unlike several `docs/renderer-expansion-candidates.md` entries flagged for
copyleft re-verification.

---

## 9. If the goal is not literally "three.js"

Three readings of the request, each cheaper and each delivering more:

1. **"CNA should render 3D in the browser."** It already does — `WEBGL2` (EasyGL) is a full 3D
   renderer with the whole stock-effect matrix. Nothing needs building.
2. **"CNA should reach browser WebGPU."** Already done — `-DCNA_GRAPHICS_RENDERER=WEBGPU` under
   `emcmake`, shipped 2026-08-26 (`WEBGPU-119`–`122`/`133`), with 2D, 3D and every stock effect
   verified in a real browser and output byte-identical to native Vulkan (`WEBGPU-123`). Nothing
   needs building; `docs/webgpu-renderer.md` §"Web / browser target" is the recipe.
3. **"CNA games should be embeddable in a three.js scene."** This is not a renderer at all — it is a
   *present sink*: render to a `RenderTarget2D`, hand the texture to a page. Cheap, additive,
   breaks no identity rules, and `docs/renderer-expansion-candidates.md` §4 already classifies
   sinks as not-identities.

---

## 10. Reproduce the evidence

```bash
./spikes/threejs-spike/fetch.sh
node scripts/run_pixijs_browser_tests.mjs spikes/threejs-spike spike.html spike-batch.html
```

Expected: `spike.html: PASS (22/22)`, `spike-batch.html: PASS (11/11)`.

See `spikes/threejs-spike/README.md` for what each check proves and for the two findings (y-down
winding; the `readRenderTargetPixels` attachment argument) that would otherwise each have cost a
session.
