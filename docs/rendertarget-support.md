# RenderTarget2D / RenderTargetCube Support Matrix

> **Metal adaptation note (2026-08-09):** Metal's current conservative contract supersedes the
> historical Metal cells that were added to this cross-renderer audit. MSAA and MRT now report
> unsupported and reject/clamp deterministically; backbuffer readback throws. See
> `docs/metal-renderer.md` for the evidence boundary.

Phase 39 (`plans/plan_graphics.md` Tasks 331–340) audited and pixel-verified `RenderTarget2D` and
`RenderTargetCube` conformance against FNA across all three graphics renderers (EasyGL, Vulkan,
Bgfx). This document summarizes the findings and closes the phase.

---

## 1. Constructor/property audits (Tasks 331–332)

`RenderTarget2D` (Task 331) and `RenderTargetCube` (Task 332) were each audited line-by-line
against FNA's C# source. Both classes' constructor overloads, `DepthStencilFormat`, and
`RenderTargetUsage` already matched FNA. Two real, fixed findings:

- `RenderTarget2D` was missing `IsContentLost`/`ContentLost` entirely (`RenderTargetCube` already
  had both) — fixed by adding them, mirroring `RenderTargetCube`'s exact pattern.
- `RenderTargetCube::GetTypeName()` was never overridden, so it incorrectly inherited
  `TextureCube`'s `"...TextureCube"` string instead of reporting its own type — fixed.

One architecture-blocked finding, **not fixed** (out of scope for a design-pass-free task):
`RenderTarget2D::Dispose(bool)` has a "still bound" guard (throws if disposed while bound);
`RenderTargetCube` cannot have the same guard added without an architecture change, because
`RenderTargetBinding` only stores `Texture*`, and `RenderTargetCube` inherits `GraphicsResource`
directly, not `Texture` (Task 863's already-tracked gap). Confirmed `GraphicsDevice::SetRenderTarget
(RenderTargetCube*, CubeMapFace)` never records the binding at all for the same reason — a direct
downstream consequence of Task 863, not a new independent bug.

## 2. Sampling after unbinding (Tasks 333–334)

**`RenderTarget2D`**: already pixel-verified working on EasyGL (Task 87) and Vulkan (Task 148)
before this phase — reconfirmed, no new work needed.

**`RenderTargetCube`**: no existing test covered sampling a cube render target back out via
`EnvironmentMapEffect` after unbinding. New tests found real bugs on 2 of 3 renderers:

- **EasyGL: works.** `EnvironmentMapEffect` reads the cube texture via `TextureCube::GetRenderer()`
  (shared with `RenderTargetCube`) and calls `BindGL()` through virtual dispatch — both the plain
  cube-texture and render-target-cube renderers override it correctly.
- **Vulkan: broken, two distinct bugs found** (see §7 for the Clear-only-RT gap, and §8 for the
  black-render bug specific to sampling a cube after rendering into its faces).
- **Bgfx: broken by the same unsafe-cast pattern** found for `RenderTarget2D` (see §5).

## 3. Depth buffer functionality (Task 335)

Task 331's property test only proved `getDepthStencilFormatProperty()` round-trips the constructor
argument — a stored value never connected to any GPU resource. Task 335 proved the FUNCTIONAL
behavior with a real pixel test: draws a near quad then a far quad into a `RenderTarget2D` with
`DepthFormat::Depth24Stencil8` and `DepthStencilState::Default`, and confirms the depth test
genuinely gates the draws on **both EasyGL and Vulkan** — not just a property.

Found (not fixed, tracked as **Task 877**) a real, scoped format-fidelity gap distinct from "does
it work at all": no renderer honors the *exact* requested `DepthStencilFormat` — EasyGL always
allocates depth-only `DepthComponent24` (zero stencil bits, ever); Vulkan ignores `hasDepth`
entirely (always allocates, using the device-global depth format); Bgfx always uses `D24S8`
(closest to correct, but not format-exact).

## 4. Mipmap support (Task 336) — real fix on EasyGL

Following FNA3D's actual native mechanism (`OPENGL_ResolveTarget`: a render target's full mip chain
auto-regenerates from level 0 via `glGenerateMipmap` when it stops being the active target — games
never render into non-zero mip levels directly), Task 336 **fixed** render target mipmaps on
EasyGL:

- `RenderTarget2D`/`RenderTargetCube`'s `LevelCount` now correctly reflects `mipMap` (previously a
  `mipMap ? 1 : 1` no-op).
- `EasyGLRenderTargetRenderer`/`EasyGLRenderTargetCubeRenderer` pre-allocate GPU storage for every
  mip level and regenerate the chain from level 0 on unbind.
- Discovered `IRenderTargetRenderer`/`IRenderTargetCubeRenderer::UnbindAsRenderTarget()` were
  **completely dead code** — never called by anything in the project. Fixed by adding
  `currentRt2D_`/`currentRtCube_` tracking to `EasyGLRenderer` so switching away from a
  bound target actually invokes it.
- Pixel-verified with a mip-completeness probe reusing the established
  `TextureFilter::Anisotropic`-renders-solid-black-on-incomplete-mip-chains signature (Task 867/299).

Vulkan/Bgfx accept-and-ignore the `mipMap` parameter (no functional change) — tracked as
**Task 878**, matching the project's existing precedent for regular `Texture2D`/`TextureCube`
(property correct everywhere, GPU support lags per-renderer).

## 5. MSAA support (Task 337) — real fix on EasyGL

Reusing Task 336's exact resolve-on-unbind mechanism, Task 337 **fixed** MSAA render targets on
EasyGL, following FNA's real algorithm (`MathHelper.ClosestMSAAPower` + `FNA3D_GetMaxMultiSampleCount`):

- `MultiSampleCount` now reflects the renderer's real, device-capability-clamped value (queried via
  `glGetIntegerv(GL_MAX_SAMPLES, ...)`), not a raw pass-through.
- `EasyGLRenderTargetRenderer`/`EasyGLRenderTargetCubeRenderer` render into a real multisampled color
  (+ depth) renderbuffer and resolve it via `glBlitFramebuffer` on unbind (same call site as
  Task 336's mip regeneration, correctly ordered: resolve first, then regenerate mips).
- Pixel-verified with a genuine anti-aliasing **differential** proof (a solid-fill-only test cannot
  distinguish "resolve preserves solid colors" from "resolve genuinely averages sub-pixel
  coverage"): a diagonal-edged triangle rendered at `MultiSampleCount=0` produces a hard binary
  edge; at `=8` it produces genuinely intermediate (blended) pixel values.

Key design distinction from `LevelCount`: `MultiSampleCount` is legitimately
renderer/device-capability-dependent even in real FNA, so Vulkan/Bgfx honestly reporting
`MultiSampleCount=0` (not implemented) is the *correct* per-renderer design, not a shortcut — tracked
as **Task 879**.

## 6. Bgfx wrong-handle-type casts (Tasks 873/874, found via Tasks 333–334)

`BgfxSpriteBatchRenderer::Draw` and `BgfxRenderer`'s `envMapping` branch each unconditionally
`static_cast` any `ITextureRenderer`/`ITextureCubeRenderer` to the plain-texture concrete type.
`RenderTarget2D`/`RenderTargetCube`'s renderers are unrelated sibling classes
(`BgfxRenderTargetRenderer`/`BgfxRenderTargetCubeRenderer`), whose first data member is a *framebuffer*
handle, not a *texture* handle — both handle types are `struct { uint16_t idx; }`, so the cast
compiles and doesn't crash, but reads a framebuffer-pool handle where a texture-pool handle is
expected, silently sampling wrong data. Confirmed by direct memory-layout analysis and new
doesn't-crash smoke tests on both `RenderTarget2D` and `RenderTargetCube`. Not fixed — tracked as
**Tasks 873/874**.

## 7. Vulkan Clear-only-RT gap (Task 875, found via Task 334)

`VulkanRenderer::Clear()` only records a global clear-colour scalar and never registers the
currently-bound render target as "used" — only an actual draw call does. A
`SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` pattern with **no draw call** in
between silently never gets a render pass recorded; the target's image stays
`VK_IMAGE_LAYOUT_UNDEFINED` forever. Confirmed via a Vulkan validation error. Not fixed — tracked as
**Task 875**.

## 8. Vulkan RenderTargetCube-via-EnvironmentMapEffect renders black (Task 876, found via Task 334)

Even after working around Task 875 (using a real `SpriteBatch` draw into each of a
`RenderTargetCube`'s 6 faces), sampling it back via `EnvironmentMapEffect` renders black instead of
the actual rendered content. The sampling path itself (`dynamic_cast<IVulkanCubeSamplable*>` +
`GetVkCubeImageView()`) is architecturally sound, unlike Bgfx's cast bug — something else in the
data chain is wrong. Root cause not isolated between two candidates (SpriteBatch-into-cube-face
correctness vs. `GetOrCreateEnvMapDescSet`'s descriptor caching). Not fixed — tracked as
**Task 876**.

## 9. `SetRenderTarget(nullptr)` returns to backbuffer (Task 338) — real fix, shared code

The core routing (does unbinding restore drawing to the actual backbuffer?) was already
extensively proven by dozens of existing tests. Auditing FNA's actual `SetRenderTargets` source
found a real, previously-undiscovered gap: FNA *always* resets `Viewport`/`ScissorRectangle` to
`(0, 0, newWidth, newHeight)` on every render-target switch (the new target's size when binding, or
the backbuffer's size when unbinding) — CNA never touched either property at all. **Fixed**: added
`GraphicsDevice::ResetViewportAndScissorForRenderTarget`, wired into all 3
`SetRenderTarget*`/`SetRenderTargets` overloads. Pixel-verified on both EasyGL and Vulkan with a
real GPU-level proof (`ScissorRectangle` is wired to the renderer's scissor test on all 3 renderers).

Found and deliberately deferred a much larger, separate gap discovered along the way:
`GraphicsDevice.Viewport` had **zero GPU wiring on the 3 renderers audited in that phase** — EasyGL,
Vulkan, and Bgfx hardcoded the full render-target/window size. Metal joined this document later;
its post-audit adaptation now preserves and submits the requested viewport (`METAL-265`), so Task
880's historical statement does not describe the current Metal renderer.

## 10. Multiple render targets with mixed formats (Task 339) — audit only, no bug

Read FNA's actual `SetRenderTargets` source: it performs **zero explicit validation** of
format/size/count mismatches between bound targets — it computes dimensions from the first target
only and delegates everything to the native driver. "Reject invalid combinations" is not an
XNA-level behavior. Confirmed CNA's own 3 renderers behave the same way (no CNA-level validation),
consistent with FNA — no bug.

Found one real, minor divergence: FNA's actual MRT cap is `MAX_RENDERTARGET_BINDINGS = 4`
(implicitly enforced via a fixed-size array that would throw past 4) — see §11 for the actual
per-renderer caps CNA uses instead, none of which match. Tracked as **Task 881**.

Deliberately did not touch the already-tracked, off-limits `EasyGL_MRT_TwoAttachments` bug
(Task 145) — even the basic same-size 2-target MRT case is already known-broken on EasyGL, which
blocks any meaningful deeper mismatched-format verification.

## 11. MRT limits per renderer (Task 340)

| Renderer | Simultaneous color attachments | Enforcement |
|---|---|---|
| EasyGL | 8 (hardcoded `constexpr int kMaxMRT = 8`) | Silently truncates anything beyond 8 — no error. |
| Vulkan | No CNA-level cap | Relies entirely on the actual GPU's `VkPhysicalDeviceLimits::maxColorAttachments`; `VulkanMRTProxy` sizes its internal vector to whatever `count` is passed, uncapped. |
| Bgfx | 8 (hardcoded `constexpr int kMaxAttachments = 8`, matching bgfx's own `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` default) | Silently truncates anything beyond 8 — no error. |
| **Real FNA/XNA** | **4** (`MAX_RENDERTARGET_BINDINGS`) | Implicit — a fixed-size internal array throws if a game passes more than 4. |

**None of CNA's 3 renderers match FNA's real 4-target limit** (confirmed Task 339 finding, tracked
as Task 881, not fixed — no test in this repo exercises more than 2 simultaneous targets). All 3
renderers additionally assume every bound target shares the first target's dimensions/format, with
zero CNA-level cross-target validation — matching FNA's own delegate-to-native-driver behavior
(§10), not a divergence.

---

## Summary: what actually works today, per renderer

| Feature | EasyGL | Vulkan | Bgfx | Metal |
|---|---|---|---|---|
| `RenderTarget2D`/`RenderTargetCube` constructors, properties, `IsContentLost`/`ContentLost` | ✅ | ✅ | ✅ | ✅ (shared `GraphicsDevice`-level code, renderer-independent) |
| `RenderTarget2D` sampling after unbind (`SpriteBatch`) | ✅ | ✅ | ❌ wrong-handle-cast bug (Task 873) | 🔍¹ adapted source has a real `MetalRenderTargetRenderer::colorTexture()` path to the resolved single-sample texture. Only the historical predecessor compiled/ran on macOS; the adapted Objective-C++ has no Apple compile/runtime or pixel proof. |
| `RenderTargetCube` sampling after unbind (`EnvironmentMapEffect`) | ✅ | ❌ renders black (Task 876) | ❌ wrong-handle-cast bug (Task 874) | 🔍 source-complete (`plans/plan_metal.md` Phase 6/11, `METAL-64`–`71`/`120`ff), never pixel-tested on real hardware — no dedicated `CTest` exists yet |
| Depth buffer functionality (does depth testing work inside an RT) | ✅ | ✅ | 🔍 not pixel-verified (no GPU readback) | 🔶 a real single-sample depth32+stencil8 texture is allocated and bound as the render pass's depth attachment — GPU-level depth-test *gating* itself is not independently pixel-verified after adaptation |
| Depth/stencil format fidelity (exact `DepthFormat` honored) | ❌ always `DepthComponent24`, no stencil (Task 877) | ❌ `hasDepth` ignored, always allocates (Task 877) | ❌ always `D24S8` (Task 877) | ❌ always `Depth32Float_Stencil8` regardless of requested `DepthFormat` — deliberate, documented simplification (`plans/plan_metal.md METAL-101`), matching Vulkan's own precedent exactly |
| Mipmap generation (`LevelCount`, real GPU mips) | ✅ (fixed, Task 336) | ✅ — real GPU mips, pixel-verified by `Vulkan_MsaaMipReadback` and `Vulkan_MrtMipFinalization`. **Previously** "`LevelCount` correct, no real GPU mips (Task 878)", which was true when written; corrected by `VULKAN-481` (D-02) rather than deleted, so the old claim is visibly superseded | 🔶 same as Vulkan (Task 878) | 🔍 adapted source encodes `generateMipmapsForTexture:` on every unbind (`plans/plan_metal.md METAL-103`) and tracks defined levels only after successful completion. Only the historical predecessor compiled on Apple; the adapted path has no Apple compile/runtime or pixel proof (`METAL-117` remains open). |
| MSAA (`MultiSampleCount`, real resolve) | ✅ (fixed, Task 337) | ✅ — real multisampling with resolve, pixel-verified by `Vulkan_MSAA_4x_Readback`, `Vulkan_MRT_MsaaResolve` and `Vulkan_MsaaDepthContract`; the applied count is device-clamped and reported truthfully (`VULKAN-347`). **Previously** "honestly reports `0`, not implemented (Task 879)"; corrected by `VULKAN-481` (D-02) | 🔶 same as Vulkan (Task 879) | ❌ deliberately unsupported: requested counts clamp to `0`, capability is false, and native attachments remain single-sample; the historical sample-count-four path produced a binary edge |
| `SetRenderTarget`/`SetRenderTargets` Viewport/ScissorRectangle reset | ✅ (fixed, Task 338) | ✅ (fixed, Task 338) | ✅ (fixed, Task 338; shared code, not independently pixel-tested) | ✅ shared `GraphicsDevice`-level code, renderer-independent |
| `Viewport`'s actual GPU effect (any sub-region viewport) | ❌ zero wiring (Task 880) | ❌ zero wiring (Task 880) | ❌ zero wiring (Task 880) | 🔍 adapted source preserves and submits the requested `GraphicsDevice.Viewport` unchanged on every encoder (`METAL-265`); portable state tests pass, but adapted Apple compile/runtime evidence is absent. |
| MRT (2+ same-size/format targets) | ❌ `EasyGL_MRT_TwoAttachments`, attachment 1 stays black (Task 145) | 🔍 not independently re-verified this phase | 🔍 not independently re-verified this phase | ❌ deliberately unsupported: count greater than one throws before active-target state changes; capability is false |
| MRT count cap matches FNA's `MAX_RENDERTARGET_BINDINGS=4` | ❌ caps at 8 instead (Task 881) | ❌ uncapped at CNA level (Task 881) | ❌ caps at 8 instead (Task 881) | ❌ supported-contract cap is one; zero restores the backbuffer and one binds a normalized 2D/cube-face descriptor |
| Vulkan `Clear()`-only RT (no draw) gets a render pass recorded | ❌ (Task 875) | N/A | N/A | N/A (Vulkan-specific) |

Legend: ✅ verified working · ❌ confirmed broken/gap · 🔶 partially correct (property right, GPU
support lags) · 🔍 not empirically verified this phase.

¹ The historical Metal run returned only the clear color for every backbuffer-readback test. The
adapted renderer therefore throws for backbuffer readback and does not register those pixel tests.
Historical Objective-C++ build evidence predates the current interfaces; a fresh macOS workflow
result is the external support-confidence boundary, not an integration blocker under the
repository's authoritative source-continuity policy. See `docs/metal-renderer.md`.

## Open, tracked follow-up work

Phase 39 opened 9 new tracked tasks while auditing/fixing `RenderTarget2D`/`RenderTargetCube`:

- **Task 873/874** — Bgfx wrong-handle-type casts (`RenderTarget2D` via `SpriteBatch`,
  `RenderTargetCube` via `EnvironmentMapEffect`). Fix shape identical for both — worth fixing
  together.
- **Task 875** — Vulkan `Clear()`-only RT never records a render pass.
- **Task 876** — Vulkan `RenderTargetCube`-via-`EnvironmentMapEffect` renders black; root cause not
  isolated.
- **Task 877** — no renderer honors the exact requested `DepthStencilFormat`.
- **Task 878** — Vulkan/Bgfx render target mip support (EasyGL already fixed).
- **Task 879** — Vulkan/Bgfx render target MSAA support (EasyGL already fixed).
- **Task 880** — `GraphicsDevice.Viewport` still has zero GPU wiring on EasyGL, Vulkan, and Bgfx
  (large, pre-existing, broader than render targets specifically); Metal's adapted path is closed
  separately by `METAL-265`, pending native Apple confidence evidence.
- **Task 881** — `SetRenderTargets`'s per-renderer MRT cap doesn't match FNA's real limit of 4.

Pre-existing, not opened this phase, still blocking deeper MRT work: `EasyGL_MRT_TwoAttachments`
(Task 145) — even a basic same-size 2-target MRT setup doesn't render correctly on EasyGL. Needs
its own dedicated root-cause investigation before Task 881 or any other MRT-adjacent work can be
meaningfully extended.

Metal joined this matrix later (`plans/plan_metal.md METAL-119`) and tracks its own open work in that
plan directly rather than duplicating a second tracking system here — see `plans/plan_metal.md` Phase 10
(`METAL-98`–`119`) for the full task list and narrative items 77/86/87/89/90 for the specific,
already-investigated findings the footnotes above reference.

This closes Phase 39 (`plans/plan_graphics.md` Tasks 331–340) in full.
