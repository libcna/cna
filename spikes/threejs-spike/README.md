# Three.js existence-gate spike — `plans/plan_threejs.md` THREEJS-0

Proves, before any renderer code is written, what a prospective CNA `THREEJS` renderer identity
could and could not do. The `dx9-spike/` precedent applies: the finding is the deliverable, the
code is kept so the finding can be re-checked.

Unlike the DirectX spikes this one is a **browser** spike, so it needs no emsdk — it exercises
three.js and WebGL2 directly through headless Chromium, which is the half of the problem that
decides the plan. The Emscripten half (does `--extern-pre-js` reach the ESM loader, does the wasm
bundle link) is **not** covered here and is `THREEJS-0b`, because no emsdk was available in the
session that wrote this.

## What it proved (2026-09-04, three.js r185, headless Chromium on SwiftShader)

**33/33 checks pass** across two pages. Each check maps to a premise `plans/plan_threejs.md` relies
on; a premise that fails here changes the plan rather than being worked around later.

### `spike.html` — can the renderer exist at all? (22/22)

| Premise | Result |
|---|---|
| P1/P1b | three.js r161+ is **ESM-only**, and it is still reachable from a **classic** script via dynamic `import()`. Its two-file relative graph (`three.module.min.js` → `./three.core.min.js`) resolves with no import map. This is the shape Emscripten's `--extern-pre-js` output has. |
| P2 | `WebGLRenderer` adopts an **existing platform-owned `<canvas>`**, as CANVAS/HTML_DOM/SVG_DOM/PIXIJS require. The raw WebGL2 context stays reachable for state three.js has no API for. |
| P3 | With `autoClear = false`, nothing clears implicitly; `Clear()` is CNA's alone. |
| P4/P4b | **A second `render()` accumulates** into the same target instead of discarding the first batch. This is the premise PixiJS violated (`PIXIJS-87`), and three.js satisfies it natively. |
| P5/P5b/P5c | `CustomBlending` honours **literal XNA blend factors**. `AlphaBlend` (One, InvSrcAlpha) → `255,255,255`; `NonPremultiplied` (SrcAlpha, InvSrcAlpha) → `127,127,127` on the same source. three.js does **not** rewrite them the way PixiJS's `premultiplyBlendMode` did, so the two presets are distinguishable by construction. |
| P6 | `gl.blendColor` survives a `render()` → `BlendState.BlendFactor` is reachable. |
| P7/P7b | `wrapS` and `wrapT` are **independent** → a mixed `AddressU`/`AddressV` pair is expressible. PIXIJS had to reject that combination (`PIXIJS-90`). |
| P8/P8b | `WebGLRenderTarget` binds, clears and reads back; unbinding restores the back buffer with contents intact. |
| P9 | `setScissor`/`setScissorTest` genuinely restrict a clear. |
| P10 | `gl.colorMask` survives three.js state management → `ColorWriteChannels` is real. |
| P11 | A **real depth buffer** makes submission order irrelevant (near wins in both orders). 3D is genuine, not painter's-algorithm. |
| P12 | `RawShaderMaterial` with `glslVersion: GLSL3` runs **CNA-authored GLSL ES 3.00**. |
| P13 | `InstancedMesh` draws N instances in one submission. |
| P14 | `dispose()` then a **new** `WebGLRenderer` on the same canvas still renders — the PIXIJS-92 context-loss failure mode does not occur, so a `GraphicsDevice` can be destroyed and recreated. |

### `spike-batch.html` — what could it honestly claim? (11/11)

| Premise | Result |
|---|---|
| Q1–Q1c | One dynamic `BufferGeometry` (`DynamicDrawUsage` + `setDrawRange`) draws a whole SpriteBatch flush, a second flush accumulates, and unused capacity does not draw. This is the design every other CNA renderer uses, and three.js supports it directly — no per-sprite scene-graph nodes and no object pool. |
| Q1d | `renderer.info.render.calls` = **1** for a 3-sprite flush → batching is *testable*, not assumed. |
| Q1e | 32-bit indices work (`CreateIndexBuffer32`). |
| Q2/Q2c | `flipY = false` + `colorSpace = NoColorSpace` gives XNA's top-left texel origin and a **straight** tint multiply with no sRGB conversion. |
| Q3 | XNA `BasicEffect`'s lighting math (`ambient + Σ saturate(dot(N, −L)) · diffuse`) runs **verbatim** in a `RawShaderMaterial`. |
| Q4 | `WebGLRenderTarget({count: 2})` writes two independent attachments → real MRT. |
| Q5 | 2000 sprites/frame in one batched geometry = **2.54 ms/frame** on SwiftShader (CPU rasterization, so a GPU is strictly faster). Not a benchmark; only evidence the design is not unusable. |

## The two findings that would otherwise have cost a session

**A y-down orthographic projection reverses triangle winding.** XNA client space is y-down
(origin top-left). Building that as `OrthographicCamera(0, w, 0, h, …)` flips handedness, which
reverses winding, which back-face-culls **every** quad. The spike's first run failed *every*
geometry check for this reason and passed *only* the clear/scissor checks — a signature that reads
like "drawing is broken" rather than "culling is on". The renderer must own this decision
explicitly, and it interacts with `RasterizerState.CullMode`, which XNA still expects to be
honoured for 3D. `side: DoubleSide` is the spike's workaround, not a design recommendation.

**`readRenderTargetPixels`'s 7th argument is `activeCubeFaceIndex`, not the attachment.** The
attachment is the 8th (`textureIndex`). Passing the attachment in slot 7 silently reads attachment 0
twice, which is precisely how a renderer comes to believe MRT works when it does not — the spike
made exactly that mistake and reported a false MRT failure until it was corrected.

## Pinned artifacts

`fetch.sh` populates `vendor/` from the **npm registry** (`cdn.jsdelivr.net` is blocked by the
outbound proxy here — `cmake/ThirdPartyPixiJS.cmake` records hitting the same wall).

| File | SHA256 |
|---|---|
| `three@0.185.1` `build/three.module.min.js` | `86bcee248b64f44bcfc23c331ae74619061957d59cab040171dcb6fb5900beb6` |
| `three@0.185.1` `build/three.core.min.js` | `05b2609338c76cd65daf74f3ac515bc9a5045e1b3b33edc07d8c9bd55250fa90` |
| `three@0.160.1` `build/three.min.js` (UMD) | `170c6789f43217c96b3170f4b42fafe135de7f7cd48497a4218f9757ee1d49fa` |

**r160.1 is the last release shipping a UMD classic script.** Verified by unpacking every release
from r160.1 to r165.0: `build/three.min.js` is present in 0.160.1 and absent from 0.161.0 onward.
The r160.1 file itself opens by printing *"Scripts build/three.js and build/three.min.js are
deprecated with r150+, and will be removed with r160."* It is kept in `vendor/` so the "just vendor
the UMD build like PIXIJS does" option can be inspected rather than argued about; the plan rejects
it (Design decision 3).

## Reproduce

```bash
./spikes/threejs-spike/fetch.sh
node scripts/run_pixijs_browser_tests.mjs spikes/threejs-spike spike.html spike-batch.html
```

Expected: `spike.html: PASS (22/22)` and `spike-batch.html: PASS (11/11)`.

The runner is `scripts/run_pixijs_browser_tests.mjs` — its name is historical and it already serves
two unrelated subsystems (the PixiJS suite and the C ABI browser probe), so this spike reuses it
rather than adding a third runner.

## Not covered here

- **Anything Emscripten.** No emsdk was available. Whether `--extern-pre-js` + `addRunDependency`
  actually holds `main()` until the dynamic `import()` resolves, and whether the two ESM files can
  be emitted beside the bundle so the browser's module loader finds them, is `THREEJS-0b` and is
  the one remaining existence-gate question.
- MSAA, stencil operations, `Texture3D`, float/half-float render targets, occlusion queries and
  anisotropic filtering — each is a capability the plan must measure before claiming, and none is
  measured here.
- `three.webgpu.js` (three.js's WebGPU renderer), which is the only route by which this identity
  could reach a platform CNA does not already reach. Out of the plan's v1 scope entirely.
