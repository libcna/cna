# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable 3D graphics backend layer. It is a
framework/runtime — not a game — designed so that XNA/FNA game code can be ported to C++ with
minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit tests and
  pixel-readback integration tests, verified against the authoritative FNA reference source
  (`/rv/data/library/github.com/FNA-XNA/FNA/src`).
- **Current development phase:** Phases 1–37 are now complete. **Phase 37 (DepthStencilState
  conformance, `GRAPHICS_TASKS.md` Tasks 311–319) closed this session** — all 9 tasks done (see §8
  for Phase 38's opening task). Headline results: Task 313 found a MASSIVE bug — Vulkan's
  `DepthStencilState` support is almost entirely fake (`DepthBufferFunction` ignored, stencil
  testing completely non-functional), tracked as **Task 870, not fixed**, the same shape/severity
  as Task 868's `BlendState` finding — reconfirmed FIVE more times across Tasks 314–318 (compare
  functions, stencil enable, masks, front-face ops, two-sided ops), always via a genuine contrast
  check (a lesson learned mid-Phase 37: a test where every check expects the same outcome can't
  distinguish "it works" from "the stencil test is bypassed entirely," since both look identical).
  Task 315 found and **fixed** a second severe bug: no EasyGL window ever requested
  `SDL_GL_STENCIL_SIZE`, so stencil testing was silently moot there too (correct code, missing GPU
  resource) — one-line SDL attribute fix, zero regressions. Task 319 (closing the phase) fixed a
  small `ReferenceStencil`-propagation bug (Task 309-shaped) and found a further universal,
  not-yet-fixed gap: `GraphicsDevice.ReferenceStencil`'s independent-override behavior has zero
  backend connection on all 3 backends (Task 872, distinct from Task 870). Also tracked: Task 871
  (`GraphicsDevice::Clear` ignores `ClearOptions::Stencil` everywhere). See §3/§5 for full details.
  **Phase 36 (BlendState conformance, Tasks 301–310) closed in an earlier session** — all 10 tasks
  done. **Task 304 found a MASSIVE bug: Vulkan's blend state support is almost entirely fake
  (Task 868, not yet fixed)** — reconfirmed a fifth time by Task 309, see §3/§5. Task 309 also
  found and fixed a real, universal `BlendFactor`-propagation bug
  (`GraphicsDevice.setBlendStateProperty` never forwarded a `BlendState`'s own `BlendFactor`) plus
  a Bgfx-specific gap where the computed blend-constant colour was never passed to
  `bgfx::setState()`. Task 310 confirmed FNA has no state-object freeze/immutability enforcement at
  all, but found a separate, real architectural divergence: CNA's `GraphicsDevice` stores
  `BlendState`/`DepthStencilState`/`RasterizerState` by value, unlike FNA's reference-type aliasing
  — tracked as Task 869, not fixed (see §3/§5). Phase 35 found and fixed a severe, project-wide bug
  (Task 293) and found-but-tracked a second one (Task 867, not yet fixed) — see §3/§5, and the new
  `docs/sampler-state-support.md` for the full Phase 35 writeup.
- **Key architectural decisions:**
  - Backend selection is **compile-time** via the `CNA_GRAPHICS_BACKEND` CMake option
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). EasyGL is primary and most heavily tested.
    SDL_Renderer is 2D-only by design (no 3D pipeline at all).
  - The `sharp-runtime` sibling repo (`../sharp-runtime/`) provides all `System.*` types and
    primitive type aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.
  - Vertex layout dispatch is **stride-keyed**: EasyGL/Vulkan/Bgfx select their GPU vertex
    layout from the raw byte stride of the bound buffer, not from `VertexDeclaration` contents.
    Only strides 16/20/24/32/52 are handled correctly.
  - `Texture3D` and `TextureCube` inherit `GraphicsResource` directly, **not** `Texture` — unlike
    FNA, where both inherit `Texture`. This is a known, documented architectural gap (see §5,
    §6) with real downstream consequences (texture-in-shader sampling, `EffectParameter` storage).

---

## 2. Current status

### Build status
All three backend build directories exist and were last rebuilt/verified in this session:
`cmake-build-debug` (EasyGL), `cmake-build-vulkan` (Vulkan), `cmake-build-bgfx` (Bgfx). All three
build cleanly from a from-scratch `cmake -B ... -DCNA_GRAPHICS_BACKEND=...` configure.

### Test status (last runs performed this session)
- **EasyGL (`cmake-build-debug`), full `ctest`:** 2079/2082 (serial `-j1`) pass. 3 pre-existing or
  expected failures (see §5): `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`
  (pre-existing, unrelated), and `EasyGL_GraphicsDevice_ReferenceStencil` (Task 319, new, expected
  — confirms Task 872, a universal gap present on EasyGL too). New
  `DepthStencilStateTest.*Name*` (Task 311),
  `GraphicsDeviceDefaultStateTest.DefaultDepthStencilStateMatches*`/`DefaultRasterizerStateMatches*`/`AssigningDepthStencilStatePropagatesReferenceStencil`
  (Tasks 312/319), `EasyGL_DepthStencilState_WriteEnable` (Task 313),
  `EasyGL_DepthStencilState_CompareFunction` (Task 314, all 5 `CompareFunction` checks),
  `EasyGL_DepthStencilState_StencilEnable` (Task 315, both checks — see the SDL stencil-buffer fix
  below), `EasyGL_DepthStencilState_StencilMask` (Task 316, all 4 checks),
  `EasyGL_DepthStencilState_StencilOps` (Task 317, all 4 checks including the contrast column), and
  `EasyGL_DepthStencilState_StencilTwoSided` (Task 318, both checks) all pass. Some tests have each
  been observed failing once under parallel `-j` execution but passed cleanly both in isolation and
  on a repeat serial full run — treated as parallel-execution flakiness, not a regression (none
  confirmed as a stable failure): `EasyGL_SkinnedBones`, `EasyGL_TransformMatrix_Translation`.
- **Vulkan (`cmake-build-vulkan`), full `ctest`:** 2007/2020 (serial `-j1`) pass. Only
  `Vulkan_DepthBias` (pre-existing, usual single sub-case), 5 confirmed-real, documented known
  failures — `Vulkan_BlendState_AlphaBlend`/`Additive`/`SeparateFunctions`/`SeparateFactors`/
  `BlendFactor` (all Task 304/306/307/308/309/868 findings, kept registered rather than hidden) —
  and the new
  `Vulkan_DepthStencilState_CompareFunction`/`StencilEnable`/`StencilMask`/`StencilOps`/`StencilTwoSided`
  (Tasks 314/315/316/317/318, all Task 870 confirmations) plus
  `Vulkan_GraphicsDevice_ReferenceStencil` (Task 319, Task 872 confirmation, kept registered rather
  than hidden) failed this run. `Vulkan_FillMode_WireFrame`'s pre-existing order-dependent flakiness
  recurred this run. `Vulkan_DepthStencilState_WriteEnable` (Task 313) passes cleanly —
  `DepthBufferWriteEnable` itself is confirmed correct on Vulkan, even though
  `DepthBufferFunction`/stencil testing are separately confirmed broken there (Task 870, see §5).
  **Caution for future sessions:** running the EasyGL and Vulkan full `ctest` suites concurrently
  produced transient false failures in an earlier session (see the git history in §3) — prefer
  running the two backends' full suites sequentially, not concurrently. `Vulkan_DepthBias`'s
  `DepthBias=-1e6` sub-case fails consistently (pre-existing, documented).
  `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage` remain the same pre-existing,
  order/timing-sensitive issues tracked since Task 279 — flaky (not stable failures), unrelated to
  any change made this session. **Task 315 made a real EasyGL backend source change** (not just an
  example/test file): added `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)` to
  `EasyGLGraphicsBackend`'s context creation — full ctest reconfirmed zero regressions on both
  backends after this change, and again after each of Tasks 316-319's new tests (Task 319 also
  touched `GraphicsDevice.cpp` itself, for the `ReferenceStencil` propagation fix).
- **Bgfx (`cmake-build-bgfx`), full `ctest`:** **1985/1985 (100%)** as of Task 309's fix. Not
  rebuilt/rerun for Tasks 311–319 (all EasyGL/Vulkan-only changes — `DepthStencilState`'s value
  layout, `GraphicsDevice`'s constructor member-init list and `ReferenceStencil` propagation fix,
  seven new EasyGL/Vulkan-registered pixel tests, and the EasyGL-specific `SDL_GL_STENCIL_SIZE`
  fix — none touch Bgfx). Bgfx's own
  `ApplyDepthStencilState` was already confirmed fully correct by code reading (Task 870's
  write-up) — whether Bgfx's window/backbuffer actually has a physical stencil buffer (the same
  class of gap just found and fixed on EasyGL) has NOT been verified; worth checking before
  assuming Bgfx's stencil support is any better than EasyGL's was before this session's fix. Bgfx
  has no GPU pixel-readback API in this project, so its integration coverage remains
  smoke-test-only by design.

### What currently works
- Full `Texture2D`/`Texture3D`/`TextureCube` construction, `SetData`/`GetData` (including
  arbitrary x/y/z or x/y/rect sub-regions, `startIndex`, and mip levels — mip levels only
  confirmed fixed on EasyGL `TextureCube`, see §5), argument-guard validation, and `Dispose`.
- `CubeMapFace` range validation (throws for an out-of-range enum value — a CNA safety extra;
  confirmed FNA itself never validates this).
- `SurfaceFormat` enum now has all 27 values matching FNA exactly, including ordinals — a real
  conformance bug (7 wrong/invented values at ordinals 20–26) was found and fixed this session.
- `Texture.GetBlockSizeSquaredEXT`, `GetFormatSizeEXT`, `GetPixelStoreAlignment`,
  `ValidateGetDataFormat` — all four ported from FNA's `Texture.cs`, wired into all real
  `GetData` call sites. Currently a no-op in practice (only `SurfaceFormat::Color` is supported
  anywhere), but this is deliberate, correct, forward-looking infrastructure.
- `EnvironmentMapEffect` (`TextureCube` reflection mapping) now works correctly on **all three**
  backends — EasyGL and Vulkan were already correct; Bgfx had zero code path for it until this
  session (Task 278).
- Vulkan rendering is now colorspace-correct: `Texture2D` and the swapchain were both incorrectly
  using sRGB GPU formats, silently gamma-distorting all non-textured rendering (fixed Task 284).
- `SpriteBatch`: all 20 of FNA's real public methods (1 constructor, 5 `Begin` overloads, `End`,
  7 `Draw` overloads, 6 `DrawString` overloads) are implemented with matching signatures. 4
  CNA-only convenience additions exist alongside them, now correctly `NOXNA`-tagged.

### What currently works (continued)
- `SamplerState`'s 6 static presets now correctly set `Name` (e.g. `"SamplerState.PointClamp"`)
  matching FNA, fixed in Task 291 — previously silently empty on every preset.
- `GraphicsDevice.SamplerStates`/`VertexSamplerStates` now correctly default every one of the 16
  slots to `SamplerState.LinearWrap` (matching FNA byte-for-byte, including `Name`), fixed in
  Task 292 — previously each slot was a default-constructed `SamplerState` (functionally identical
  filter/address values, but empty `Name`, and completely untested before this task).
- **`GraphicsDevice.SamplerStates`/`VertexSamplerStates` are now actually honored by 3D stock-effect
  draws, confirmed on all three backends** (Task 293, severe finding — see below for the full
  story). Any game code drawing textured 3D geometry via
  `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` (the normal way to use
  `BasicEffect`/`DualTextureEffect`/`AlphaTestEffect`/`EnvironmentMapEffect`/`SkinnedEffect`)
  previously had its assigned `SamplerState` silently ignored on every backend — `TextureAddressMode`
  and `TextureFilter` had zero effect. Three separate, backend-specific root causes, all fixed:
  **EasyGL/shared `GraphicsDevice`** — all 18 `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/
  `DrawInstancedPrimitives` overloads skipped the `applySamplerStatesToBackend()` call that
  `DrawPrimitives`/`DrawIndexedPrimitives` correctly make; fixed by adding it to all 18.
  **Vulkan** — `GetOrCreateDualTexDescSet` hardcoded `defaultSampler_` into both descriptor slots
  and cached the descriptor set keyed only by image views, ignoring the correctly-computed
  `slotSamplers_[0]`/`[1]`; fixed by threading both samplers through and folding them into the cache
  key. **Bgfx** — the dual-texture draw branch bound texture slot 1 using `samplerFlags_[0]` instead
  of `samplerFlags_[1]`, so the second texture always inherited slot 0's sampler; fixed to use the
  correct index. **This directly unblocks Tasks 294–299** (pixel tests for `Clamp`/`Wrap`/`Mirror`,
  `Point`/`Linear` filtering, mipmap filters, anisotropic filtering) — before this fix, every one of
  those tests would have failed identically for this same root cause, regardless of what each task's
  title suggests it's individually testing. Verified: `EasyGL_SamplerState_DualTextureEffect` and
  `Vulkan_SamplerState_DualTextureEffect` both pixel-verify the fix (same test source, reused);
  Bgfx has no pixel-readback API so its fix is confirmed only by full-suite no-regression (1977/1977).

### What does NOT work yet
- **`Texture2D::SetData(level>0,...)` is a total silent no-op on both Vulkan and Bgfx** — same
  bug shape and severity as the `Texture3D`/`TextureCube::GetData` finding below, but for the most
  commonly used texture type. `ITextureBackend::UpdatePixelsLevel` has an empty default body;
  neither backend overrides it. Found while building Task 298's mipmap-filter test — the uploaded
  higher-mip-level colour never appeared on Vulkan, which traced back to this, not a filter/sampler
  issue. Vulkan additionally hardcodes `VkImageCreateInfo::mipLevels=1`,
  `VkImageViewCreateInfo::levelCount=1`, and never sets sampler `minLod`/`maxLod` (defaulting to 0,
  clamping automatic LOD selection to level 0 regardless of filter) — three more fixes needed
  together with `UpdatePixelsLevel` for Vulkan `Texture2D` mips to work at all. **Also affects
  EasyGL differently**: `TextureFilter::Anisotropic` (and every other `*Mip*`-suffixed filter)
  renders **solid black** on any ordinary single-level `Texture2D` (the common case,
  e.g. `Texture2D::CreateFromPixels`) — a classic GL mipmap-incomplete-texture symptom, since
  EasyGL never sets `GL_TEXTURE_MAX_LEVEL` to match a texture's real level count. Vulkan does not
  share this symptom. Tracked as Task 867, not fixed this session (multi-part, three-backend
  fix needing its own dedicated pass).
- **Anisotropic filtering is inconsistent across backends**: Vulkan correctly queries the real
  device cap and clamps `SamplerState.MaxAnisotropy` to it; EasyGL has zero anisotropy support at
  all (`TextureFilter::Anisotropic` silently falls back to plain trilinear — the underlying
  `easy-gl` library has no anisotropy API whatsoever); Bgfx enables the effect via sampler flags
  but ignores the requested `MaxAnisotropy` level entirely. Not a crash risk (verified: an extreme
  `MaxAnisotropy=9999` doesn't crash on any backend) — just inconsistent visual fidelity. Task 299
  finding, not tracked as a fix task (EasyGL's gap would require adding real anisotropy support to
  the `easy-gl` library itself, out of scope for a CNA-side fix).
- `Texture3D`/`TextureCube::GetData` is a **total silent no-op on both Vulkan and Bgfx** — neither
  backend overrides the base class's empty default implementation. Calling it leaves the output
  buffer completely untouched, with no error (Task 280 finding, tracked as Task 865).
- `Texture3D` sampling is not wired into any shader on any backend, and cannot be, without an
  architecture change (`Texture3D`/`TextureCube` would need to inherit `Texture` to fit into
  `GraphicsDevice.Textures[slot]`, or a parallel binding path would need to be added) — Task 277
  finding, tracked as Task 863.
- `TextureCube::DDSFromStreamEXT` is a non-functional stub: it ignores its `stream` argument and
  always returns a blank 1×1 texture. Fails silently, not loudly. Tracked as Task 663.
- Vulkan and Bgfx very likely have the same "mip level >0 GPU storage never allocated" bug that
  was found and fixed for EasyGL's `TextureCube` (Task 276) — for **both** `Texture3D` and
  `TextureCube`, on **both** backends. Flagged but not reproduced with a test. Tracked as Task 864.
- `SpriteBatch`'s `SamplerState` (from `Begin()`) is a no-op on Vulkan and Bgfx — only EasyGL
  applies it.
- Multiple `SpriteBatch::Begin()`/`End()` calls per frame on Vulkan: only the last batch renders.
- `SurfaceFormat` support is effectively Color-only everywhere — `Texture::ValidateFormat` throws
  for every other value. `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT`/`ValidateGetDataFormat` exist
  and are correct, but nothing in any backend actually maps a non-`Color` format to a real GPU
  format yet (this is exactly Phase 34's remaining scope, Tasks 286–290 now that Task 285's CPU-side
  packing is verified).

---

## 3. Recent changes

Most recent first. Earlier history (everything before Task 271) is in `GRAPHICS_TASKS.md`, not
repeated here.

| Commit / Task | Files | Change |
|---|---|---|
| (uncommitted) Tasks 318-319 | `examples/easygl_depthstencilstate_stencil_twosided_test.cpp` (new), `examples/easygl_graphicsdevice_reference_stencil_test.cpp` (new), `GraphicsDevice.cpp` (fix), `GraphicsDeviceDefaultStateTests.cpp` (extended), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (Task 870 updated, new Tasks 872) | **Closes Phase 37.** Task 318: 2-column genuinely differential pixel test for `TwoSidedStencilMode`/back-face (CCW) stencil ops, built as a proper differential test from the start (applying Task 317's lesson immediately): both columns draw the SAME deliberately back-facing triangle (`CullMode=None` + reversed winding), differing only in `TwoSidedStencilMode`. **EasyGL: both pass exactly as predicted.** **Vulkan: the "should pass" column passes by coincidence, the contrast column fails — a fifth, clean reconfirmation of Task 870.** Task 319: found and fixed a small Task-309-shaped bug (`setDepthStencilStateProperty` never propagated an assigned state's own `ReferenceStencil` into `GraphicsDevice`'s own field) — fixed, new unit test confirms it. **Found a SECOND, universal, NOT-fixed bug while verifying independent-override behavior with a real pixel test**: `GraphicsDevice::setReferenceStencilProperty` has zero backend connection on all 3 backends (`IGraphicsBackend` has no `SetReferenceStencil` method at all) — confirmed failing identically on **both** EasyGL and Vulkan, unlike Task 870 which is Vulkan-specific. Tracked as new Task 872. |
| `95abf99` Task 317 | `examples/easygl_depthstencilstate_stencil_ops_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (Task 870 updated) | 4-column pixel test: 3 isolate one front-face `StencilOperation` slot each (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`), verified via stamp→operation→read-back; a 4th contrast column (deliberately-wrong read-back reference) is critical for validity — without it, all 3 op-slot checks expect the same PASS outcome a fully-bypassed stencil test would *also* produce (discovered mid-development when an earlier 3-column version coincidentally passed on Vulkan too). **EasyGL: all 4 pass exactly as predicted.** **Vulkan: 3 of 4 pass by coincidence, the contrast column correctly fails — a fourth, clean reconfirmation of Task 870.** Kept `Vulkan_DepthStencilState_StencilOps` registered as a documented known failure. |
| `d86c1f4` Task 316 | `examples/easygl_depthstencilstate_stencil_mask_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (Task 870 updated) | 4-column pixel test, 2 differential pairs for `StencilMask` (read mask) and `StencilWriteMask` (write mask), each pairing a narrow mask (expect PASS) against a full/no-op mask (expect FAIL) so opposite outcomes prove the mask is genuinely applied. **EasyGL: all 4 pass exactly as predicted.** **Vulkan: 2 of 4 fail — a third, clean reconfirmation of Task 870** (both masks are discarded, unused parameters in `ApplyDepthStencilState`; the 2 checks expecting PASS pass only by coincidence since nothing ever gates on Vulkan, while the 2 contrast checks correctly fail). Kept `Vulkan_DepthStencilState_StencilMask` registered as a documented known failure. |
| `c1d8e74` Task 315 | `examples/easygl_depthstencilstate_stencil_enable_test.cpp` (new), `EasyGLGraphicsBackend.cpp` (fix), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (new Task 871, Task 870 updated) | Two-column differential pixel test for `StencilEnable`. **Found and fixed a real, severe bug**: no EasyGL window ever requested `SDL_GL_STENCIL_SIZE`, so no window had a real stencil buffer at all — the stencil test trivially always-passed at the GL level regardless of `StencilEnable`, even though the state-application code (`glStencilFunc`/`glStencilOp`/`glEnable(GL_STENCIL_TEST)`) was already correct. Fixed with a one-line SDL attribute addition. **After the fix, both checks pass on EasyGL.** **Vulkan: Check A fails outright — a full, direct reconfirmation of Task 870** for stencil specifically; also found `FindDepthFormat()` prefers a stencil-less format as a compounding cause, noted in Task 870. **New Task 871, NOT fixed**: `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` on every backend — found while needing to work around it for this test's stencil baseline. |
| `65d3d21` Task 314 | `examples/easygl_depthstencilstate_compare_function_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (Task 870 updated) | 5-column pixel test, one per `CompareFunction` (`Always`/`Never`/`Less`/`LessEqual`/`Greater`), each with a depth chosen to give an unambiguous, function-distinguishing expected result. **EasyGL: all 5 pass exactly as predicted.** **Vulkan: 4 of 5 fail — a full, direct reconfirmation of Task 870** (only `Less` passes, exactly matching the hardcoded `VK_COMPARE_OP_LESS` pipeline predicted by Task 313's finding). Kept `Vulkan_DepthStencilState_CompareFunction` registered as a documented known failure. |
| `eccbb9e` Task 313 | `examples/easygl_depthstencilstate_write_enable_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (new Task 870) | Three-quad differential pixel test confirms `DepthBufferWriteEnable` works correctly on both EasyGL and Vulkan. **Found and documented two real, separate discoveries while designing it**: (1) Vulkan's `colored3d.vert.glsl` expects clip-space Z in DirectX's `[0,+w]` range (matching real XNA semantics) — a raw negative Z silently clips away entirely on Vulkan only, a gotcha for any future identity-matrix depth pixel test in this codebase. (2) **A new MASSIVE bug, tracked as Task 870, NOT fixed**: `VulkanGraphicsBackend::ApplyDepthStencilState` ignores `DepthBufferFunction` (hardcoded per-pipeline as `LESS`/`LESS_OR_EQUAL`) and the entire stencil-test parameter set (stencil testing is completely non-functional on Vulkan) — the same shape/severity as Task 868's `BlendState` finding. EasyGL/Bgfx both fully correct by contrast. Expect Tasks 314 (`DepthBufferFunction`) and 315–319 (stencil) to hit this directly. |
| `a1bcf20` Task 312 | `GraphicsDevice.cpp`, `GraphicsDeviceDefaultStateTests.cpp` (extended) | **Found and fixed a THIRD bug of the same shape as Task 302, flagged by Task 311.** `GraphicsDevice`'s `depthStencilState_`/`rasterizerState_` were plain default-constructed, never copied from `DepthStencilState::Default`/`RasterizerState::CullCounterClockwise` (FNA's actual constructor behavior). Fixed via the constructor's member-init list, ordered to avoid `-Wreorder`. 3 new tests. |
| (uncommitted) Task 311 | `DepthStencilState.hpp/.cpp`, `DepthStencilStateTests.cpp`, `GRAPHICS_TASKS.md` (Task 866 updated) | **Opens Phase 37.** Audited `DepthStencilState` against FNA — full 16-property surface, all 3 presets, default values all already matched exactly. Fixed the known Task 866 gap: presets didn't set `Name` (e.g. `"DepthStencilState.Default"`). Mirrors Tasks 291/301 exactly. Closes `DepthStencilState`'s portion of Task 866; `RasterizerState` remains open. 5 new tests. Flagged (not fixed here) the Task-302-shaped `GraphicsDevice` default-state bug fixed immediately after by Task 312. |
| `52326b3` Task 310 | `GraphicsDeviceDefaultStateTests.cpp` (extended), `GRAPHICS_TASKS.md` (new Task 869) | **Closes Phase 36.** Confirmed via FNA source that `BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerState` have NO freeze/immutability enforcement at all — no bug, CNA matches (also none). Found a real, separate architectural divergence while verifying: FNA's `GraphicsDevice` state setters store a *reference* to the assigned object (mutating it afterward changes what's applied); CNA stores all of them *by value* (a deliberate, consistent, project-wide pattern, not `BlendState`-specific), so post-assignment mutation is silently a no-op in CNA. New test pins this behavior. Tracked as Task 869, not fixed — needs a dedicated architecture decision if ever pursued. |
| `239aba5` Task 309 | `GraphicsDevice.cpp`, `BgfxGraphicsBackend.cpp`, `examples/easygl_blendstate_blendfactor_test.cpp` (new), `CMakeLists.txt` | **Found and fixed a real, universal bug**: `GraphicsDevice::setBlendStateProperty` never propagated a `BlendState`'s own `BlendFactor` to the device (FNA applies it atomically as part of the whole native blend-state struct); fixed by calling `setBlendFactorProperty` from within `setBlendStateProperty`. New test proves it on EasyGL (`200,100,0` exactly as predicted). **Fails on Vulkan — a fifth confirmation of Task 868**, not a new bug (hardcoded equation never selects `VK_BLEND_FACTOR_CONSTANT_COLOR`). Also fixed a separate Bgfx-specific gap found while investigating: `blendFactorPacked_` was computed but never passed to any of Bgfx's 5 `bgfx::setState()` call sites — all 5 fixed (smoke-tested only, no Bgfx readback API). `MultiSampleMask` confirmed a complete, universal no-op across all 3 backends (documented, not fixed — no consumer, would need new backend-interface surface). |
| (uncommitted) Task 308 | `examples/easygl_blendstate_separate_factors_test.cpp` (new), `CMakeLists.txt` | Mirrors Task 307 but for blend *factors* (`ColorSourceBlend`/`ColorDestinationBlend` vs `Alpha*Blend`) instead of functions. Two checks with alpha factors swapped, expecting opposite colour results (source-only vs destination-only). Both pass on EasyGL exactly as predicted. **On Vulkan, Check A coincidentally passes and Check B cleanly fails — a fourth confirmation of Task 868**, same asymmetric-pass pattern as Task 305. |
| (uncommitted) Task 307 | `examples/easygl_blendstate_separate_functions_test.cpp` (new), `CMakeLists.txt` | New test building 2 custom `BlendState`s to verify `ColorBlendFunction`/`AlphaBlendFunction` are independent (none of the 4 presets vary `BlendFunction`). Both checks pass exactly as predicted on EasyGL. **Both fail on Vulkan — a third, clean confirmation of Task 868**: both readbacks show the unmodified source colour, since Task 868's hardcoded destination factor evaluates to exactly 0 at alpha=255, making the requested `BlendFunction` moot regardless of which one is set. |
| (uncommitted) Task 306 | `examples/easygl_blendstate_additive_test.cpp` (new), `CMakeLists.txt` | New `Additive` pixel test checking both saturation (`255+200` clamps, no wraparound) and full-destination-add behaviour (`colorDst=One`, unlike `AlphaBlend`/`NonPremultiplied`'s `InverseSourceAlpha`). Passes on EasyGL exactly as predicted. **Fails on Vulkan, definitively re-confirming Task 868** — unlike Task 305's coincidental pass, this test's destination-factor mismatch is unambiguous (`G=100` instead of `150`, the destination-dropped signature). Kept registered as a second documented known failure. |
| (uncommitted) Task 305 | `examples/easygl_blendstate_nonpremultiplied_test.cpp` (new), `CMakeLists.txt` | New `NonPremultiplied` pixel test (raw non-premultiplied 50%-alpha red over green), mirroring Task 304's `AlphaBlend` test in reverse — together they prove which stage performs the alpha multiplication. Passes on EasyGL as predicted. **Passes on Vulkan too, but only by coincidence** — Task 868's hardcoded Vulkan blend equation's colour factors happen to match `NonPremultiplied`'s exactly, so this can't be read as evidence Task 868 is fixed; flagged prominently in the test's own file header. No new bug, closes real `NonPremultiplied` coverage. |
| `305275a` Task 304 | `examples/easygl_blendstate_alphablend_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (new Task 868) | EasyGL passes exactly as predicted (correct premultiplied-alpha semantics). **Found a MASSIVE, confirmed bug on Vulkan**: `ApplyBlendState` ignores every specific blend-factor/function request, hardcoding a single equation (`NonPremultiplied`'s) for anything except exactly `Opaque`. Confirmed on real hardware (`R=64` instead of expected `R=128`, the double-alpha-multiplication signature). Also found a narrower, related gap in Bgfx (alpha factors/blend functions ignored) while cross-checking; EasyGL confirmed fully correct by contrast. Kept the failing Vulkan test registered as a documented known failure rather than hidden. Tracked as Task 868, not fixed — expect it to resurface in most remaining Phase 36 Vulkan tests. |
| `a17c87f` Task 303 | `examples/easygl_blendstate_opaque_test.cpp` (new), `CMakeLists.txt` | Found Task 255's existing `BlendState::Opaque` pixel test only uses a fully-opaque source colour, which can't distinguish `Opaque` from `AlphaBlend` (their math collapses to the same result at alpha=255). New test uses alpha=128 specifically, confirming destination + alpha are both genuinely discarded. Passed on both backends immediately — no bug found, closes a real coverage gap. |
| `812dbb7` Task 302 | `GraphicsDevice.cpp`, `GraphicsDeviceDefaultStateTests.cpp` (new) | **Found and fixed a real bug, same shape as Task 292.** `GraphicsDevice.blendState_` was a plain default-constructed `BlendState`, never copied from `BlendState::Opaque` (FNA's actual default) — values coincidentally matched, invisible until Task 301 gave `Opaque` a `Name`. Fixed via the constructor's member-init list. Flagged, not fixed: `depthStencilState_`/`rasterizerState_` are likely the same bug, out of this task's scope. |
| `2db584a` Task 301 | `BlendState.hpp/.cpp`, `BlendStateTests.cpp` | **Opens Phase 36.** Audited `BlendState` against FNA — property surface, all 4 presets, default values all already matched exactly. Fixed the known Task 866 gap: presets didn't set `Name` (e.g. `"BlendState.Additive"`). Mirrors Task 291's `SamplerState` fix exactly. Closes Task 866's `BlendState` portion; `DepthStencilState`/`RasterizerState` remain open for their own later audit tasks. 6 new tests. EasyGL 2054/2056, Vulkan 1992/1994, both only pre-existing failures. |
| `1646589` Task 300 | `docs/sampler-state-support.md` (new) | **Closes Phase 35.** Synthesizes Tasks 291–299's findings into one reference doc: API conformance, the central Task 293 per-slot-binding bug and its 3-backend fix, `TextureAddressMode`/`TextureFilter`/mipmap/anisotropic coverage, and a per-backend support matrix. Links to Tasks 866/867 rather than duplicating them. |
| (uncommitted) Task 299 | `examples/easygl_texture_anisotropic_effect_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (Task 867 extended) | Audited `TextureFilter::Anisotropic`/`MaxAnisotropy` on all 3 backends: Vulkan correct (real device-cap query + clamp); EasyGL has zero anisotropy support at all (silently falls back to trilinear — underlying `easy-gl` library has no API for it); Bgfx enables the effect but ignores the requested level entirely. New test verifies the task's literal "caps and fallback" ask: `MaxAnisotropy=9999` (far beyond any cap) doesn't crash on either backend. Found an additional severe finding building it: `TextureFilter::Anisotropic` (and every `*Mip*` filter) renders solid black on any ordinary single-level `Texture2D` on EasyGL (GL mipmap-incompleteness) — same root cause as Task 867, scope extended to cover it, not fixed. |
| (uncommitted) Task 298 | `examples/easygl_texture_mip_filter_effect_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` (new Task 867) | EasyGL: verified real mip-level selection works for explicit `Mip*` filters (`LinearMipPoint` correctly samples a high mip level at 8x8px on a 128-texel texture); confirmed `Point`/`Linear` are deliberately non-mip-aware on EasyGL (documented tradeoff, avoids GL-incomplete textures). **Found a severe, separate bug while testing Vulkan**: `Texture2D::SetData(level>0)` is a total silent no-op on Vulkan/Bgfx (`UpdatePixelsLevel` never overridden) — same class as Task 865. Vulkan additionally hardcodes `mipLevels=1`/`levelCount=1`/never sets sampler `minLod`/`maxLod`. Un-registered the confounded Vulkan test rather than leave it misleadingly failing; tracked the full finding as new Task 867 (not fixed — multi-part, needs its own pass). |
| (uncommitted) Task 297 | `examples/easygl_texture_filter_point_vs_linear_test.cpp` (new), `CMakeLists.txt` | First genuinely new-ground Phase 35 test — no prior test touched `TextureFilter`. New test draws 4 columns in one frame (magnification/minification × Point/Linear) on `DualTextureEffect`, sampling each at a texel boundary; `Point` reads pure, `Linear` reads a ~50/50 blend, in both scales. All 4 checks passed on both backends immediately — no bug found. Documented that magnification/minification share identical sampler math in CNA (flat `TextureFilter`→min+mag pair, no mipmaps), so this task's two scales confirm robustness at scale, not a distinct code path. |
| `29c4b06` Task 296 | `examples/easygl_texture_address_mode_mirror_effect_test.cpp` (new), `CMakeLists.txt` | New `Mirror` pixel test on `DualTextureEffect`, mirroring Tasks 293/294's pattern. FNA has no `PointMirror` preset, so this builds a custom `SamplerState`. Deliberately sampled at raw `u=1.6` (not `1.25`, where `Mirror` and `Clamp` coincidentally agree) so only a genuinely correct `Mirror` implementation passes. All 3 backends already correctly mapped `Mirror` to their GPU's mirrored-repeat mode — no bug found, pure coverage addition. Registered and passing on both EasyGL and Vulkan. |
| (uncommitted) Task 295 | `GRAPHICS_TASKS.md` only | **Already fully satisfied, no new code** — Task 293's own fix-proof test already is a `TextureAddressMode::Wrap` pixel test on a 3D stock effect. Documented the mapping so it isn't duplicated. |
| `f70a169` Task 294 | `examples/easygl_texture_address_mode_clamp_effect_test.cpp` (new), `CMakeLists.txt` | Confirmed Task 269's existing address-mode test is `SpriteBatch`-only (a different code path from Task 293's fix), so a real 3D-stock-effect test was needed. New test mirrors Task 293's `DualTextureEffect` pattern with `SamplerState::PointClamp` (expects the opposite, edge-extend/`GREEN` result vs. `PointWrap`'s repeat/`RED`). Registered on both EasyGL and Vulkan — both pass, confirming Task 293's fix holds for `Clamp` too, not just `Wrap`. |
| `8649227` Task 293 (+Vulkan/Bgfx follow-up) | `GraphicsDevice.cpp` (18 sites), `VulkanGraphicsBackend.cpp/.hpp`, `BgfxGraphicsBackend.cpp`, `examples/easygl_sampler_state_effect_test.cpp` (new), `CMakeLists.txt` | **Found and fixed a severe, project-wide bug across all 3 backends.** EasyGL: all 18 `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads skipped `applySamplerStatesToBackend()`; fixed. Vulkan: `GetOrCreateDualTexDescSet` hardcoded `defaultSampler_` and cached by view-only key, ignoring `slotSamplers_`; fixed by threading samplers through + widening the cache key. Bgfx: dual-texture draw used `samplerFlags_[0]` for both texture slots instead of `samplerFlags_[1]` for slot 1; fixed. New pixel test (`DualTextureEffect` + `SamplerStates[0]=PointWrap`, UV past 1.0) proves the fix on both EasyGL and Vulkan (reused test source, registered as both `EasyGL_SamplerState_DualTextureEffect` and `Vulkan_SamplerState_DualTextureEffect`); Bgfx confirmed via full-suite no-regression only (no pixel-readback API). Along the way, also verified and committed a pre-existing, unrelated uncommitted fix in the sibling `sharp-runtime` repo (`BitConverter.hpp` `System::Single` ambiguity) that had been silently blocking any Vulkan build. Directly unblocks Tasks 294–299. All three backends' full ctest suites reconfirmed with zero regressions: EasyGL 2043/2045, Vulkan 1983/1984, Bgfx 1977/1977 (100%). |
| (uncommitted) Task 292 | `SamplerStateCollection.cpp`, `SamplerStateCollectionTests.cpp` (new) | Found and fixed a real bug uncovered directly by Task 291: `SamplerStateCollection`'s constructor default-constructed each of 16 slots instead of copying `SamplerState::LinearWrap` (FNA's actual behavior). Values coincided (both Linear+Wrap×3) so this was invisible until `Name` existed to distinguish them. Zero prior test coverage for this class or `GraphicsDevice`'s sampler defaults existed. Fixed + added a full new test file. 2042/2044 EasyGL ctest pass serially. |
| (uncommitted) Task 291 | `SamplerState.hpp/.cpp`, `SamplerStateTests.cpp` | **Opens Phase 35.** Audited `SamplerState` against FNA: property surface, all 6 presets, and default values already matched FNA exactly. Real finding: FNA sets `Name` on every preset (e.g. `"SamplerState.PointClamp"`); CNA's private preset constructor never did. Fixed by threading a `name` param through the constructor. Also found the same gap exists in `BlendState`/`DepthStencilState`/`RasterizerState` — tracked separately as Task 866, not fixed here (scope discipline). 8 new tests. 2032/2034 EasyGL ctest pass serially. |
| `7c3e051` Task 290 | `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | **Closes Phase 34.** Found 7 of 27 `SurfaceFormat` values still untested for throw-behavior (`Bgra5551`/`Bgra4444`/`Dxt3`/`Dxt5`/`Rg32`/`ByteEXT`/`UShortEXT`). Added one exhaustive `EverySurfaceFormatEitherWorksOrThrowsClearly` test iterating all 27 values instead of more one-off tests. Confirmed `Texture::ValidateFormat` is backend-agnostic (called from the shared `Texture2D`/`Texture3D`/`TextureCube` constructors), so this holds for Vulkan/Bgfx too. No bug found. |
| `5bf517f` Task 289 | `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | Continues Phase 34. Same verify-only shape as Tasks 285–288: `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` for `HdrBlendable`/`Rgba1010102`/`Rgba64` were already correct (Task 282); fallback status already documented (Task 281's `docs/surface-format-support.md`). Added the missing throws-clearly tests. No bug found. |
| `1d8c859` Task 288 | `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | Continues Phase 34. Same verify-only shape as Tasks 285–287: `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` for `HalfSingle`/`HalfVector2`/`HalfVector4` were already correct (Task 282) and no backend special-cases them. Added the missing throws-clearly tests. No bug found. |
| `ef25803` Task 287 | `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | Continues Phase 34. Same verify-only shape as Tasks 285/286: `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` for `Single`/`Vector2`/`Vector4` were already correct (Task 282) and no backend special-cases them. Added the missing throws-clearly tests (`SingleThrows`/`Vector2Throws`/`Vector4Throws`). No bug found. |
| `23ea5d9` Task 286 | `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` | Continues Phase 34. **Found the task's own premise was half wrong:** `NormalizedShort2`/`NormalizedShort4` are not `SurfaceFormat` texture values in FNA at all (confirmed via FNA source) — they're `VertexElementFormat`-only, already correctly implemented (Vulkan/Bgfx real GPU mapping, matching ordinals, existing tests). `NormalizedByte2`/`NormalizedByte4` *are* real `SurfaceFormat` values; their CPU-side `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` were already correct and tested (Task 282). The one real gap: no test confirmed `Texture2D` construction actually throws for these two formats. Added 2 tests (`UnsupportedFormatConstructionTest.NormalizedByte2Throws`/`NormalizedByte4Throws`). No bug found — closed a narrow test-coverage gap, same shape as Task 285. |
| `a1012de` Task 285 | `tests/Microsoft/Xna/Framework/Graphics/PackedVector/PackedVectorTests.cpp` | Continues Phase 34. Found `Bgr565`/`Bgra5551`/`Bgra4444` `PackedVector` types **already existed** with pack/unpack math already matching FNA's bit layouts exactly (verified line-by-line against FNA's `Bgr565.cs`/`Bgra5551.cs`/`Bgra4444.cs`), and `tests/PackedVectorGolden.md` already had a golden reference table for all three — but the golden values weren't actually pinned in tests (`Bgr565` only tested Red/Half; `Bgra4444`/`Bgra5551` had zero golden tests). Added 8 new golden-value tests closing that gap. No bug found — pure test-coverage task. 2012/2014 EasyGL ctest pass (2 pre-existing, unrelated failures unchanged). |
| `3750522` (no task #) | `SpriteBatch.hpp` | Added missing `NOXNA` tags to 4 CNA-only `SpriteBatch` members found during an API-coverage analysis against FNA (parameterless constructor, `Draw(texture,float,float)`, 2 non-optional-`Rectangle&` `Draw` overloads). Documentation/marker-only, no behavior change. |
| `1c50a30` Task 284 | `VulkanGraphicsBackend.cpp`, `examples/vulkan_texture_srgb_test.cpp` (new), `CMakeLists.txt`, `docs/surface-format-support.md` | Found and fixed 2 compounding Vulkan bugs: `Texture2D`'s Vulkan image used `VK_FORMAT_R8G8B8A8_SRGB` (should be `_UNORM`), and the swapchain format selection explicitly preferred an SRGB surface format — both apply an unwanted gamma transform. The two partly canceled for textured content but left all non-textured rendering wrong (a nominal 128 read back as 188). Fixed both to `UNORM`. New test proves it (diff 60 → 0). EasyGL/Bgfx confirmed to have no equivalent issue. |
| `4533778` Task 283 | `Texture.hpp/.cpp`, `Texture2D/3D/Cube.cpp`, `GraphicsDevice.cpp`, `TextureTests.cpp` | Ported FNA's `Texture.GetPixelStoreAlignment`/`ValidateGetDataFormat` (both `internal` in FNA; made `public` on CNA's `Texture`, a documented deviation, since 3 of 4 real call sites aren't `Texture` subclasses in CNA). Wired the validation into all 4 real `GetData` call sites. Currently a no-op everywhere (only `Color` supported). |
| `80d3805` Task 282 | `Texture.hpp/.cpp`, `TextureTests.cpp` (new) | Ported FNA's real `Texture.GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` (not a `SurfaceFormatHelper` class as the plan guessed). 22 new exhaustive per-format tests. Fixed a missing `NOXNA` tag on `Texture::ValidateFormat` found along the way. |
| `706b591` Task 281 | `SurfaceFormat.hpp`, `SurfaceFormatTests.cpp` | Opens Phase 34. Building a canonical enum table directly against FNA found CNA's `SurfaceFormat` diverged from FNA at ordinal 20+ (7 invented values with no FNA equivalent, 7 real FNA values missing entirely). Fixed to match FNA exactly. |
| `89a0b82` Task 280 | `docs/texture3d-texturecube-support.md` (new) | Closes Phase 33. Documentation-only task that found `Texture3D`/`TextureCube::GetData` is a silent no-op on Vulkan/Bgfx (tracked as Task 865) and flagged a likely (unconfirmed) mip-allocation bug on both backends (Task 864). |
| `20e4d03` Task 279 | `TextureCube.cpp`, `TextureCubeTests.cpp` | Added `CubeMapFace` range validation (confirmed via FNA source this is a CNA safety extra, not a parity fix). Found `Vulkan_FillMode_WireFrame`'s order-dependent flakiness is pre-existing (via `git stash` bisection). |
| `41f8fc8` Task 278 | `BgfxGraphicsBackend.hpp/.cpp`, new Bgfx shader pair, `bgfx_shaders.hpp` (regenerated) | Found and fixed Bgfx's missing `EnvironmentMapEffect` code path (silently fell back to plain lit-textured rendering). Required building bgfx's `shaderc` tool from source. |
| `3d09cf3` Task 277 | `AUDIT.md` only (audit, no code) | Confirmed `Texture3D` shader sampling is architecturally unreachable in CNA today. Tracked as Task 863. |
| `9a2d884` Task 276 | `EasyGLGraphicsBackend.cpp`, new mip test | Found and fixed a real bug: `EasyGLTextureCubeBackend` only allocated GPU storage for mip level 0, so writes to level >0 silently went nowhere. |
| `a003534`/`032e835`/`79095e6` Tasks 275/274/273 | new tests | Added genuine x/y/z(/rect) sub-region + `startIndex` pixel-verified `SetData`/`GetData` tests for `Texture3D`/`TextureCube` beyond what existed. No bugs found in these three. |
| `d66af1c`/`2400d2b` Tasks 272/271 | `TextureCube.hpp/.cpp`, `Texture3D.hpp/.cpp`, new test files | Audited both against FNA; found/fixed the same 3 bug classes in each (hardcoded `LevelCount`, missing `SetData`/`GetData` guards, missing `Dispose(bool)`), plus 2 `TextureCube`-specific bugs. Found `DDSFromStreamEXT` is a non-functional stub (Task 663). |

---

## 4. Current blocker / main problem

**There is no build-breaking or test-breaking blocker.** The repository builds and the test
suites pass at the rates given in §2 on all three backends.

The most significant *correctness* gap currently open is architectural, not a build/test failure:
`Texture3D`/`TextureCube` do not inherit `Texture` in CNA (they inherit `GraphicsResource`
directly), which structurally prevents `Texture3D` from ever being sampled in a shader via the
normal `GraphicsDevice.Textures[slot]` path, and is the root cause behind `EffectParameter`
needing separate storage slots per texture type. There is no failing command or failing test
tied to this — it manifests as a compile-time impossibility if game code tries
`GraphicsDevice.Textures[i] = my3DTexture` in the way real XNA/FNA code would. See `AUDIT.md`'s
Task 277 entry and `GRAPHICS_TASKS.md` Task 863 for the full analysis and fix options (both require
a real architecture decision, not a small patch).

The most significant *silent-failure* gaps (compile and run without error, but produce wrong or
no data) are: `TextureCube::DDSFromStreamEXT` (Task 663) and `Texture3D`/`TextureCube::GetData` on
Vulkan/Bgfx (Task 865). Neither has a reliably reproducing failing *test* today because no test
currently exercises the exact code path that would reveal them beyond what's already documented
(DDS stub: no test loads a real DDS cube file; Vulkan/Bgfx `GetData`: existing tests never assert
on returned pixel values).

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| Confirmed bug | `SpriteBatch` with multiple `Begin()`/`End()` calls per frame on Vulkan: only the last batch renders. |
| Confirmed bug, severe, silent failure | `TextureCube::DDSFromStreamEXT` ignores its `stream` argument and always returns a blank 1×1 texture. Tracked as Task 663. |
| Confirmed bug, severe, silent failure | `Texture3D`/`TextureCube::GetData` is a total no-op on Vulkan and Bgfx (neither overrides the empty base-class default). Tracked as Task 865. |
| Confirmed bug, MASSIVE, silent failure | **Vulkan's blend state support is almost entirely fake.** `VulkanGraphicsBackend::ApplyBlendState` ignores every specific blend-factor/function parameter, only checking "is this exactly `BlendState.Opaque`?" — any other `BlendState` (`Additive`, `AlphaBlend`, any custom state) gets one hardcoded blend equation baked into every pipeline, regardless of what was actually requested. Confirmed on real hardware FIVE times: `Vulkan_BlendState_AlphaBlend` (Task 304), `Vulkan_BlendState_Additive` (Task 306), `Vulkan_BlendState_SeparateFunctions` (Task 307), `Vulkan_BlendState_SeparateFactors` (Task 308 — one of its two checks coincidentally passes, the same asymmetric pattern as Task 305, since the hardcoded equation collapses to "source only" at `alpha=255`, matching *one* possible expectation but not the other), and `Vulkan_BlendState_BlendFactor` (Task 309 — the hardcoded equation never selects `VK_BLEND_FACTOR_CONSTANT_COLOR`, so `Blend::BlendFactor` is moot regardless of Task 309's separate `BlendFactor`-propagation fix). Task 305's `NonPremultiplied` test is the only FULLY coincidental pass. All 5 real failures kept registered as documented known failures (like `Vulkan_DepthBias`) rather than hidden. Bgfx has a narrower, related gap (colour blend factors correctly mapped; alpha factors and blend functions ignored — the `BlendFactor`-propagation piece of this was fixed by Task 309). EasyGL is fully, correctly implemented (confirmed by contrast, and by passing all of Tasks 304–309 exactly as predicted). Tracked as Task 868, not fixed — large, multi-pipeline-site change needing its own dedicated task. |
| Confirmed, architectural | `Texture3D`/`TextureCube` sampling cannot be wired into any shader today — they don't inherit `Texture`, so can't enter `GraphicsDevice.Textures[slot]`; custom `ShaderEffect` has no texture-binding API at all. Tracked as Task 863. |
| Confirmed bug, pre-existing | `EasyGL_MRT_TwoAttachments`: `SetRenderTargets` with 2 attachments renders correctly to attachment 0 but attachment 1 stays black. Not caused by recent work; needs dedicated investigation. |
| Confirmed bug, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` (sibling `easy-gl` repo) aborts on an internal assert; unrelated to any file in this repo. |
| Confirmed bug, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. Not investigated further. |
| Confirmed bug, pre-existing, order-dependent | `Vulkan_FillMode_WireFrame` fails only when run as part of the full suite (parallel or serial), passes in isolation. Confirmed via `git stash` bisection to predate recent sessions. `Vulkan_RenderTargetUsage` shows similar parallel-only flakiness. Recurred again in Task 309's session, including transiently in isolation right after a period of concurrent EasyGL+Vulkan `ctest` load (see §2's caution note) — reconfirmed unrelated to Task 309's changes (its `GraphicsDevice.cpp`/`BgfxGraphicsBackend.cpp` edits don't touch fill-mode/rasterization state; `SetBlendFactor` is a trivial 4-float store with no GPU-side effect). |
| Likely bug, not yet confirmed | Vulkan and Bgfx probably have the same mip-level-allocation bug fixed for EasyGL's `TextureCube` (Task 276), for both `Texture3D` and `TextureCube`, on both backends. Tracked as Task 864, not reproduced with a test. |
| Fixed (Task 293), all 3 backends | `GraphicsDevice.SamplerStates`/`VertexSamplerStates` were silently ignored by 3D stock-effect draws on every backend, for 3 different backend-specific reasons (missing `applySamplerStatesToBackend()` on EasyGL/shared code; hardcoded `defaultSampler_` + view-only descriptor cache key on Vulkan; `samplerFlags_[0]` reused for texture slot 1 on Bgfx). All 3 fixed and reverified: EasyGL/Vulkan pixel-test-confirmed (`*_SamplerState_DualTextureEffect`), Bgfx confirmed via full-suite no-regression (no pixel-readback API). |
| Fixed (Task 309) | `GraphicsDevice::setBlendStateProperty` never propagated a `BlendState`'s own `BlendFactor` to the device — fixed by calling `setBlendFactorProperty` internally. Also fixed a separate Bgfx-specific gap: `blendFactorPacked_` was computed but never passed to any of Bgfx's 5 `bgfx::setState()` call sites. Confirmed via `*_BlendState_BlendFactor` (EasyGL passes; Vulkan fails, but only as a further Task 868 confirmation, not a new bug). |
| By design, documented (Task 309) | `GraphicsDevice.MultiSampleMask` is a complete no-op on all 3 backends — zero references to any sample-mask concept anywhere in backend code. Not fixed: no consumer exists yet, and supporting it would need new backend-interface surface on all 3 backends for a niche, rarely-used DX9-era feature. |
| Confirmed, architectural, documented (Task 310) | `GraphicsDevice` stores `BlendState`/`DepthStencilState`/`RasterizerState` **by value**, unlike FNA's reference-type aliasing (`nextBlend = value;` stores the same object) — mutating a custom state object *after* assigning it to `GraphicsDevice` is silently a no-op in CNA but would take effect in real XNA/FNA. Deliberate, consistent, project-wide pattern (not `BlendState`-specific); no game/example code in this repo relies on the FNA behavior. Tracked as Task 869, not fixed — would need every affected state property to become a reference/pointer type, a real architecture decision. Confirmed by contrast: FNA has NO freeze/immutability enforcement on these classes either, so there's no "throws after first use" behavior missing from CNA — only this value-vs-reference divergence. |
| Fixed (Task 311) | `DepthStencilState`'s 3 static presets (`Default`/`DepthRead`/`None`) didn't set `Name`, matching the same gap already fixed in `SamplerState` (Task 291)/`BlendState` (Task 301). Fixed by threading a `name` param through the private preset constructor. Closes `DepthStencilState`'s portion of Task 866; `RasterizerState`'s portion remains open (Phase 38). |
| Fixed (Task 312), same shape as Task 302 | `GraphicsDevice`'s `depthStencilState_`/`rasterizerState_` were plain default-constructed, never copied from `DepthStencilState::Default`/`RasterizerState::CullCounterClockwise` (FNA's actual constructor behavior). Values coincided for `depthStencilState_` (invisible until Task 311 gave `Default` a `Name`). Fixed via the constructor's member-init list. |
| Confirmed bug, MASSIVE, silent failure (Task 313 finding) | **Vulkan's `DepthStencilState` support is almost entirely fake — the same shape/severity as Task 868's `BlendState` finding.** `VulkanGraphicsBackend::ApplyDepthStencilState` only stores `depthEnable`/`depthWriteEnable`; `DepthBufferFunction` is ignored (every one of the 7 Vulkan 3D pipeline-creation functions hardcodes its own `depthCompareOp` — a mix of `VK_COMPARE_OP_LESS`/`LESS_OR_EQUAL`, unrelated to what's actually requested), and stencil testing is **completely non-functional** (`stencilTestEnable`/front/back never set anywhere in the Vulkan backend, confirmed via grep). EasyGL and Bgfx's *state-application code* is both fully, correctly implemented by contrast. Tracked as Task 870, not fixed — large, multi-pipeline-site change needing its own dedicated task, exactly like Task 868. **Update (Task 314):** directly and fully reconfirmed — of 5 `CompareFunction` checks, only `Less` passes on Vulkan, exactly matching the hardcoded `VK_COMPARE_OP_LESS` pipeline. **Update (Task 315):** directly reconfirmed for stencil specifically — `StencilEnable=true` never gates fragments on Vulkan. Also found `VulkanGraphicsBackend::FindDepthFormat()` prefers a stencil-less format (`VK_FORMAT_D32_SFLOAT`) before stencil-capable ones — a compounding, additional root cause. **Update (Task 316):** confirmed a third time via `StencilMask`/`StencilWriteMask` — 2 of 4 checks pass on Vulkan only by coincidence (nothing ever gates), the other 2 correctly fail. **Update (Task 317):** confirmed a fourth time via front-face `StencilFail`/`StencilDepthBufferFail`/`StencilPass` — 3 of 4 checks pass on Vulkan only by coincidence, a dedicated contrast column (deliberately-wrong reference value) correctly fails. **Update (Task 318):** confirmed a fifth time via `TwoSidedStencilMode`/back-face (CCW) ops — the "should pass" column passes coincidentally, the contrast column correctly fails. This closes out Phase 37's per-property verification tasks (311-319); all 5 confirmations remain unfixed here, tracked for a dedicated future task. |
| Fixed (Task 319), same shape as Task 309 | `GraphicsDevice::setDepthStencilStateProperty` never propagated an assigned `DepthStencilState`'s own `ReferenceStencil` into `GraphicsDevice`'s own `referenceStencil_` field. Fixed by calling `setReferenceStencilProperty` internally. Confirmed via a new unit test. |
| Confirmed bug, universal, not fixed (Task 319 finding, tracked as Task 872) | `GraphicsDevice::setReferenceStencilProperty` has **zero backend connection on all 3 backends** — `IGraphicsBackend` has no `SetReferenceStencil` method at all, unlike `BlendFactor` (Task 309) where the backend method already existed. Confirmed via a pixel test failing identically on both EasyGL and Vulkan: an independent `setReferenceStencilProperty(0x99)` call (without reassigning the whole `DepthStencilState`) has no effect on a subsequent stencil compare, which still uses the state's own stale baked-in reference value. Unlike Task 870, this is NOT Vulkan-specific. Not fixed — needs a new `SetReferenceStencil` backend-interface method plus per-backend plumbing (EasyGL needs to cache current stencil func/mask to reissue it with a new reference; Vulkan needs `vkCmdSetStencilReference` dynamic state, moot until Task 870 is fixed; Bgfx needs to patch just the reference bits of its packed stencil state). |
| Fixed (Task 315), severe | No EasyGL window in this project ever requested `SDL_GL_STENCIL_SIZE`, so no window ever had a physical stencil buffer — the GL stencil test trivially always-passed regardless of `DepthStencilState.StencilEnable`, even though the state-application code (`glStencilFunc`/`glStencilOp`/`glEnable(GL_STENCIL_TEST)`) was already correct (this is why Task 870's write-up called EasyGL "fully correct" — true of the code, not of the runtime behavior until this fix). Fixed with a one-line `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)` addition to `EasyGLGraphicsBackend`'s context creation. Confirmed via `EasyGL_DepthStencilState_StencilEnable`; full ctest reconfirmed zero regressions. Whether Bgfx's window has a real stencil buffer is unverified — worth checking before assuming its stencil support is any better than EasyGL's was. |
| Confirmed bug, universal, not fixed (Task 315 finding, tracked as Task 871) | `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` and the `stencil` clear value entirely on every backend — confirmed via reading the full function body (`(void)stencil;`, no `hasClearFlag(options, ClearOptions::Stencil)` check anywhere). FNA's real `Clear(Color)` clears Target+DepthBuffer+Stencil together. Confirmed on EasyGL specifically: none of `Clear`/`ClearColorAndDepth`/`ClearDepth` ever pass a Stencil GL clear flag. On Vulkan the effect is compounded by a separate quirk (`stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE` on every render pass, so stencil content is undefined regardless of `Clear()`). Found while needing a reliable stencil baseline for Task 315's pixel test — worked around by explicitly "stamping" a known value via a real draw instead of relying on `Clear()`. Not fixed — needs its own dedicated task touching all 3 backends' `Clear`-family methods. |
| Incomplete | `SpriteBatch`'s `SamplerState` is a no-op on Vulkan and Bgfx (EasyGL only) — a separate code path from Task 293's fix; `SpriteBatch` doesn't go through `GraphicsDevice.DrawUserPrimitives`. |
| Incomplete | EasyGL/Bgfx stride-keyed vertex layout only supports strides 16/20/24/32/52. |
| Incomplete | Vulkan: `Tangent`/`Binormal` `VertexElementUsage` values have no mapping. |
| Incomplete, tracked | `RasterizerState` static presets don't set `Name` (FNA does, e.g. `"RasterizerState.CullCounterClockwise"`) — same gap `SamplerState`/`BlendState`/`DepthStencilState` had before Tasks 291/301/311 fixed it in each. Tracked as Task 866, now only `RasterizerState`'s portion remains — its own later audit task in Phase 38's list. |
| Incomplete | `SurfaceFormat` support is Color-only everywhere for actual GPU texture formats (only `Color` passes `Texture::ValidateFormat`); Phase 34 (Tasks 281–290) is complete and covers CPU-side size/packing math + throw-clearly behavior for all 27 formats, not real non-Color GPU mapping. |
| Incomplete | SDL_Renderer: no 3D support at all (`CreateVertexBuffer` always throws), by design. |
| Incomplete | Bgfx: no GPU pixel-readback API, so its tests are smoke-only, by design. |
| By design | `Texture2D::GetData` (level 0) permanently throws after `SetContextRecoveryEnabled(false)` once the CPU shadow buffer is freed — no GPU readback fallback exists. |
| Risky assumption | `GraphicsDevice`'s user-primitive scratch buffers (Task 260) never shrink — acceptable for typical use, but memory stays at the high-water mark for the device's lifetime. |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|---|---|---|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, `IVertexBuffer`, etc. |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via EasyGL wrapper |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | `VulkanVertexFormatHelper.hpp` for per-format mapping |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | `BgfxVertexFormatHelper.hpp`; no readback API |
| CNA utilities | `include/CNA/`, `src/CNA/` | `NOXNA` helpers, logging, math |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types, primitive aliases |

### Critical invariants (do not break these)

- **`NOXNA` macro** tags every non-XNA extension in public headers — required for any new CNA-only
  public method/constructor/type. Requires `#include "CNA/CNAHelper.hpp"`.
- **C# properties** → `getXProperty()` / `setXProperty()` — never public fields on the XNA surface.
- **`static readonly`** (C#) → `static const` member in `.hpp` + definition in `.cpp`.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) must
  be used on XNA API surfaces — never raw `uint8_t`/`float`/`std::string` directly.
- **Backend selection is compile-time** — no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout** — only strides 16/20/24/32/52 work correctly for 3D.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */`.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp`/`.cpp`.
- **`Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture`** — a known deviation from
  FNA (see §5). Do not assume code that works for `Texture2D` "just works" for these two.
- **`SurfaceFormat` ordinal values are load-bearing** — every backend does
  `static_cast<int>(format)`. Any enum edit must preserve FNA's exact ordinals (verified 0–26).
- **`Texture::ValidateFormat` blocks every format except `Color`** at `Texture2D`/`Texture3D`/
  `TextureCube` construction time — this is why `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT`/
  `ValidateGetDataFormat` are currently no-ops in practice everywhere they're wired in.
- **`GraphicsDevice::userVertexScratch_`/`userIndexScratch_`** are shared, growable, non-shrinking
  scratch buffers used by all `DrawUserPrimitives`/`DrawUserIndexedPrimitives` overloads. Never
  resize down; never reenter mid-write.

### FNA reference

Authoritative behavioral reference: `/rv/data/library/github.com/FNA-XNA/FNA/src`. When CNA
intentionally diverges from FNA, document it in the commit/PR description and in `AUDIT.md` —
not as a source comment explaining the deviation's rationale (a `//` comment explaining a genuine
non-obvious behavioral constraint is fine; a comment whose only purpose is "this differs from FNA
because X" belongs in the audit trail instead).

---

## 7. Useful commands

```bash
# Configure (EasyGL — primary)
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Configure (Vulkan)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON

# Configure (Bgfx)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON

# Build CNA library
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build and run all unit tests (EasyGL)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)
./cmake-build-debug/CnaTests

# Run a specific unit test suite
./cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Build Vulkan (single-threaded is more reliable for link stability)
cmake --build cmake-build-vulkan --target CNA -j1

# Full ctest run (unit + integration), any backend build dir
cd cmake-build-debug && ctest -j$(nproc)
cd cmake-build-debug && ctest --output-on-failure   # for a failure's full output
cd cmake-build-debug && ctest -R <TestName>          # run one test in isolation (useful for flaky tests)

# Run a specific EasyGL integration/example test directly (needs an X server on :0)
DISPLAY=:0 ./cmake-build-debug/cna_test_easygl_surface_format_throws
```

There is no known reproducible failing build command right now (see §4).

---

## 8. Next smallest tasks

In priority order:

1. **`GRAPHICS_TASKS.md` Task 320 — document depth/stencil support matrix**
   - Goal: closes Phase 37. Synthesize Tasks 311–319's findings into one reference doc, mirroring
     Task 300's `docs/sampler-state-support.md` precedent: API conformance (all 16 properties, 3
     presets), a per-backend support matrix (EasyGL: fully correct after Task 315's stencil-buffer
     fix; Vulkan: `DepthBufferFunction` + all stencil testing non-functional, Task 870; Bgfx: state
     code confirmed correct by reading, physical stencil-buffer existence unverified), and links to
     the 3 new tracked bugs (870, 871, 872) rather than duplicating their write-ups.
   - Files: new `docs/depthstencilstate-support.md`.
   - Verification: doc-only task, no new tests — cross-check that every linked task/bug number
     still matches `GRAPHICS_TASKS.md`'s current wording before publishing.

2. **`GRAPHICS_TASKS.md` Task 663 — implement `TextureCube::DDSFromStreamEXT` for real**
   - Goal: replace the current stub with a real DDS cube-map parser (header parsing incl. `isCube`
     flag, reuse `Texture2D.cpp`'s DXT decode helpers, 6×`levelCount` `SetData` calls).
   - Files: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp`, `TextureCubeTests.cpp`.
   - Verification: build a real/hand-built DDS cube-map test fixture **first**, then implement
     against it — do not mark done on "compiles and doesn't throw" alone (see §9).

3. **`GRAPHICS_TASKS.md` Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
   - Goal: `vkCmdCopyImageToBuffer` + host-visible staging buffer, mirroring the existing upload
     path's staging-buffer pattern in reverse.
   - Files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
     (`VulkanTexture3DBackend`/`VulkanTextureCubeBackend::GetData`).
   - Verification: new Vulkan pixel-readback test analogous to the EasyGL ones in
     `easygl_texture3d_partial_box_readback_test.cpp`.

4. **`GRAPHICS_TASKS.md` Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug**
   - Goal: confirm (via a failing test first, matching the Task 276 methodology) that `Texture3D`/
     `TextureCube` mip levels >0 silently fail on Vulkan and Bgfx, then fix by pre-allocating every
     mip level at image/texture creation time.
   - Files: `VulkanGraphicsBackend.cpp` (`VkImageCreateInfo::mipLevels`),
     `BgfxGraphicsBackend.cpp` (`hasMips` parameter to `bgfx::createTexture3D`/`createTextureCube`).
   - Verification: new mip round-trip test per backend, mirroring
     `examples/easygl_texturecube_mip_test.cpp`.

---

## 9. Do not do yet

- **No architecture change to make `Texture3D`/`TextureCube` inherit `Texture`** (Task 863) without
  a deliberate, scoped design pass — it touches `EffectParameter`, `TextureCollection`, and every
  backend's texture-bind code. Not a small patch.
- **No rushed `TextureCube::DDSFromStreamEXT` implementation** without a real DDS cube-map test
  fixture built first — a "looks plausible" parser that isn't verified against real data just
  trades one silent-failure stub for a differently-silent one.
- **No SpriteBatch Vulkan multi-batch fix** until the root cause is isolated — a wrong fix could
  silently break single-batch rendering.
- **No opportunistic fixes** for `EasyGL_MRT_TwoAttachments`, `Vulkan_DepthBias`, or
  `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage` flakiness — each needs its own dedicated
  root-cause investigation, not a guess bundled into an unrelated task.
- **No investigation of `easy-gl-resource-smoke-tests`** as part of a CNA task — it lives in the
  sibling `easy-gl` repo.
- **No refactor of the stride-keyed vertex layout system** — load-bearing for all 3D tests across
  all backends; needs its own dedicated phase with full regression testing.
- **No further changes to the `GraphicsDevice` user-primitive scratch buffers** without re-running
  the full `DrawUserPrimitives`/`DrawUserIndexedPrimitives` pixel-readback suite.
- **No API renames or namespace moves** — XNA API names and shapes are frozen by the FNA reference.
- **No mass Doxygen or NOXNA cleanup passes** — fix tags only on files you're already touching for
  a real reason.
- **No broad `SurfaceFormat`/`GetData`/`SetData` rewrite** — Phase 34's scope is one task at a time
  (Tasks 285–290); do not bundle multiple format implementations into one change.

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task in §8.
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md after finishing.

Current status: Phases 1-37 are fully complete. Phase 37 (DepthStencilState conformance,
GRAPHICS_TASKS.md Tasks 311-320) closed except for Task 320 (a doc-only task) - Tasks 311-319 are
all done, Task 320 is next. EasyGL is fully green: 2079/2082 (2 pre-existing failures + Task 319's
new, expected EasyGL_GraphicsDevice_ReferenceStencil failure). Vulkan: 2007/2020 - only
Vulkan_DepthBias (pre-existing, usual single sub-case) and 11 confirmed-real, documented known
failures (Vulkan_BlendState_AlphaBlend/Additive/SeparateFunctions/SeparateFactors/BlendFactor -
Task 868 - plus Vulkan_DepthStencilState_CompareFunction/StencilEnable/StencilMask/StencilOps/
StencilTwoSided - Task 870 - plus Vulkan_GraphicsDevice_ReferenceStencil - Task 872). Bgfx:
1985/1985 (100%) as of Task 309's fix, not rebuilt for Tasks 311-319 (none touch Bgfx). Caution:
don't run EasyGL's and Vulkan's full ctest suites concurrently - an earlier session saw transient
false failures from GPU/driver contention that vanished on a sequential rerun (see NEXT.md §2).

Phase 35's headline result (full writeup: docs/sampler-state-support.md): Task 293 found and fixed
a severe, project-wide bug across all 3 backends - GraphicsDevice.SamplerStates was being silently
ignored by essentially all 3D stock-effect texture draws. Task 298/299 found a SECOND severe bug,
tracked as Task 867, not yet fixed: Texture2D::SetData(level>0,...) is a total silent no-op on
Vulkan/Bgfx, plus related Vulkan mip-allocation and EasyGL mipmap-incompleteness findings.

Phase 36 (BlendState conformance, Tasks 301-310) is fully closed. Headline results: Task 304 found
something MASSIVE - Vulkan's blend state support is almost entirely fake
(VulkanGraphicsBackend::ApplyBlendState ignores every specific blend-factor/function parameter,
hardcoding ONE equation for anything except exactly Opaque) - tracked as Task 868, confirmed FIVE
times across Tasks 304/306/307/308/309, NOT fixed (large multi-pipeline-site change needing its
own dedicated task). Task 309 found and fixed a real, universal bug:
GraphicsDevice::setBlendStateProperty never propagated a BlendState's own BlendFactor to the
device - fixed, plus a Bgfx-specific blendFactorPacked_ propagation gap also fixed.
GraphicsDevice.MultiSampleMask confirmed a complete, universal no-op on all 3 backends - documented,
not fixed. Task 310 (closing the phase) confirmed FNA has NO state-object freeze/immutability
enforcement at all (no bug), but found a real architectural divergence: CNA's GraphicsDevice stores
BlendState/DepthStencilState/RasterizerState BY VALUE, unlike FNA's reference-type aliasing -
tracked as Task 869, not fixed (deliberate, project-wide pattern, needs an architecture decision).

Phase 37 (DepthStencilState conformance, Tasks 311-319) is now closed - all 9 verification tasks
done. Task 311 audited DepthStencilState against FNA (all correct; fixed the known Task 866 Name
gap). Task 312 found and fixed a THIRD Task-302-shaped bug: GraphicsDevice's depthStencilState_/
rasterizerState_ were never actually copied from the FNA-specified presets - fixed via the
constructor's member-init list.

Task 313 found a SECOND MASSIVE bug, tracked as Task 870, NOT fixed: Vulkan's DepthStencilState
support is almost entirely fake - VulkanGraphicsBackend::ApplyDepthStencilState ignores
DepthBufferFunction (hardcoded per-pipeline to VK_COMPARE_OP_LESS or LESS_OR_EQUAL) and the ENTIRE
stencil-test parameter set (stencil testing is completely non-functional on Vulkan -
stencilTestEnable/front/back never set anywhere) - same shape/severity as Task 868. Reconfirmed
FIVE more times across Tasks 314-318 (compare functions: only Less passes; stencil enable; stencil
read/write masks; front-face stencil ops; two-sided/back-face stencil ops) - always via a genuine
contrast check that a working implementation must REJECT, a lesson learned mid-phase: a test where
every check expects the SAME outcome can't distinguish "it works" from "the stencil test is
bypassed entirely" (Task 870), since both look identical - Task 317's first draft fell into this
trap and had to be redesigned; Task 318 got it right from the start.

Task 315 also found and FIXED a severe, previously-undiscovered bug: no EasyGL window in this
project ever requested SDL_GL_STENCIL_SIZE, so no window ever had a physical stencil buffer at all
- the GL stencil test trivially always-passed regardless of StencilEnable, even though the
state-application code was already correct. Fixed with a one-line
SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) addition; full ctest reconfirmed zero regressions.
IMPORTANT: any stencil pixel test needs PresentationParameters.DepthStencilFormat =
DepthFormat::Depth24Stencil8 (GraphicsDeviceManager) since the default Depth24 has no stencil
aspect at all - see any Task 315+ test file header. Also found (Task 315) a compounding,
Vulkan-only cause: VulkanGraphicsBackend::FindDepthFormat() checks VK_FORMAT_D32_SFLOAT
(stencil-less) BEFORE the stencil-capable formats.

New Task 871, NOT fixed: GraphicsDevice::Clear ignores ClearOptions::Stencil (and the stencil clear
value) on every backend - found because stencil pixel tests need a reliable baseline and must stamp
it via a real draw instead of using Clear().

Task 319 (closing the phase) found and fixed a small Task-309-shaped bug (setDepthStencilStateProperty
never propagated an assigned state's own ReferenceStencil into GraphicsDevice's own field) - fixed.
Also found a SECOND, universal, NOT-fixed bug tracked as new Task 872: GraphicsDevice.
ReferenceStencil's independent-override behavior (settable without reassigning the whole
DepthStencilState, per real FNA semantics) has ZERO backend connection on all 3 backends - unlike
Task 870, this fails identically on BOTH EasyGL and Vulkan, since IGraphicsBackend has no
SetReferenceStencil method at all.

Next task: GRAPHICS_TASKS.md Task 320 - document depth/stencil support matrix. Closes Phase 37.
Synthesize Tasks 311-319's findings into one reference doc, mirroring Task 300's
docs/sampler-state-support.md precedent: API conformance, a per-backend support matrix (EasyGL:
fully correct after Task 315's stencil-buffer fix; Vulkan: DepthBufferFunction + all stencil
testing non-functional, Task 870; Bgfx: state-application code confirmed correct by reading,
physical stencil-buffer existence unverified - worth flagging as an open question), and links to
the 3 new tracked bugs (870, 871, 872) rather than duplicating their write-ups. Doc-only task, no
new tests - cross-check every linked task/bug number still matches GRAPHICS_TASKS.md's current
wording before publishing.
Update GRAPHICS_TASKS.md and NEXT.md after finishing.
```
