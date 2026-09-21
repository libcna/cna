# plan_vulkan_parity.md — Vulkan renderer parity on real Radeon/RADV hardware

A hardware-validation workstream for the **existing classic CNA/XNA Graphics contract** on the
Vulkan renderer. Not a modern-graphics/CNAEXT project, not a performance project, and not a second
copy of `plans/plan_vulkan.md` — that campaign's 243 rows reached their verdict on **2026-09-11**
against **llvmpipe under Xvfb**. This one re-measures the parts that verdict could not see, on the
machine's real **AMD Radeon 780M / RADV**, across the native **X11** and **Wayland** backends, and
closes what the newer `plans/plan_graphics_shared_cleanup.md` contracts left open for this renderer.

Task IDs: `VKPAR-0001`, `VKPAR-0002`, … . Rows carry their evidence; this file is the ledger.

**Evidence classes are kept apart and never blended.** A line that does not name one is not
evidence:

| Class | Meaning |
|---|---|
| **RADV-HW** | AMD Radeon 780M, `DRIVER_ID_MESA_RADV`, real GPU. The evidence this workstream exists to produce. |
| **LVP** | `lavapipe`, `DRIVER_ID_MESA_LLVMPIPE`, software. Cross-check only — never the primary evidence for a hardware claim. |
| **XNA-MEASURED** | Microsoft XNA 4.0 through `tools/xna-oracle`, references checked in. Outranks every renderer. |

---

## Status

| ID | Task | Status |
|---|---|---|
| VKPAR-0001 | Baseline: repository, hardware, platform paths | ✅ |
| VKPAR-0002 | Renderer architecture audit | ✅ |
| VKPAR-0003 | Three configurations of this tree did not build | ✅ |
| VKPAR-0004 | GSC-F2 — Vulkan classic null-texture semantics (white → XNA's opaque black) | ✅ |
| VKPAR-0005 | Vulkan facedness: the two-sided stencil "driver quirk" claim, measured on RADV | ✅ |
| VKPAR-0006 | 103 Vulkan example tests abort before asserting anything (Reach profile) | ✅ |
| VKPAR-0007 | X11 + Vulkan surface path on RADV | ✅ |
| VKPAR-0008 | Wayland + Vulkan surface path on RADV | ✅ |
| VKPAR-0009 | SDL-free native configurations (X11 and Wayland) | ✅ |
| VKPAR-0010 | Baselines, measured and classified | ✅ |
| VKPAR-0011 | GSC-F1 — EasyGL per-pixel SkinnedEffect ambient-only white | ◐ reproduced and narrowed, not fixed |
| VKPAR-0012 | Validation layers and synchronization validation | ✅ |
| VKPAR-0013 | The remaining Vulkan failures | ⬜ classified, not fixed |
| VKPAR-0014 | Channel expansion: two contradictory contracts, settled by measuring XNA | ✅ |
| VKPAR-0015 | Where these tests ran: the user's live desktop, and how to stop that | ✅ |
| VKPAR-0016 | `cmake-build-vulkan` grew to 87 GB: 341 EasyGL test binaries that run Vulkan | ✅ |
| VKPAR-0017 | The CNA runtime as one shared library (`libcna.so`) instead of 798 static copies | ✅ |
| VKPAR-0018 | Classic closeout, step 1: 23 more tests that died on the Reach profile | ✅ |
| VKPAR-0019 | `SpriteBatch_BlendState`: the white constant factor is XNA's answer, not a renderer defect | ✅ |
| VKPAR-0020 | Format capability: the eleven HiDef texture formats, and the render-target fallback the format tests had not caught up with | ✅ |
| VKPAR-0021 | Cube transfers: every stored format in its own VkFormat, exact byte and block readback | ✅ |
| VKPAR-0022 | Ten tests that asserted the public layer as it was before XNA's rules were recovered | ✅ |
| VKPAR-0024 | `Depth24` on RADV was a stencil format, reported as `Depth24Stencil8` | ✅ |
| VKPAR-0025 | Ten transfer and render-target tests older than the XNA rules they break | ✅ |

---

## VKPAR-0001 — Baseline

### Repository

| Fact | Value |
|---|---|
| Baseline commit | `391279c502026c3b8276881b6b35aef73670fbba` (= `origin/next`, fetched 2026-09-21) |
| Branch | `vulkan-parity`, branched from `origin/next` |
| Identity | `Robert Vokac <robertvokac@robertvokac.com>` (repository-local `user.name`/`user.email`) |
| Working tree at branch time | clean except untracked `startup-metrics.log` (pre-existing, not this workstream's) |
| Build directory | `cmake-build-vulkan/` (in-repo, shared, incremental — `CLAUDE.md` build-location rule) |

The `graphics-shared-cleanup` work **is** present: all eight task commits and the integration merge
`c0225e9d5` are ancestors of the baseline (`git merge-base --is-ancestor`, each verified
individually). Nothing was recreated or cherry-picked.

| Precondition fix | Commit | Ancestor of baseline |
|---|---|---|
| Two-sided stencil correction | `66186d6dd` | yes |
| SkinnedEffect non-uniform-scale normals | `9113ab4e7` | yes |
| Measured XNA missing-texture semantics + glTF white | `ea0656ca1` | yes |
| PresentationParameters depth-format contract | `c731c1a96` | yes |
| D3D validation enforcement | `12b6e09dd` | yes |
| `CaseInsensitivePathTest` cleanup | `9133cb279` | yes |
| AudioEngine per-process temp isolation | `e5b911a85` | yes |

### Hardware — measured, not assumed

| Fact | Value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), `DEBIAN_VERSION_FULL=13.6` |
| Kernel | `6.12.107+deb13-amd64` (`thinkpadt14`) |
| CPU / RAM | 16 threads, 30 GiB, **no swap** |
| GPU (PCI) | `c3:00.0 AMD/ATI Phoenix1 [1002:15bf] (rev dd)` |
| DRM | `/dev/dri/card0`, `/dev/dri/renderD128` |
| Vulkan instance | **1.4.309** |
| Physical device | **AMD Radeon 780M (RADV PHOENIX)** |
| driverID | `DRIVER_ID_MESA_RADV` (`radv`) |
| Device API version | 1.4.305 |
| driverVersion / Mesa | 25.0.7 |
| vendorID / deviceID | `0x1002` / `0x15bf` |
| Device type | `PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU` |
| Second ICD | `lvp_icd.json` — **lavapipe present**, so the LVP cross-check class is available |

Validation and diagnostics available on this host:

| Capability | State |
|---|---|
| `VK_LAYER_KHRONOS_validation` | present, **1.4.309** |
| `VK_EXT_debug_utils` | present (instance extension) |
| `VK_KHR_xlib_surface` / `VK_KHR_xcb_surface` | present |
| `VK_KHR_wayland_surface` | present |
| `VK_EXT_headless_surface` | present |
| Other layers | `VK_LAYER_MESA_device_select`, `VK_LAYER_MESA_overlay`, `VK_LAYER_INTEL_nullhw`, Steam's two |

### Platform paths — and one correction the brief's assumption needed

The session is GNOME/Wayland (`XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=GNOME`,
`gnome-shell` live, socket `wayland-0`), with `Xwayland :0` rootless beside it. But the **agent
shell does not inherit that session**: it starts with `WAYLAND_DISPLAY` empty and `DISPLAY=:99`.

`:99` is `Xvfb :99 -screen 0 1920x1080x24`, and it is **unusable for Vulkan presentation**:

```
vulkan: No DRI3 support detected - required for presentation
```

`vulkaninfo` on `:99` emits that for every ICD and enumerates RADV without a usable surface. On
`:0` there is no such message, `xdpyinfo` lists `DRI3`, and both RADV and lavapipe enumerate.

| Display | Server | DRI3 | Usable for Vulkan presentation |
|---|---|---|---|
| `:99` | `Xvfb` | no | **no** |
| `:0` | `Xwayland` (rootless, under Mutter) | yes | yes |
| `wayland-0` | Mutter/GNOME | n/a | yes (native `VK_KHR_wayland_surface`) |

`CNA_TEST_DISPLAY` in `cmake-build-vulkan/` is already `:0`, so the registered Vulkan ctests target
the DRI3-capable server rather than Xvfb. **Any Vulkan run on `:99` is an environment artefact, not
a renderer result**, and is classified as such wherever it appears below.

`:0` reports `1536x960` where the panel is 1920x1200 — consistent with the **125% fractional
scaling** Phase 56 asks about; confirmed against the compositor before that row is closed.

---

## VKPAR-0002 — Renderer architecture audit

The renderer is **not** a thin or early implementation. Measured on the baseline:

| Module | Lines | Note |
|---|---|---|
| `modules/renderers/vulkan/src` | 38 125 | of which `VulkanRenderer.cpp` alone is **23 183** |
| `modules/renderers/vulkan/include` | 6 955 | |
| `modules/renderers/vulkan/examples` | 42 009 | **155** example/test translation units |
| `modules/renderers/vulkan/tests` | 512 | `VulkanCompiledEffectTests` |
| **Total** | **87 601** | second-largest renderer family after `common` (96 509) |

For scale: `directx11` is 9 999 lines, `directx12` 16 747, `software` 36 249, `easygl` 72 104.

Renderer classes present (from `VulkanRenderer.hpp`): `VulkanTextureRenderer`,
`VulkanTexture3DRenderer`, `VulkanTextureCubeRenderer`, `VulkanTexture2DArrayRenderer`,
`VulkanStorageTexture2DRenderer`, `VulkanRenderTargetRenderer`, `VulkanRenderTargetCubeRenderer`,
`VulkanMRTProxy`, `VulkanEffectRenderer`, `VulkanSpriteBatchRenderer`, `VulkanVertexBufferRenderer`,
`VulkanIndexBufferRenderer`, `VulkanStorageBufferRenderer`, `VulkanComputeShaderRenderer`,
`VulkanOcclusionQueryRenderer`, `VulkanGpuTimerRenderer`, plus a `PipelineKey`/`PipelineKeyHash`
pipeline cache and the `IVulkanSamplable`/`Cube`/`Volume`/`Array` sampling contracts.

The prior campaign's own verdict and its scope are in `plans/plan_vulkan.md` §28. **This plan does
not restate it and does not inherit it**: its evidence is llvmpipe-under-Xvfb, and the rows below
say which of its conclusions survive contact with RADV.

---

## VKPAR-0003 — The Vulkan configuration does not build

Found by trying to produce the baseline, not by reading. On the baseline commit, with the build
directory's existing settings, `cmake --build cmake-build-vulkan` **fails**:

```
CNA_GRAPHICS_RENDERER   = VULKAN
CNA_GRAPHICS_RENDERERS  = VULKAN;OPENGL33
CNA_PLATFORM            = SDL3     CNA_CNAEXT = OFF     CMAKE_BUILD_TYPE = Debug
→ 19 errors in vulkan_shader_effect_test.cpp, then 2 in shader_effect_reflection_contract_test.cpp
```

Two independent defects, neither of them Vulkan-renderer defects — both are **test-registration**
defects, and both are invisible in the configurations these suites are usually built in.

### (a) Two Vulkan example TUs need `CNA_CNAEXT` and are registered unconditionally

`vulkan_shader_effect_test.cpp` and `vulkan_shader_dialect_contract_test.cpp` include
`CNA/Graphics/ShaderPackageEXT.hpp` and build a `ShaderPackageEXT` (`MOD-2216`/`MOD-2217`).
`ShaderCodeEXT` and `ShaderPackageEXT` live behind `ShaderEffect.hpp`'s own `#ifdef CNA_CNAEXT`, so
with the project default `CNA_CNAEXT=OFF` neither TU compiles:

```
vulkan_shader_effect_test.cpp:42:12: error: 'CNA::Graphics' has not been declared
vulkan_shader_effect_test.cpp:44:31: error: 'CNA::Examples' has not been declared
```

The same file already guards its `MOD-2243` block with `if(CNA_CNAEXT)`; these two registrations
were simply left outside it. Fixed by moving both inside that guard.

**Why no test caught it:** `CNAEXT_GuardDiscipline` checks that every file in `graphics-ext` is
wrapped in `#ifdef CNA_CNAEXT`. It says nothing about a *consumer* outside that module, and nothing
about CMake registration. The Vulkan example suite had therefore only ever been built with
`CNA_CNAEXT=ON` — which is not the configuration the classic XNA contract is tested in.

### (b) An EasyGL example is registered by family identity but compiles against the default renderer

`modules/renderers/CMakeLists.txt:162` re-points `CNA_GRAPHICS_RENDERER` to the identity of the
family currently being entered, and restores it afterwards (`:192`). EasyGL's example block guards
on `CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"` (et al.), so in a multi-renderer build it is entered
**whenever OPENGL33 is in `CNA_GRAPHICS_RENDERERS`**, regardless of the default.

`shader_effect_reflection_contract_test.cpp` names itself from the *project-wide* `CNA_RENDERER_<X>`
macro, and only the **default** renderer's macro is defined project-wide (that is the whole design of
runtime renderer selection). With the default VULKAN it reaches its own `#else`:

```
shader_effect_reflection_contract_test.cpp:53:2: error:
    "ShaderEffect reflection contract requires EasyGL, DirectX 11, or DirectX 12"
```

This is the rule stated at the top of `modules/graphics/examples/CMakeLists.txt`
(`plans/plan_runtimerenderer.md` RTR-P9-13) — *a gate deciding whether a target exists, where that
target runs against whatever renderer the build defaults to, must test the default* — broken in a
way that file's own wording did not anticipate: the per-family re-point turns an equality test into a
membership test. Fixed by registering only when the family being entered **is** the default
(`CNA_GRAPHICS_RENDERER STREQUAL _cna_default_renderer_identity`), which is what the TU reads.

The narrow fix is deliberate. Other EasyGL example TUs in the same block compile in this
configuration; only the ones that switch on the default-renderer macro cannot, and this is the only
one that does. Widening the block's own guard would drop EasyGL tests that currently build and pass,
which is a different decision and not this workstream's to make.

### (c) The same defect a third time, in the headless examples

Found later, rebuilding `cmake-build-multi` (`OPENGL33` default, `HEADLESS` also compiled in) for the
EasyGL and Software regressions:

```
headless_smoke_test.cpp:41:10: fatal error:
    CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp: No such file or directory
```

Six targets, same shape as (b): the block guards on `CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS"`,
which the per-family re-point makes true whenever HEADLESS is merely a *member*. These TUs include a
renderer-private header that only reaches them when headless is the **default**, so the targets exist
and cannot compile. Same fix, and it is also the semantically right one: a test asserting the
headless renderer's behaviour is meaningless in a build that will run OPENGL33.

**Classification:** test/fixture defect (registration), not a renderer defect. None of the three
changes any rendering behaviour. All three are the same rule broken the same way, which is why they
are one task: `CNA_GRAPHICS_RENDERER` inside a family directory names *the family being entered*,
not the renderer the resulting binary will run.

---

## VKPAR-0004 — GSC-F2: Vulkan's classic null-texture semantics

**The contract, XNA-MEASURED.** `GSC-0004` rendered seven scenes on the real XNA 4.0 runtime
(`tools/xna-oracle/scenes/null-texture/`) and checked the references in. Centre pixels, decoded
from `tools/xna-oracle/reference/null-texture/` on this baseline:

| Reference | Centre pixel |
|---|---|
| `basic_textureenabled_null.png` | `(0, 0, 0, 255)` |
| `skinned_null.png` | `(0, 0, 0, 255)` |
| `alphatest_null.png` | `(0, 0, 0, 255)` |
| `dualtexture_texture_null.png` | `(0, 0, 0, 255)` |
| `dualtexture_texture2_null.png` | `(0, 0, 0, 255)` |
| `envmap_texture_null.png` | `(100, 50, 25, 255)` |
| `envmap_cube_null.png` | `(128, 128, 128, 255)` |

Every classic stock effect reads an unbound texture as **opaque black**. Only the CNAEXT PBR base
colour keeps opaque white (glTF's "no `baseColorTexture`" identity), and the glTF importer binds
its own white through `ContentManager` rather than relying on renderer default behaviour.

**What Vulkan does instead.** `VulkanRenderer::FillStockFamilyRecordEXT` binds `defaultWhiteView_`
/ `defaultWhiteCubeView_` for *every* unbound slot, in every classic family:

| Family (`VulkanRenderer.cpp`) | Unbound slot binds | Should bind |
|---|---|---|
| `needsSkinned` | `defaultWhiteView_` | opaque black |
| `needsEnvMap` — `texture0` | `defaultWhiteView_` | opaque black |
| `needsEnvMap` — `envMap` cube | `defaultWhiteCubeView_` | opaque black cube |
| `needsDualTex` — both slots | `defaultWhiteView_` | opaque black |
| `needsLitTextured`/`LitUntextured`/`LitColored` | `defaultWhiteView_` | opaque black **when the effect samples** |
| shared fallback (alpha-test, `colored3d`/`textured3d`) | `defaultWhiteView_` | opaque black |
| `needsPbr`, `needsPbr && needsSkinned` — base colour | `defaultWhiteView_` | **white — correct, keep** |

The renderer has **no opaque-black fallback image at all**; the only 1×1 fillers it creates are
`defaultWhite{,Cube,Volume,Array}` and `defaultFlatNormal`.

**Why no test caught it.** Both renderer-neutral tests gate Vulkan out:

| Test | Gate | Vulkan |
|---|---|---|
| `StockEffectNullTextureTest` | `CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, DirectX11, DirectX12)` | skipped |
| `DualTextureEffectNullSamplerTest` | `CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, OpenGL4, Software, DirectX11, DirectX12)` | skipped |

and Vulkan's own examples **pin the white**, e.g. `vulkan_alphatest_null_texture_test.cpp`:
`kExpectedNullTexture(153, 102, 204, 255)` — which is `diffuse(0.6, 0.4, 0.8) × white`, where the
XNA reference is `(0, 0, 0, 255)`. Its header even records "Vulkan already had the correct
white-texture fallback … no bug found here", written before `SOFTWARE-303`/`GSC-0004` measured what
XNA actually does. This is exactly the shape `GSC-F2` predicted.

**BasicEffect's one exception, carried over from EasyGL.** `TextureEnabled=false` does not sample in
XNA, but Vulkan's lit programs — like EasyGL's — multiply unit 0 in unconditionally. That case keeps
the white identity; the black applies where the effect genuinely samples. `GSC-0004`'s EasyGL hunk
(`else if (!params.pbr && params.textureEnabled)`) is the precedent.

---

### Reproduction — RADV-HW, before any change

Both renderer-neutral suites were pointed at Vulkan (the gates above extended) on the unmodified
renderer, `cmake-build-vulkan`, `DISPLAY=:0`, device `AMD Radeon 780M (RADV PHOENIX)`:

```
9 tests from 2 test suites ran.
[  PASSED  ] 1 test.
[  FAILED  ] 8 tests
```

The readbacks name the defect exactly — `FF-FF FF-FF` is opaque white where the XNA reference is
`00-00 00-FF`:

| Test | Got | Expected |
|---|---|---|
| `StockEffectNullTextureTest.BasicEffectWithTextureEnabledSamplesOpaqueBlack` | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.SkinnedEffectSamplesOpaqueBlack` (both lighting modes) | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.AlphaTestEffectSamplesOpaqueBlack` | `(255,255,255,255)` | `(0,0,0,255)` |
| `StockEffectNullTextureTest.EnvironmentMapEffectSamplesOpaqueBlackForEitherSlot` | 127 off per channel (the white cube reflecting) | reference value |
| `DualTextureEffectNullSamplerTest.NullTexture*` (4 legs) | `(120,200,40,255)` / `(160,80,240,255)` | `(0,0,0,255)` |

The single pass is `BasicEffectWithoutTextureEnabledNeverReadsTheSlot`, and it passes for the right
reason: Vulkan's lit and textured fragment shaders read
`(pc.textureEnabled > 0.5) ? texture(...) : vec4(1.0)`, so `TextureEnabled=false` never samples.

### Fix

`VulkanRenderer` gains an opaque-black 1×1 2D image and an opaque-black 1×1 cube, built exactly like
the existing `EnsureDefaultFlatNormalTexture()` / `EnsureEnvMapResources()` fillers, created lazily
and released with the other defaults. Six binding sites in `FillStockFamilyRecordEXT` and
`DrawInstancedPrimitivesCoreEXT` move from `defaultWhiteView_`/`defaultWhiteCubeView_` to them:
skinned, env-map (2D **and** cube), dual-texture (both slots), the lit BasicEffect families, the
shared alpha-test / `textured3d` arm, and the instanced classic path.

**What deliberately did not change.** The CNAEXT PBR base colour keeps opaque white — glTF's "no
`baseColorTexture`" identity, the same split `GSC-0004` made on DirectX12 — as do the PBR maps, the
flat-normal fallback, the IBL cube fallbacks, the shadow-map fillers, and `ShaderEffect`'s own bound
resources (a CNAEXT custom effect sampling a unit the game never bound gets white, not undefined
memory; that is a different contract and `VULKAN-390` owns it). The instanced site makes the split
explicit with `d.usePbr || d.usePbrSkinned` rather than relying on the PBR pipeline binding a
different set.

**Unlike EasyGL, no `textureEnabled` special case was needed.** `GSC-0004` had to keep a white
identity for EasyGL because its lit programs multiply unit 0 in unconditionally. Vulkan's shaders
already branch on the flag, so the white identity lives in the shader where it belongs and the
binding can be black unconditionally.

### Result — RADV-HW

```
9 tests from 2 test suites ran.
[  PASSED  ] 9 tests.
```

### A second defect found on the way: three Vulkan examples had never asserted anything

`Vulkan_AlphaTest_NullTexture`, `Vulkan_DualTextureEffect_NullTexture0` and
`..._NullTexture2` abort before their first check:

```
CNA: fatal exception escaped Game::Run(): GetBackBufferData is not supported by the Reach graphics profile.
terminate called after throwing an instance of 'System::NotSupportedException'
```

Confirmed **on the baseline sources too** (the three files stashed, targets rebuilt, re-run): this is
not a consequence of this task's edits. `GetBackBufferData` is HiDef-only and these three never
called `setGraphicsProfileProperty`; their EasyGL siblings always have. Three registered tests were
therefore reporting a renderer defect they could not have detected either way. Fixed with the
missing `GraphicsProfile::HiDef`, and they now pass against the corrected values.

Their headers also had to be rewritten rather than just renumbered. All three asserted the white
fallback as *correct* — `vulkan_alphatest_null_texture_test.cpp` recorded "Vulkan already had the
correct white-texture fallback … no bug found here, confirmed by pixel readback" — which predates
`SOFTWARE-303` measuring XNA. And because the expected pixel is now black, each test clears to a
witness colour `(7,199,53,255)` before the draw under test: otherwise "sampled opaque black" and
"drew nothing at all" are the same readback. The AlphaTest test's retry loop, which used to wait for
a *non-black* pixel, now waits for a pixel that is not the witness.

**Classification:** renderer defect (the white fallback) plus test debt (three dead example tests,
and two renderer-neutral suites that skipped this renderer).

---

## VKPAR-0005 — Vulkan facedness, and a recorded rationale that does not hold

`FillDepthStencilState` sends XNA's `CounterClockwise*` operations to `VkStencilOpState front` and
the ordinary ones to `back`. That **arrangement** agrees with what `GSC-0002` later established for
DirectX11/12 (ordinary → `BackFace`, CCW → `FrontFace`). The arrangement is not in question.

**The recorded reason is.** The comment attributes the swap to an unisolated driver quirk:

> Root cause not fully isolated (plausibly an llvmpipe/Mesa software-rasterizer quirk in its own
> front/back `VkStencilOpState` assignment specifically, since culling's front/back classification
> is provably correct on this same driver) — swapped here pragmatically …

There is a mechanical explanation that needs no quirk. Every stock vertex shader negates clip Y
(`pos.y = -pos.y;`, `colored3d.vert.glsl:52` and siblings) because Vulkan NDC Y is inverted, and the
viewport is deliberately left **top-left/positive-height** for that reason
(`VulkanRenderer.cpp:13788`). Negating Y mirrors every triangle, so **framebuffer winding is the
reverse of XNA's displayed winding**, and with `rs.frontFace = VK_FRONT_FACE_CLOCKWISE` an
XNA-displayed-clockwise triangle is `back` to Vulkan. The stencil swap is then exactly right, and
principled — not a workaround.

### What the hardware says

The renderer-neutral `TwoSidedStencilTest` (GSC-0002's, five cases) skipped Vulkan:
`CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, DirectX11, DirectX12)`. Pointed at
Vulkan on RADV-HW it fails, and the failure is a clean inversion on every field:

```
StencilPass:            clockwise triangle left stencil 255 (counter-clockwise operation), expected 5
StencilPass:    counter-clockwise triangle left stencil   5 (ordinary operation),          expected 255
StencilFail:            clockwise triangle left stencil 255 …  StencilDepthBufferFail: same
```

Two things follow immediately, and both contradict what was recorded here.

1. **Stencil gates perfectly well on Vulkan.** Both values land where a rule put them. The
   registration of `Vulkan_DepthStencilState_StencilTwoSided` still says *"expected to FAIL the
   contrast check per Task 870 — stencil testing never gates on Vulkan"*. On this GPU it does.
2. **`front` is the clockwise face here, so the swap is backwards.** A clockwise triangle received
   the *counter-clockwise* operations, i.e. it was evaluated against `ds.front` — which Task 870
   had filled with the CCW fields.

### The convention, settled

The Y-flip does not mirror anything. `pos.y = -pos.y` converts D3D-style clip space (+Y up, flipped
by the viewport transform) into Vulkan clip space (+Y down, not flipped), so a triangle drawn
clockwise as displayed is still clockwise in framebuffer space, and with
`rs.frontFace = VK_FRONT_FACE_CLOCKWISE` Vulkan calls it **front**. Culling has always relied on
exactly that — `CullClockwiseFace → VK_CULL_MODE_FRONT_BIT` — and the whole cull/winding set passes
unchanged on RADV-HW, which is what makes this a measurement rather than a second guess:

| Test | Result |
|---|---|
| `Vulkan_FrontFaceWinding` (the unconditional XNA oracle) | Passed |
| `Vulkan_TriangleStripWinding` | Passed |
| `Vulkan_RasterizerState_CullMode` / `_Camera` / `_Golden` / `_IndexedBasicEffect` | Passed |

So the ordinary operations belong on `front` and the CounterClockwise ones on `back` — the plain,
unswapped assignment, agreeing with culling, with `frontface_winding_test.cpp`'s contract and with
`GSC-0002`'s reading of XNA's `DepthStencilState::Apply`. The swap is removed and the comment now
records the measurement instead of an unisolated "llvmpipe/Mesa quirk".

**Classification:** renderer defect, introduced by a software-rasterizer-era diagnosis that real
hardware contradicts. It is also the answer to the question `VKPAR-0002` left open about whether
`plan_vulkan.md`'s llvmpipe verdicts survive contact with RADV: this one did not.

---

## VKPAR-0006 — 103 Vulkan example tests had stopped asserting anything

The largest single finding of this workstream, and it is not a rendering defect.

`SOFTWARE-213` (`d72162d7b`, 2026-09-09) made `GraphicsDevice::GetBackBufferData` throw under
`GraphicsProfile::Reach`, which is XNA's real rule and is correct. It updated the gtest suites that
needed it. It did not touch any renderer's example tests, and `GraphicsDeviceManager` defaults to
Reach. Every example that reads the back buffer without asking for HiDef has aborted ever since:

```
CNA: fatal exception escaped Game::Run(): GetBackBufferData is not supported by the Reach graphics profile.
terminate called after throwing an instance of 'System::NotSupportedException'
```

Measured across the example suites on the baseline — TUs that call `GetBackBufferData` and never set
`GraphicsProfile::HiDef`:

| Family | example TUs | call `GetBackBufferData` | of those, no HiDef |
|---|---|---|---|
| **vulkan** | 154 | 110 | **104** |
| easygl | 246 | 171 | 4 |
| software | 22 | 18 | 0 |
| webgpu | 60 | 35 | 32 |
| sdl-gpu | 46 | 4 | 3 |

EasyGL and Software were carried across the change; Vulkan was not. Re-running each failing
`Vulkan_*` ctest **serially**, so that nothing is blamed on parallel GPU contention, classifies them:

| Cause | Count |
|---|---|
| Aborts on the Reach profile — asserts nothing | **103** |
| Genuine failures (see `VKPAR-0010`) | 42 |

None passed when run alone, so the parallel run's counts were not flattered or inflated by
contention either.

`plan_vulkan.md`'s parity verdict is dated **2026-09-11**, two days after `SOFTWARE-213`. Its
`^Vulkan_ 258/258` evidence predates the change, which is how a suite this large went quiet without
anyone noticing: a test that aborts is red in `ctest`, and these were being read as the renderer's
known-failing set rather than as a suite that had stopped running.

**Fix.** All 104 TUs now request HiDef, by the two shapes the EasyGL and Software suites already use
— `gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef)` where the test owns a
`GraphicsDeviceManager` (82 files), and
`game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(...)` in `main()` before `Run()` where it
does not (22 files). No test's assertions were touched.

**Not fixed here:** WebGPU's 32 and SDL_GPU's 3. They are the same defect in renderers this
workstream is explicitly not opening; recorded for their own plans.

### Result — the `^Vulkan_` suite, RADV-HW

| | Before | After |
|---|---|---|
| Registered | 370 | 370 |
| **Passing** | **224** | **318** |
| Failing | 146 | **52** |
| — aborting on the Reach profile | 103 | **0** |
| — genuine failures | 42 | 51 |
| Newly failing | — | **0** |

Ninety-four tests changed state and none went the wrong way. Of the 103 that were dead, **91 now
pass outright** and **12 run far enough to fail for a real reason** — they are not new defects, they
are defects that were already there and could not be seen. `Vulkan_DepthStencilState_StencilTwoSided`
is `VKPAR-0005`'s; `Vulkan_RenderTarget_BlendFactor` and `Vulkan_Swapchain_Sync` also pass now and
are not claimed by either fix, so they are recorded as passing without an attributed cause rather
than counted as wins.

**Classification:** test debt, shared across renderer families, caused by a correct production
change that a whole class of test was not carried across.

---

## VKPAR-0012 — Validation and synchronization validation

Nothing had to be switched on for this: `VulkanRenderer` already requests
`VK_LAYER_KHRONOS_validation` in any build without `NDEBUG` (`sEnableValidation`), checks at runtime
that the layer is really present, installs a `VK_EXT_debug_utils` messenger at
`WARNING|ERROR` severity, and records both `pMessage` and the stable `pMessageIdName` before echoing
through `CNA::Logger` with a `[Vulkan Validation]` prefix. `cmake/TestHelpers.cmake` then puts that
prefix in **every** registered renderer test's `FAIL_REGULAR_EXPRESSION`, so a warning or error fails
the test that produced it. `cmake-build-vulkan` is `Debug`, so **every run in this plan already had
validation on**.

| Measurement | Result |
|---|---|
| `[Vulkan Validation]` messages, whole 10141-test run | **0** |
| `[Vulkan Validation]` messages, 370-test `^Vulkan_` run | **0** |
| `VK_LAYER_KHRONOS_validation` confirmed loaded by the tests that assert it | yes (3 assertions) |
| `Vulkan_RenderTarget_ProducerConsumer_SyncVal` | **Passed** |
| `Vulkan_Swapchain_Sync` | **Passed** (28.5 s) |

Synchronization validation is not part of the layer's default set; CNA requests it through
`VkValidationFeaturesEXT` from inside the process (`SetSyncValidationEnabledEXT`), and lifts the
layer's ten-repeats-per-id cap via `VK_EXT_layer_settings` so a reported hazard *count* means
something. Both tests that use it pass on RADV-HW.

So: **zero unexplained validation errors, zero unexplained synchronization hazards, nothing
suppressed by string.** What this does *not* yet cover is sync validation across the whole suite
rather than the two tests that opt into it; that is the remaining half of the phase and is recorded
as such rather than claimed.

---

## VKPAR-0007 / VKPAR-0009 — X11, and the SDL-free X11 configuration

### The surface path, RADV-HW

Every Vulkan run in this plan presents through `DISPLAY=:0`, which is **Xwayland** under the live
GNOME/Mutter session, and every one of them reports the hardware device:

```
[Vulkan] GPU: AMD Radeon 780M (RADV PHOENIX)
CNA: Vulkan capabilities -- device=AMD Radeon 780M (RADV PHOENIX); MSAA up to 8x;
     MRT up to 4 targets (FNA MAX_RENDERTARGET_BINDINGS); anisotropic filtering: supported, max 16x;
     wireframe fill mode: supported; independent MRT blend/write state: supported;
     render-target formats: Color plus device-queried Rgba64/float/HDR 2D and cube storage;
     detailed format usage: 27 formats classified
```

So the X11 window → `VkSurfaceKHR` → RADV → swapchain → present path is exercised by all 370
`Vulkan_*` tests and by the demo runs below. `Xvfb :99` cannot do this at all (no DRI3, see
`VKPAR-0001`), which is worth stating plainly because it is the display an agent shell gets by
default here.

### `cna_demo_2d` on Vulkan + X11

`cna_demo_2d --smoke N` runs exactly N frames and exits, so a soak is bounded and its exit code
means something.

| Run | Frames | Exit | RSS | fds | threads | `[Vulkan Validation]` |
|---|---|---|---|---|---|---|
| short | 3 000 | 0 | 98 760 kB → 98 760 kB | 31 → 31 | — | 0 |
| soak | 10 000 | 0 | 98 512 kB → 98 516 kB (**+4 kB total**) | 31 → 31 | 9 → 9 | 0 |

The soak was sampled at roughly 10 s, 45 s, 90 s, 135 s and 165 s. Four kilobytes of drift across ten
thousand frames is not progressive growth, and neither descriptors nor file descriptors nor threads
moved at all.

### SDL-free

`cmake-build-multi` is configured `CNA_PLATFORM=X11`, `CNA_ENABLE_SDL=OFF`, `CNA_AUDIO_PLATFORM=NULL`,
with VULKAN among `CNA_GRAPHICS_RENDERERS` — the configuration the brief asks for, already in the
tree. Its `cna_demo_2d` (which links `cna_content`) has **no SDL dependency of any kind**:

```
$ ldd  cmake-build-multi/cna_demo_2d | grep -ci libSDL      → 0
$ readelf -d cmake-build-multi/cna_demo_2d | grep NEEDED
    libX11.so.6  libXext.so.6  libXi.so.6  libXrandr.so.2  libXcursor.so.1  libXau.so.6
    libXss.so.1  libvulkan.so.1  libzstd.so.1  libav*.so  libstdc++  libm  libgcc_s  libc
```

`libvulkan.so.1` and the X libraries, and nothing else window-system-shaped. Runtime renderer
selection is by the `CNA_GRAPHICS_RENDERER` environment variable, so this binary runs Vulkan on the
native X11 backend with no SDL in the process.

> **Correction (`plans/plan_gpu_test_isolation.md` GTI-0005).** `cmake-build-multi` and
> `cmake-build-wayland` are **RelWithDebInfo**, so the renderer loaded no validation layer in either, and
> the "zero `[Vulkan Validation]` messages" reported for the SDL-free X11 and native Wayland runs below
> measured nothing. Re-measured with the layer injected by the loader, and proven active per instance:
> 0 errors and 0 warnings on both paths. The rendering results were never in question.

## VKPAR-0008 — Wayland

`cmake-build-wayland`: `CNA_PLATFORM=WAYLAND`, `CNA_ENABLE_SDL=OFF`, `CNA_AUDIO_PLATFORM=ALSA`,
VULKAN among `CNA_GRAPHICS_RENDERERS`. Rebuilt at this baseline, 0 errors.

### SDL-free, and X11-free

`cna_demo_2d`'s **direct** dependencies are the whole story:

```
NEEDED  libwayland-client.so.0   libxkbcommon.so.0   libvulkan.so.1
        libzstd  libav{codec,format,util}  libswresample  libstdc++  libm  libgcc_s  libc
```

No SDL, no X11, no xcb, no GLX. `ldd` does show `libX11`/`libxcb` transitively, and they are
traceable to **`libavutil.so.59`** (FFmpeg's VA-API), not to CNA: `libavcodec`, `libavformat` and
`libvulkan` each reference none. The repository's own gates agree — `CnaWaylandLinkClosure` and
`WaylandIsSdlFree`, **6/6 passed**.

### The surface path, against the real compositor

`cna_demo_2d` has no test-harness environment override, so it runs against the live GNOME/Mutter
session (`WAYLAND_DISPLAY=wayland-0`), which is the genuine
window → `VkSurfaceKHR` → RADV → swapchain → present path:

| | |
|---|---|
| Device | `AMD Radeon 780M (RADV PHOENIX)` |
| Frames | 3 000, exit 0 |
| RSS | 112 628 kB → 112 636 kB (**+8 kB**) |
| fds | 49 → 49 |
| `[Vulkan Validation]` | **0** |

### The contract tests, against a private compositor

`CnaTests` deliberately cannot reach the developer's desktop: `WaylandTestEnvironment` rewrites
`WAYLAND_DISPLAY` to `cna-test-no-compositor` and the session bus to a nonexistent path before any
test runs (`plan_wayland.md` WAYLAND-0110), so a test can never open a window on a screen nobody is
watching. The sanctioned route is `tools/platform/wayland_test_server.sh`, which starts a private
compositor and names it in `CNA_WAYLAND_TEST_DISPLAY`. Through Weston's headless backend with the
GL renderer, selecting Vulkan at runtime:

```
14 tests from 3 test suites ran.   [  PASSED  ] 14 tests.
[Vulkan] GPU: AMD Radeon 780M (RADV PHOENIX)
```

— the same `StockEffectNullTextureTest`, `DualTextureEffectNullSamplerTest` and
`TwoSidedStencilTest` cases this branch corrected, passing on the native Wayland backend on real
hardware.

### Fractional scaling

125% **is** active on this desktop and the Wayland run above happened under it:
`~/.config/monitors.xml` carries `<scale>1.25</scale>`, `org.gnome.mutter experimental-features` is
`['scale-monitor-framebuffer']`, and Xwayland's logical size is `1536x960`. What is demonstrated is
that the Vulkan path runs cleanly on a fractionally-scaled session; a positive assertion tying
logical size to swapchain extent is **not** made here and is left to a later row rather than implied.

---

## VKPAR-0011 — GSC-F1, reproduced on hardware and narrowed, not fixed

The brief asks for this first and time-boxes it. Reproduced, narrowed by one measurement, and
stopped there on purpose.

**Reproduced**, `cmake-build-multi` (`OPENGL33` default, EasyGL on Mesa 25.0.7 / Radeon 780M),
with `StockEffectNullTextureTest`'s own `GSC-F1` skip lifted:

```
StockEffectNullTextureTests.cpp:230: Failure    [trace: per pixel]
  Render([&]{ DrawSkinnedQuad(effect); })  is  <FF-FF FF-FF>   (255,255,255,255)
  kRed                                     is  <FF-00 00-FF>   (255,0,0,255)
```

Note *which* draw fails: the **control** draw, with a real red `Texture2D` bound — not the
missing-texture leg. The per-vertex leg of the same loop passes, so the two programs disagree while
everything above them is identical. That matches `GSC-F1`'s description and confirms it on hardware
this project had not run it on.

**Narrowed.** The obvious candidate was the specular term: `EnsureSkinnedProgram`'s fragment stage
ends with `FragColor.rgb += specularRGB * FragColor.a`, and a stale or non-zeroed per-light specular
with the material's default `SpecularColor` of white would add exactly enough to saturate
`(1,0,0)` to `(1,1,1)`. It is **not** that. `SkinnedEffect.cpp:437` already zeroes each disabled
light's specular (`light0On ? … : Vector3::Zero`), and forcing `SpecularColor` to zero in the test
leaves the pixel white.

So the white is in the diffuse path — `litRGB * texColor.rgb` — which leaves two candidates worth
trying next: `uEmissiveColor` (the pre-folded `emissive + ambient*diffuse`) arriving wrong for this
program, or `texColor` itself sampling white, i.e. the per-pixel skinned program not seeing the
bound texture that the per-vertex one does.

**Not fixed.** Past this point it is an EasyGL shader/uniform-binding investigation, which is the
workstream the brief explicitly says not to start. The skip in `StockEffectNullTextureTest` stays
and still names `GSC-F1`; what is new here is that the defect is now reproduced on real hardware and
one hypothesis is eliminated in writing rather than left for the next reader to re-try.

---

## VKPAR-0010 — Baselines

`cmake-build-vulkan`, `DISPLAY=:0` (Xwayland, DRI3), `ctest -j6`, RADV-HW. Whole suite, 812 s:

```
10141 tests, 97% passed, 267 failed
```

That single number is not usable as it stands, and saying why is the point of this row:

| Group | Failing | What it actually is |
|---|---|---|
| `Vulkan_*` | 146 | this renderer. **103** abort on the Reach profile (`VKPAR-0006`), **42** are genuine, 1 is a stress timeout |
| `EasyGL_*` | 89 | **not an EasyGL result.** This build's default renderer is VULKAN, so these binaries select Vulkan at runtime while asserting EasyGL's contracts. The same membership-vs-default confusion `VKPAR-0003` fixed for one TU, here affecting a whole block. EasyGL is measured in `cmake-build-multi`, where OPENGL33 **is** the default |
| everything else | 32 | shared gtest suites; see below |

Before `VKPAR-0003` this suite could not be measured at all: the build stopped, and `ctest`
registered **917** tests rather than 10141.

### The 42 genuine `Vulkan_*` failures, by area

Each was re-run alone, so none of these is parallel-GPU contention; none passed when isolated.

| Area | Tests |
|---|---|
| Texture / cube / volume transfer | `CubeVolume_GetDataContract`, `CubeVolume_SetDataContract`, `CubeFaceReadbackDependency`, `Texture3D_Mip_Layout`, `Texture3DAddressW`, `Dxt1FromStream`, `DxtTextureCube`, `InvalidMipLevel`, `TextureFilterMipContract` |
| Render targets / MRT | `MRT_MixedFormats`, `MRT_MsaaResolve`, `MrtMipFinalization`, `RenderTargetCube_DepthFormat`, `RenderTargetCube_PluralMRT`, `RenderTarget_BlendFactor`, `RenderTarget_DepthStencilUsage`, `FloatRenderTarget`, `BoundMsaaReadback` |
| Format capability | `SurfaceFormat_Throws`, `SurfaceFormatClassification`, `FormatLimitQueries`, `CapabilityContract`, `ProfileLimitsAudit`, `AdapterQueryContract` |
| Two-sided stencil | `DepthStencilState_StencilTwoSided` — **closed by `VKPAR-0005`** |
| Draw / layout | `DrawRangeValidation`, `IndexBuffer_UploadBounds`, `DeclaredEffectLayout`, `PipelineKeyStateCoverage` |
| SpriteBatch / font | `SpriteBatch_BlendState`, `SpriteBatch_SortModeSemantics`, `SpriteFont_Properties` |
| Resource lifetime | `BoundResourceDispose`, `ResourceOutlivesDevice`, `MoveSemantics`, `DescriptorContractUniformity` |
| Clear / present / sync | `GraphicsDevice_ClearOptions`, `GraphicsDevice_OrderedClear`, `Backbuffer_PassOrder`, `Swapchain_Sync` |
| Occlusion / shader-effect | `OcclusionQuery_Cycle`, `OcclusionQuery_Precision`, `ShaderEffect_BoundTexture`, `ShaderEffect_PerUnitSampler` |
| Stress | `DynamicBufferStress` (timeout) |

### Shared-suite failures that are not `Vulkan_*`-prefixed

| Test | First read |
|---|---|
| `Texture3DTextureCubeContentTypeReaderTest.TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd` | `TextureCube::GetData: the active renderer did not return the complete compressed cube face region` — renderer, compressed cube readback |
| `…Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder` | `Texture3D::SetData: the active renderer did not store the complete declared-format volume region` — renderer, volume upload |
| `ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` | `NormalizedByte2` samples blue **0**, contract says **255**. Vulkan sets `VK_COMPONENT_SWIZZLE_IDENTITY` everywhere and never expands a missing channel, so XNA's D3D9 one-/two-channel rule is not implemented. `plan_vulkan.md`'s own `F-11` predicted exactly this ("the D3D9 one-/two-channel expansion rule it exists for would then be wrong") and `VULKAN-174` then pinned blue **= 0** in the renderer's own test, so the two suites disagree by construction |
| `ClassicTextureFormat.NormalizedIntegerCubeFormats*` (2) | same family |
| `HdrRenderTargetRoundTripTest` (2) | float/HDR render-target transfers |
| `InstancedDrawMultiStreamTest.DuplicateSemanticStreamsRemapToUnusedIndices`, `OrdinaryDrawBindingOffsetTest.MultipleStreamsUseOnlyTheGeometryStreamsOwnOffset` | both throw *"The vertex declaration contains a duplicate usage and usage index"* from the **public layer**, before the renderer sees anything |
| `OrdinaryDrawMultiStreamTest.SixteenBindingsCanSupplyAConsumedSemanticFromSlot15` | `CNA Vulkan: the combined multi-stream declaration does not supply every input of the selected stock shader` |
| `UnicodeContentRootTest` / `UnicodeTree` (3) | content path resolution, no renderer involvement |

None of these is caused by this branch: `VKPAR-0004`'s change decides only which image view is
bound when a texture is **null**, and `VKPAR-0005`'s only the `front`/`back` stencil assignment.

---

## Regression matrix

Every renderer below runs on this machine's real hardware. `cmake-build-multi` is
`CNA_PLATFORM=X11`, `CNA_ENABLE_SDL=OFF`, four renderers compiled in and selected by the
`CNA_GRAPHICS_RENDERER` environment variable; `cmake-build-vulkan` is the SDL3 platform.

The suites are the ones this branch touched — `StockEffectNullTextureTest`,
`DualTextureEffectNullSamplerTest` and `TwoSidedStencilTest`, 14 cases:

| Renderer | Build | Result |
|---|---|---|
| **Vulkan** (RADV, SDL3 platform) | `cmake-build-vulkan` | **14/14 passed** |
| **Vulkan** (RADV, native X11, **no SDL**) | `cmake-build-multi` | **14/14 passed** |
| **EasyGL / OPENGL33** (Mesa 25.0.7, OpenGL 4.6 core) | `cmake-build-multi` | **14/14 passed** |
| **Software** | `cmake-build-multi` | **14/14 passed** |
| **Headless** | `cmake-build-multi` | skipped cleanly — no raster path, which is what the gates say |

No Direct3D regression is required by this branch: nothing it changes is shared with DirectX11 or
DirectX12. `VKPAR-0004` and `VKPAR-0005` are inside `modules/renderers/vulkan/`; `VKPAR-0003`
changes only CMake registration; the two gtest gate edits **add** Vulkan to a renderer list and
leave every other renderer's arm byte-for-byte, which the EasyGL and Software columns above
demonstrate rather than assert.

---

## VKPAR-0014 — Channel expansion, measured

Two explicit contracts disagreed about the same pixel, so one of them had to be wrong:

| Source | `NormalizedByte2(0.5, 0.25)` sampled | Provenance |
|---|---|---|
| `ClassicTextureFormatTests.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` (renderer-neutral) | `(129, 64, 255, 255)` | `SOFTWARE-142`: *"missing channels follow the XNA/D3D texture rule"* — reasoned from documentation and FNA, **not measured** |
| `vulkan_snorm_format_test.cpp` (`plan_vulkan.md` `VULKAN-174`) | blue = **0** | an anti-mutation argument about storage width (*"there is no third byte"*) — **not a claim about sampling at all** |
| Vulkan renderer | blue = **0** | `VK_COMPONENT_SWIZZLE_IDENTITY` on every view: Vulkan reads a missing colour channel as 0 |

Neither side had measured anything, so neither could be the tie-break.

### The measurement

`tools/xna-oracle/FormatExpansionOracle.cs` — a new, deliberately small XNA program built against the
real GAC assemblies, because the scene-driven oracle has no `SurfaceFormat` key. It is the exact
shape of the renderer-neutral test: 1×1 texture of the format, `SpriteBatch` with `PointClamp` and
`BlendState.Opaque`, into an 8×8 `Color` render target, centre pixel read back. Run on **XNA 4.0 →
wine 10.0 → DXVK 2.6.0 → RADV, the same Radeon 780M**; result checked in as
`tools/xna-oracle/reference/format-expansion/xna-format-expansion.txt`:

| Format | XNA | | Format | XNA |
|---|---|---|---|---|
| `NormalizedByte2` | **128, 64, 255, 255** | | `Single` | 64, **255, 255, 255** |
| `Rg32` | 128, 64, **255, 255** | | `HalfSingle` | 64, **255, 255, 255** |
| `Vector2` | 64, 128, **255, 255** | | `Alpha8` | **0, 0, 0**, 128 |
| `HalfVector2` | 64, 128, **255, 255** | | `Bgr565` | 132, 65, 255, **255** |

The rule, now measured: **a colour channel the format does not store samples as 1.0, a missing alpha
samples as 1.0, and `Alpha8` is the one exception at `(0, 0, 0, A)`.** The renderer-neutral suite was
right; `VULKAN-174`'s pin was wrong. Every one of its thirteen expectations agrees with the table
(the renderer-neutral `129` against XNA's `128` for the SNORM formats is inside its tolerance of 2).

### The fix

Vulkan's own rule differs only in the colour channels — it already reads a *missing alpha* as 1.0 —
so a view's component mapping closes the gap without any shader knowing which format it samples.
`ClassicSampledSwizzleEXT(surfaceFormat)` holds the table; it is keyed on the CNA `SurfaceFormat`
rather than the `VkFormat` because `VK_FORMAT_R8_UNORM` could serve both a one-channel colour format
(`(r,1,1,1)`) and `Alpha8` (`(0,0,0,r)`), which the `VkFormat` cannot tell apart.

Of the formats this renderer actually implements (`Color`, `Bgr565`, `Bgra5551`, `Bgra4444`,
`NormalizedByte2`, `NormalizedByte4`, `Dxt1/3/5`) **only `NormalizedByte2` needs a row** — blue →
`VK_COMPONENT_SWIZZLE_ONE`. `Bgr565` already gets alpha 1.0 from Vulkan. The unimplemented formats take
the identity default and each adds its row when it lands. Applied to the sampled 2D view and the cube
view; the **storage-bridge view is reset to identity** before it is created, because a storage image
must not be swizzled.

`vulkan_snorm_format_test.cpp` now expects blue 255, and says what that costs: blue can no longer
catch four-wide storage, because it is constant by construction. The R/G legs still catch a channel
swap, the negative leg still separates SNORM from UNORM, and storage width stays pinned by
`MapSurfaceFormatToStorageEXT`'s two-byte entry and by
`ClassicTextureFormat.EveryPromotedFormatPreservesFullPartialAndMipBytesExactly`.

### Cross-renderer

`ClassicTextureFormat.*`, `cmake-build-multi` (SDL-free X11) plus `cmake-build-vulkan`:

| Renderer | Passed | Failed | Skipped |
|---|---|---|---|
| EasyGL / OPENGL33 | 13 | 0 | 0 |
| Software | 13 | 0 | 0 |
| **Vulkan** | 7 | 2 | 4 |

`PointSamplingExpandsChannelsAndPreservesDeclaredRanges` passes on Vulkan now. The two failures are
`NormalizedIntegerCubeFormats{PreserveExactTransfers,FeedEnvironmentMapSampling}`, which throw
*"TextureCube::SetData: the active renderer did not store the complete declared-format cube region"*
— a cube-transfer defect already in `VKPAR-0013`'s transfer group, failing before this change too,
unrelated to channel expansion. The four skips are formats Vulkan does not claim. `Vulkan_NormalizedByteFormat`
passes with the corrected value.

No Direct3D run: the change is inside `modules/renderers/vulkan/` and a Vulkan test. D3D11/12 are in
`GSC-0001`'s matrix and were not touched.

### Full-suite regression — on a private display this time

All 10 141 registered tests, `cmake-build-vulkan` rebuilt with the change (0 errors, 0 warnings),
run through the private route `VKPAR-0015` describes (headless Weston, rootful Xwayland on a private
display, nothing on the owner's desktop), compared test by test with the earlier baseline:

| Group | Before | After |
|---|---|---|
| `Vulkan_*` | 318 passed, 51 failed, 1 timeout | **319 passed, 51 failed** |
| everything else (gtest) | 8 947 passed, 32 failed, 279 skipped | 9 076 passed, 34 failed, 277 skipped |

`Failed → Passed`: `ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` —
this row — plus three `Unicode*` path tests whose earlier failures were environmental. `Skipped →
Passed`: the five `TwoSidedStencilTest` cases (`VKPAR-0005`). No `Vulkan_*` test changed state in
either direction.

`Passed → Failed`, six, none of them this change:

| Test | Cause |
|---|---|
| `ContentPipelineCliTest.WorkerCountsProduceIdenticalColdNoOpAndDependencyRebuilds`, `ContentRuntimeContractTest.TheBaseOpenStreamServesTheContentRoot`, `SavedPictureStoreTest.SavePictureWritesARealReadableFile` | **pass when re-run serially** in the same private environment. Parallel races on shared paths (`/tmp/cna_content_contract_1`, a fixture directory under `tests/assets/media/`, a staging scavenger seeing a sibling's directory) — fixture debt, not renderer |
| `XnaPipelineGenuineRuntimeInterop`, `…InteropLzx`, `XnaDifferentialBuildTest.CnaAcceptsAndRefusesTheSameSourcesXnaDoes` | Wine + the real XNA runtime. They hang inside the private wrapper (it gives them a private `XDG_RUNTIME_DIR` and no session bus) and passed in ~11 s in the earlier run. The two interop runs were stopped by hand rather than left for 2x 15 min; a hung `winedbg --auto` was what kept the harness's pipe open. **A limitation of the private route for Wine-based tests**, not a CNA result |

`EasyGL_*` is omitted from the table for the reason `VKPAR-0010` gives: in a Vulkan-default build
those binaries run Vulkan.

---

## VKPAR-0015 — Where these tests ran, and how to stop that

Recorded because it matters more than any single test result. **Every GPU run in this plan before
this row — the full 10 141-test suite, the 370 `Vulkan_*` tests, the `cna_demo_2d` soaks, the Wine
oracle and the cross-renderer format runs — opened windows on the owner's live desktop.** `:0` is the
Xwayland of the owner's running GNOME session, and `wayland-0` is that session's compositor. Where
the sections above describe `:0` as "the DRI3-capable server", read it as "the owner's screen".

Why it is easy to do by accident, and will keep happening to anyone who runs `ctest` here:

* `CNA_TEST_DISPLAY` is `:0` in the build directories, and **757** registered tests carry
  `ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"`. `ctest` applies that per test, so
  exporting a different `DISPLAY` before calling it changes nothing.
* The other **9 378** (gtest cases) inherit the caller's `DISPLAY`.
* `Xvfb :99` is the private alternative an agent shell gets, and it cannot present Vulkan at all (no
  DRI3), which is precisely what pushes a Vulkan run toward `:0`.

The route that is private *and* keeps the X11 code path identical: `tools/platform/wayland_test_server.sh`
(headless Weston, private runtime directory, `DISPLAY` unset) with a **rootful Xwayland on a private
display number** inside it. Measured: that display has DRI3, enumerates the Radeon 780M, and leaves
nothing behind. The runs recorded after this row use it, through a small runner that applies each
test's registered ctest properties but forces `DISPLAY` to the private server and refuses to start
outside the private compositor.

Not changed here, because it is the owner's decision and it affects every agent on the machine: the
`:0` default itself.

---

## VKPAR-0016 — 87 GB in one build directory, and what it cost the SSD

The owner found `cmake-build-vulkan` at **87 GB**. At the start of this workstream it was **1.4 GB**
— only because `VKPAR-0003`'s build break stopped the build before anything linked. Unblocking it
linked everything.

| Measured | |
|---|---|
| Executables at the build root | **798**, 82.9 GB, mean **104 MB**, largest 691 MB |
| `cna_test_vulkan_*` | 367 files, 38.8 GB |
| `cna_test_easygl_*` | **341 files, 36.2 GB** |
| One test (`cna_test_vulkan_basiceffect_one_light`, 106 MB) | `.debug_info` 42.8 + `.debug_str` 23.7 + `.debug_line` 6.5 + other DWARF ≈ **77 MB**; `.strtab`+`.symtab` 17.5 MB; **`.text` 7 MB** |
| Linkage | the whole CNA runtime **statically** in every executable; `NEEDED` is only SDL, Vulkan and system libraries |

Two separate causes, and the second multiplies the first:

1. **Every test is its own executable holding a full static copy of the engine** — 7 MB of code and
   ~80 MB of its Debug DWARF, 798 times — so any edit to a module relinks all of them.
2. **341 of those executables should not exist in this build.** `CNA_GRAPHICS_RENDERERS` is
   `VULKAN;OPENGL33` with Vulkan the default, and EasyGL's example block was gated on the identity of
   the family being *entered* — the same membership-for-default mistake `VKPAR-0003` fixed for one TU
   and for the headless block. Each of those 341 binaries selects Vulkan at run time while asserting
   EasyGL's contracts (`VKPAR-0010` already classified their results as meaningless here), and each was
   relinked on every Vulkan renderer edit because it links the Vulkan renderer too.

**The SSD cost of this workstream**, from the build logs: ~3 300 executable links in
`cmake-build-vulkan` (five full relinks after one-line renderer edits, plus smaller ones) ≈ **345 GB**,
and ~240 links in `cmake-build-multi`/`cmake-build-wayland` ≈ 30 GB — **~375 GB written in one
session**, ~140 GB of it for the 341 binaries that should not have existed. That is exactly the
waste `CLAUDE.md` exists to prevent, and most of it came from rebuilding the whole tree to run a
handful of targets.

**Done here:**

* the 341 `cna_test_easygl_*` binaries were deleted from `cmake-build-vulkan` (checked unused first;
  82 → 48 GB);
* EasyGL's example block now requires a GL profile to be the build's **default** renderer
  (`_cna_default_renderer_identity IN_LIST OPENGLES2;OPENGLES3;OPENGL33`). Reconfigured,
  `cmake-build-vulkan` generates **0** EasyGL example targets; `cmake-build-multi`, whose default is
  OPENGL33, is unaffected. The EasyGL parity fixtures, the diagnostic scene and the 2D corpus are
  registered inside the same block and follow it.

The first cause is `VKPAR-0017`.

---

## VKPAR-0017 — `libcna.so` instead of 798 static copies of the engine

The owner's decision, after `VKPAR-0016`: link the runtime as a shared library rather than into every
executable. `CNA_SHARED_LIBRARY` (default **ON** on native ELF GNU/Clang with CMake ≥ 3.27, off — and
refused if forced — elsewhere); the mechanism and its scope are written up in
`docs/build-performance.md` "Shared runtime library", which is where build policy lives.

**Measured on `cmake-build-vulkan`**, same code as `VKPAR-0014`:

| | Before | After |
|---|---|---|
| Build directory | 87 GB | **5.4 GB** |
| Executables at the build root | 798, mean 104 MB | 426, mean 4.8 MB (a typical test 0.5–0.9 MB) |
| `libcna.so` | — | 182 MB once — both renderers, 98 725 exported symbols, **0 unresolved** (`ldd -r`) |
| Relinked by one Vulkan renderer `.cpp` edit | ~800 targets, **~83 GB** | **29 targets, ~1.7 GB** (read from the `build.ninja` graph) |
| Relinked when `libcna.so` changes | — | **0** (`CMAKE_LINK_DEPENDS_NO_SHARED`) |

`ninja -t cleandead` removed 430 outputs of targets that no longer exist (the EasyGL example block's
remaining 36 executables among them) once nothing was running from the tree.

**No code is present twice.** A strong symbol in both `libcna.so` and an executable would mean two
copies of its code and static state. `CnaTests` put `cna_test_build_config` — which brings
sharp-runtime's archives — before `CNA`; it now names `CNA` first. Checked with `nm` on the built
binaries: every symbol defined in both is an `R_X86_64_COPY` relocation (one live object, e.g.
`BlendState::Opaque`); 0 strong duplicates in `CnaTests` or the sampled examples.

**Both modes and the EasyGL gate, configure-only**, in `build-probe/cfg-vkpar0017*` (removed after):

| Configuration | Static | Shared |
|---|---|---|
| OPENGL33 default, SDL3 | configures; 342 `cna_test_easygl_*`, the reflection test, 32 EasyGL parity fixtures | same, plus `libcna.so` |
| OPENGL33 default, SDL off | configures; EasyGL block skipped by its own pre-existing SDL guard | configures |
| `cmake-build-vulkan` (VULKAN default) | — | 0 EasyGL example targets |

**Regression — the full suite, private display, test by test against the static build of the same
commit:**

| Group | Static | Shared |
|---|---|---|
| `Vulkan_*` | 319 passed, 51 failed | **319 passed, 51 failed** |
| everything else | 9 066 passed, 30 failed, 277 skipped | 9 065 passed, 30 failed, 278 skipped |

Three tests left `Passed` (`AudioEngineTest.UpdateSweepsFinishedFireAndForgetCue…`,
`DynamicSoundEffectInstanceTest.BufferNeededFiresExactlyTheStarvedCount`,
`StorageDeviceDeleteContainerTest.ContainerAllowsNormalizedPathsThatRemainContained`) and two joined
it; **all three pass 3 of 3 serially** — timing under `-j6`, not the link change. Eight
parameterised audio tests appear only in one run because GoogleTest prints their parameter's raw
bytes, pointers included, into the name.

The six Wine/XNA tests ran separately on Xvfb `:99` (their harness pins it): both
`XnaPipelineGenuineRuntimeInterop` legs pass against the shared build (30 s, 31 s);
`XnaPipelineGenuineRuntimeBuiltFamilies` fails, **as it already did in the static baseline**;
`XnaDifferentialBuildTest.CnaAcceptsAndRefusesTheSameSourcesXnaDoes` hangs to its timeout on `:99`,
on the private Xwayland and inside the private compositor alike — it hung the same way with the static
build privately, and passed only in the first run of this workstream, on the owner's live desktop.
It needs something from that session and is recorded as an environment limitation, not rerun there.

**Not done:** the other build directories. Each switches to the new default the next time it is
configured, which recompiles it once for `-fPIC`; none was rebuilt here, deliberately.

### Release smoke before the merge

A throwaway `build-probe/vkpar-release-smoke` (removed afterwards): `CMAKE_BUILD_TYPE=Release`, the
same `VULKAN;OPENGL33` SDL3 configuration, shared by default. Built only `libcna.so`, `cna_demo_2d`
and three Vulkan tests — which compiles the whole engine in Release, `NDEBUG` paths included —
rather than a whole second tree. 0 errors; **Release `libcna.so` is 16 MB**.

The 24 warnings are not this branch's: 16 in vendored draco, and 8 GCC `-O2` flow diagnostics inside
libstdc++ headers from five untouched translation units (`CnbModelCodec`, `GltfImportCore`,
`XnbWriter`, `DibBitmap`, draco's `ply_reader`). The same five, recompiled **without** `-fPIC` in
the static mode, emit the same 8, so the link change did not cause them either.

On the private display: `Vulkan_BasicEffect_OneLight` and `Vulkan_FrontFaceWinding` pass and
`cna_demo_2d --smoke 600` runs on the Radeon. `Vulkan_NormalizedByteFormat`'s rendering legs pass —
blue 255 included — but its leg D1, *"VK_LAYER_KHRONOS_validation is loaded"*, fails, because Release
deliberately runs without validation (`sEnableValidation` is off under `NDEBUG`). That assertion
predates this workstream (`VULKAN-174`), and seven Vulkan example tests make it; they have only ever
run in Debug trees. A validation-count assertion should skip in a build with no validation rather
than fail — recorded for the test-infrastructure work, not fixed here.

---

## VKPAR-0013 — What is still red, and what it will take

Fifty-one `Vulkan_*` tests still fail, plus a handful of shared-suite cases. **None is unexplained
and none is new**; every one was failing on the baseline too, and twelve of them could not even be
seen before `VKPAR-0006`. Classified by what would have to change:

| Class | Count | Examples |
|---|---|---|
| **Renderer defect — texture/cube/volume transfer** | ~11 | `CubeVolume_{Get,Set}DataContract`, `Texture3D_Mip_Layout`, `Texture3DAddressW`, `DxtTextureCube`, `Dxt1FromStream`, `TextureFilterMipContract`, `InvalidMipLevel`, plus the two content-reader cases that fail inside `TextureCube::GetData` and `Texture3D::SetData` |
| **Renderer defect — render targets / MRT** | ~9 | `MRT_MixedFormats`, `MRT_MsaaResolve`, `MrtMipFinalization`, `RenderTargetCube_{DepthFormat,PluralMRT}`, `RenderTarget_DepthStencilUsage`, `FloatRenderTarget`, `BoundMsaaReadback` |
| **Renderer defect — format capability** | ~6 | `SurfaceFormat{_Throws,Classification}`, `FormatLimitQueries`, `CapabilityContract`, `ProfileLimitsAudit`, `AdapterQueryContract` |
| **Renderer defect — channel expansion** | 3 | `ClassicTextureFormat.*`. Diagnosed: Vulkan sets `VK_COMPONENT_SWIZZLE_IDENTITY` on every view and never expands a missing channel, so `NormalizedByte2` samples blue 0 where XNA's D3D9 one-/two-channel rule says 255. `plan_vulkan.md` **F-11 predicted exactly this**, and `VULKAN-174` then pinned blue = 0 in the renderer's own test, so the two suites now contradict each other by construction. Fixing it means the swizzles **and** retiring that pin |
| **Renderer defect — other** | ~14 | SpriteBatch/SpriteFont, resource lifetime, clear/present ordering, occlusion queries, `ShaderEffect` binding, draw/layout validation |
| **Public-layer defect, not Vulkan** | 2 | `InstancedDrawMultiStreamTest.DuplicateSemanticStreamsRemapToUnusedIndices` and `OrdinaryDrawBindingOffsetTest.MultipleStreamsUseOnlyTheGeometryStreamsOwnOffset` both throw *"The vertex declaration contains a duplicate usage and usage index"* from `VertexDeclaration`, before any renderer is reached |
| **Environment, not CNA** | 3 | `UnicodeContentRootTest` / `UnicodeTree` — content path resolution, no renderer involved |
| **Stress** | 1 | `Vulkan_DynamicBufferStress` times out |

`Vulkan_RenderTarget_BlendFactor` and `Vulkan_Swapchain_Sync` pass now and are **not** claimed by
either fix in this branch; they are recorded as passing without an attributed cause rather than
counted as wins.

---

## Classic closeout

The step after the parity merge: close what `VKPAR-0013` classified — texture/cube/volume transfers,
render targets and MRT, format capability — and stop around 340–350 of the ~370 `Vulkan_*` tests
rather than chase the tail. Branch `vulkan-classic-closeout`, from `next` at `33105f3ae`. Every run
below is **RADV-HW** through `tools/platform/run_gpu_tests_private.sh` (private Weston + rootful
Xwayland, DRI3), never the live desktop.

The fifty-one failures were re-run first, unchanged, to have the real messages rather than the
classification's guesses (`VKPAR-0013` was written from test names). **Twenty-three of them were not
renderer defects at all.**

### VKPAR-0018 — 23 more tests that died on the Reach profile

The same defect `VKPAR-0006` repaired in 103 tests, in the ones it did not reach: each uses a
HiDef-only feature — `GetBackBufferData` (9), volume textures (6), separate alpha blending (2), more
than one render target (2), mipmapped non-power-of-two surfaces, an occlusion query, float targets —
while `GraphicsDeviceManager` defaults to Reach, which CNA enforces (`SOFTWARE-213`). They threw
before reaching their subject, and `VKPAR-0013` had filed several of them as transfer or
render-target defects because of what they were *named*.

`VKPAR-0006` missed them because they are not Vulkan's own sources. Nine are shared sources
compiled for Vulkan from `modules/graphics/examples/` and `modules/renderers/sdl-renderer/examples/`,
and four carry a per-renderer table whose Vulkan row said `wantHiDefProfile = false`. The repair is
the same one — request HiDef — applied three ways:

| Shape | Tests | Change |
|---|---|---|
| Per-renderer table | `Backbuffer_PassOrder`, `GraphicsDevice_OrderedClear`, `CubeVolume_{Get,Set}DataContract` | the Vulkan row's `wantHiDefProfile` false → true (other renderers' rows untouched) |
| Shared source, manager in the constructor | 8, e.g. `BasicEffect_DiffuseColorClamp`, `InstancedTexturedDraw`, `InvalidMipLevel`, `Texture2D_FromStream` | `setGraphicsProfileProperty(HiDef)` after the manager is created |
| Shared source, device requested in `main` | `SkinnedEffect_BoneDeformation` | `SetGraphicsProfileEXT(HiDef)` before `Run()`, the shape `alpha_test_integration_test.cpp` already uses |
| Vulkan's own | 10: `MRT_MixedFormats`, `MrtMipFinalization`, `OcclusionQuery_Precision`, `ShaderEffect_{BoundTexture,PerUnitSampler}`, `SpriteBatch_BlendState`, `Texture3DAddressW`, `Texture3D_Mip_Layout`, `CapabilityContract`, `FloatRenderTarget` | as the constructor shape |

Requesting HiDef is safe on every renderer these shared sources compile for: only DirectX9
implements `isProfileSupported`, and every other `GraphicsAdapter::IsProfileSupported` answers true.

**Result (RADV-HW): 19 of 23 pass.** The other four now reach their subject and fail on it — which is
what they were for:

| Test | What it reaches now | Taken up in |
|---|---|---|
| `SpriteBatch_BlendState` | 18/23; the five constant-factor legs (`Blend.BlendFactor`) draw white instead of the constant colour | a renderer row below |
| `MRT_MixedFormats` | its premise: it expects `RenderTarget2D(Bgr565)` to throw, which has not been true since `SOFTWARE-216` restored XNA's fall-back-to-`Color` | the render-target format row below |
| `MrtMipFinalization` | `Texture2D::GetData: total data size does not match the requested region` | the transfer row below |
| `FloatRenderTarget` | `SetRenderTargets: render targets must have matching pixel sizes` | the render-target row below |

### VKPAR-0019 — The white constant factor is XNA's answer

`SpriteBatch_BlendState` S12/S13 draw with a `Blend.BlendFactor` source and expected the colour of a
`GraphicsDevice.BlendFactor` set **between** a Deferred `Begin()` and `End()`. RADV drew white.

That is what Microsoft XNA draws too. `SOFTWARE-350` measured it: `BlendState.Apply` writes the
state's own `BlendFactor` (White by default), and assigning `GraphicsDevice.BlendFactor` dirties the
cached state so the next `BlendState` assignment reaches `Apply` again. A Deferred batch assigns its
state at `End()`, so the factor set before that was overwritten by White before anything was drawn.
The test (`GFX-091`, older than `SOFTWARE-350`) asserted the pre-measurement model, and on Vulkan the
public layer had since become right underneath it.

The scene's subject — one static constant-factor state, a different dynamic constant per batch,
A→B→A — is kept by drawing those batches **Immediate**: `Begin()` applies the state, and the factor
set after it is the one the draw uses, exactly as in XNA. **RADV-HW: 23/23**, including the A→B→A
and static/dynamic/static pipeline transitions, so the renderer's dynamic blend constant was never
the problem.

### VKPAR-0020 — The eleven HiDef texture formats, and the render-target fallback

**The renderer.** Vulkan's `Texture2D` storage table held Reach's nine formats and nothing else, so
every HiDef-only format — `Rgba1010102`, `Rg32`, `Rgba64`, `Alpha8`, `Single`, `Vector2`,
`Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable` — was deferred to the
framework rule and refused as *"SurfaceFormat 13 is not implemented by the selected graphics
renderer"*. The shared `SurfaceFormat_Throws`, `BoundResourceDispose` and `MoveSemantics` tests
construct a `Single` texture at HiDef and died on that line.

All eleven are core Vulkan 1.0 formats; `ClassifySurfaceFormatEXT` still asks the device for each.
The layouts are D3D9's, low bits first, and the table comment carries the field-for-field reasoning
(`A2B10G10R10_UNORM_PACK32`, `R16G16[B16A16]_UNORM`, `R8_UNORM`, the plain float formats;
`HdrBlendable` is `HalfVector4`'s storage, as in XNA). The one- and two-channel formats get the
**measured** channel expansion of `VKPAR-0014` on their sampled view — `(r,1,1,1)` for `Single` and
`HalfSingle`, `(r,g,1,1)` for the two-channel formats, `(0,0,0,a)` for `Alpha8` — and so do the
sampled views of float render targets (2D and cube). The swizzle now also checks the VkFormat
actually stored, because a cube kept every non-block format as RGBA8 at the time, and expanding
"missing" channels of four-channel storage would have discarded data.

**The render-target tests.** Four Vulkan tests still asserted the model XNA does not have: that
`RenderTarget2D(…, Bgr565, …)` **throws**. `SOFTWARE-216` restored XNA's rule — the constructor
asks `QueryRenderTargetFormat` and builds the selected format, falling back to `Color` — and this
renderer stores no packed 16-bit target, so it falls back. Updated to that rule, each keeping its
subject:

| Test | Was | Now |
|---|---|---|
| `SurfaceFormatClassification` | RT legs K/N expected refusals; J ignored the profile; everything ran at Reach only | construction never refuses and reports the exact format when the public query says yes, `Color` otherwise; J includes the profile; **both sweeps run at Reach and HiDef**, so the new storage is constructed rather than only classified |
| `AdapterQueryContract` | leg A, the "control", expected a throw | the device builds `Bgr565` as `Color`, and leg B checks the adapter names that same format — device and adapter both at HiDef |
| `FormatLimitQueries` | an unadvertised base/mip/MSAA request had to refuse | it constructs as the nearest target (`Color`, or fewer samples) and never with the identity it could not have; runs at HiDef, where the snapshot's float targets are legal |
| `MRT_MixedFormats` | "mixed-format MRT is unreachable" because `Bgr565` threw | a `Bgr565` request binds beside `Color` as `Color`; and a genuinely mixed pair, `Color` + `Single` (two VkFormats in one pass), clears and each target reads back its own representation (bytes; `128/255` as a float) |

**Evidence (RADV-HW, private compositor).** Full `cmake-build-vulkan` suite after the renderer
change: `SurfaceFormat_Throws`, `BoundResourceDispose`, `MoveSemantics` pass. The four updated
tests pass. `ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` and
`EveryPromotedFormatPreservesFullPartialAndMipBytesExactly` **returned early on Vulkan for every
format it did not claim**; they now execute the eleven formats' sprite samples and byte round trips
and pass — the measured `Alpha8|0,0,0,128` and `Single|64,255,255,255` included.

The same full run found the change's one casualty: four float **cube** tests that had passed or
skipped only because the cube was refused now constructed and failed. That is `VKPAR-0021`.

### VKPAR-0021 — Cube transfers in the cube's own format

The shared layer moves a non-`Color` uncompressed cube's texels through
`ITextureCubeRenderer::SetDataBytesEXT`/`GetDataBytesEXT`, as exact declared-format bytes. Vulkan
never implemented either — the defaults refuse — and allocated every non-block cube as RGBA8, so
**no** packed, integer, half or float cube could hold its data: `ClassicTextureFormat.NormalizedIntegerCube*`
and `TextureCubeTest.{ByteTransfers…,GenericValueType…}` were red on the baseline, and the float
cube tests passed only because the cube was refused and they returned early (`VKPAR-0020` exposed
that). Compressed cubes kept their blocks in a CPU shadow but offered no exact block readback, so
the four `TextureCubeTest.SetDataCompressed*` tests failed on `GetData(byte*)`.

* The cube allocates every format the storage table holds in that table's VkFormat, with its real
  texel size; the swizzle of `VKPAR-0020` applies to it for that reason.
* `SetDataBytesEXT`/`GetDataBytesEXT` store and read exact bytes through the same per-face staging
  copy as the RGBA8 route, which is now one helper pair. The RGBA8 `SetData`/`GetData` refuse a
  cube stored in anything else rather than writing four-byte texels into it.
* `GetCompressedDataEXT` returns the exact blocks of a block-aligned region from the shadow
  `SetCompressedDataEXT` already kept.
* `ClassifyTextureCubeFormatEXT` answers as the 2D table does, except that `NormalizedByte2/4` are
  `Unsupported` for a cube: XNA forbids them there at both profiles, and a renderer should not claim
  them whatever its `Texture2D` stores.

**Evidence (RADV-HW, private compositor).** Every cube-related test in the build
(`TextureCube|ClassicTextureFormat|Cube|EnvironmentMap|EnvMap`, 275): all pass except
`Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder` (a **volume** test, untouched here)
and four Vulkan examples that assert pre-`SOFTWARE-2xx` contracts (their own row). Newly passing:
`ClassicTextureFormat.NormalizedIntegerCube{FormatsPreserveExactTransfers,FormatsFeedEnvironmentMapSampling}`,
the four float cube tests (now real), `TextureCubeTest.{ByteTransfersUseByteCounts…,GenericValueTypeRoundTripsAFaceMipRectangle…,ScalarFloatElementsSpanOneVector4CubeTexel,ColorElementsSpanOneVector4CubeTexelWithoutConversion}`
and the four `SetDataCompressed*`. Vulkan also joins the renderer allowlists of three audited cube
contracts it now meets, and passes them: `PlainCubeCapabilityDoesNotInheritTexture2DFormatClaims`
and the content readers' `TextureCubeReaderPreservesEveryClassic{Uncompressed,Compressed}Format…`.

### VKPAR-0024 — `Depth24` on RADV was a stencil format

RADV offers neither `X8_D24_UNORM_PACK32` nor `D24_UNORM_S8_UINT`. `PickDepthFormat` therefore
fell from a `Depth24` request straight to the combined candidates and chose `D32_SFLOAT_S8_UINT`,
which `XnaDepthFormatFromVkFormatEXT` truthfully reports as `Depth24Stencil8`. A game asking for
depth without stencil got a stencil plane, and with it a legal `Clear(ClearOptions.Stencil)` where
XNA throws (`SOFTWARE-333`): `RenderTarget_DepthStencilUsage` X1 failed exactly there, and only on
RADV — `llvmpipe` offers `X8_D24`.

`Depth24` now tries `D32_SFLOAT` after `X8_D24` — still no stencil, and Vulkan guarantees one of
the two as a depth attachment; FNA3D takes the same step. The test instrument that forces the
preferred formats away (`SetDepthFormatPreferredUnsupportedForTestEXT`) blocks `D32_SFLOAT` too,
so `vulkan_applied_formats_test` still sees the visible substitution it exists to check.

**Evidence (RADV-HW, private compositor).** Every `Depth|Stencil|Clear|AppliedFormats` test (229):
all pass but `RenderTargetCube_DepthFormat`, a stale test of the next row. `RenderTarget_DepthStencilUsage`
passes, and `GraphicsDevice_ClearOptions`' Depth24 suite now sees no stencil plane — which is what
lets that test's missing-plane expectation (`VKPAR-0022`) hold on this hardware.

### VKPAR-0022 — Ten tests older than the rules they break

Between 2026-09-09 and 09-11 the `SOFTWARE-2xx/3xx` rows recovered a run of Microsoft XNA rules from
its IL and made the shared layer follow them. These ten Vulkan-registered tests were written before
that and still asserted FNA's behaviour or the older CNA one; on Vulkan the public layer had become
right underneath them. Each was checked against the rule's own row and its code, and changed in the
smallest way that keeps its subject:

| Test | XNA rule it met | Change |
|---|---|---|
| `DeclaredEffectLayout` | overlapping declaration elements are refused (`SOFTWARE-205`) | the moved layout **swaps** UV and normal instead of laying the UV over the normal; a stride guess still reads the wrong half |
| `Viewport_Subregion` | a viewport must fit the active surface (`SOFTWARE-226`) | the round-trip viewport fits the 64×64 back buffer |
| `GraphicsDevice_ClearOptions` | bound state is immutable (`SOFTWARE-232`); `Clear(Color)` clears depth to **1.0**, not `Viewport.MaxDepth` (`SOFTWARE-334`); clearing a missing plane throws (`SOFTWARE-333`) | a copied state for the reject leg; the 1.0 expectation (the restricted viewport's `MaxDepth` of 0.73 now separates XNA from FNA); the baseline stamp clears only existing planes, and a case naming a missing plane expects the `InvalidOperationException` and an untouched surface |
| `SkinnedEffect_VertexColor` | bound state is immutable (`SOFTWARE-232`) | leg (e) uses a copy of the bound constant-factor state |
| `PipelineKeyStateCoverage` | Min/Max need One/One factors (`SOFTWARE-212`) | the alpha-function key field is changed to `Subtract` |
| `IndexBuffer_UploadBounds` | an index transfer is a byte span; past the end is `InvalidOperationException` (`SOFTWARE-250`) | C and D catch that type |
| `ZeroLengthBuffers` (shared) | a zero count is refused (`SOFTWARE-204`) | the empty buffers are expected to be refused by name before any allocation, the device still drawing; HiDef for its probe |
| `SpriteBatch_SortModeSemantics` (shared) | .NET 4's unstable quicksort swaps two equal keys (`SOFTWARE-354`) | C2 expects the first-issued sprite on top |
| `SpriteFont_Properties` (shared) | the content constructor stores an absent default character; only the public setter validates (`SOFTWARE-353`) | the constructor stores it, the setter refuses |
| `ProfileLimitsAudit` | Reach has no volume textures (`SOFTWARE-179`) | HiDef; G and H are now answered by HiDef's own ceilings |

**Evidence (RADV-HW, private compositor): all ten pass.** `GraphicsDevice_ClearOptions` passes
with `VKPAR-0024`, which stopped RADV's `Depth24` target from carrying a stencil plane.
Three of the sources (`zero_length_buffer_test.cpp`, `spritebatch_sort_mode_semantics_test.cpp`,
`sprite_font_test.cpp`) are shared with EasyGL registrations that this build does not configure
(`VKPAR-0016`); they were not run on EasyGL here.

### VKPAR-0025 — Ten transfer and render-target tests older than their rules

`VKPAR-0013` filed these as renderer defects in the transfer and render-target classes, by name.
Run for their real messages, **every one** threw from the shared layer, enforcing an XNA rule a
`SOFTWARE-2xx` row had recovered after the test was written. None reached the renderer. Each was
changed in the smallest way that keeps its subject:

| Test | XNA rule | Change |
|---|---|---|
| `CubeFaceReadbackDependency` | a transfer's `elementCount` is the region's exact size (`SOFTWARE-277`); two targets need HiDef | exact counts (the sentinel suffix still guards the rest); HiDef |
| `MrtMipFinalization` | the same exact size; an invalid level is `InvalidOperationException` (`SOFTWARE-280`), not `std::out_of_range` | exact counts; that exception type |
| `DxtTextureCube` (shared) | a DXT cube transfers **bytes** only (`SOFTWARE-277`); image streams must seek (`SOFTWARE-305`); DDS is the named extension (`SOFTWARE-306`) | reads the exact blocks back (and the partially replaced face as red, red, red, blue) — sampling still checks the decoded colour; a seekable stream; `DDSFromStreamEXT` |
| `Dxt1FromStream` (shared) | image streams must seek (`SOFTWARE-305`); `GetBackBufferData` is HiDef | a seekable stream; HiDef |
| `ResourceOutlivesDevice` | Reach has no volume textures (`SOFTWARE-179`) | the bare device requests HiDef, so leg A has a volume to outlive it |
| `BoundMsaaReadback` | Set/GetData on an active render target throws (`SOFTWARE-246`) | B: the bound read is refused and writes nothing; C: the unbound read returns the resolved draw — EasyGL's GFX-164 test took the same shape |
| `MRT_MsaaResolve` | MRT mismatches are `ArgumentException` (`SOFTWARE-220`), not `std::runtime_error` | that type |
| `RenderTargetCube_PluralMRT` | MRT compares resources by identity, so two faces of one cube may not be bound together (`SOFTWARE-220`) | the per-slot face legs use faces of two cubes; the same-cube pair is a rejection leg; `ArgumentException` |
| `RenderTargetCube_DepthFormat` (shared) | clearing a missing plane throws (`SOFTWARE-333`) | `Clear(Color)`, which clears the planes the face has |
| `FloatRenderTarget` | MRT requires equal **pixel size**, not equal format (`SOFTWARE-220`); a target format is a preference (`SOFTWARE-216`) | H: `Vector4` beside `Color` is refused, `Single` beside `Color` clears both (4.0 in the float, white in the bytes); I: a `Dxt1` request is built and reported as `Color` |

**Evidence (RADV-HW, private compositor): all ten pass**, `FloatRenderTarget`'s odd-sized
`HalfVector4` MSAA and mip-chain legs (J, K) included — they were never reached before. Three
sources are shared with EasyGL registrations this build does not configure and were not run there.

---

## Future modern-Vulkan work — inventory only, nothing implemented

Recorded because the brief asks, and deliberately not acted on:

* **Reusable as-is.** The `PipelineKey`/`PipelineKeyHash` cache, the growing descriptor-pool
  allocator (`AllocateFromGrowingPoolEXT`, which chains rather than refusing), the retirement queue
  (`RetiredResources`, `(pool, set)` pairs), `VulkanStorageBufferRenderer` /
  `VulkanComputeShaderRenderer` / `VulkanTexture2DArrayRenderer` / `VulkanStorageTexture2DRenderer`
  / `VulkanGpuTimerRenderer`, the debug-utils messenger with its message-id classification, and the
  opt-in synchronization-validation plumbing.
* **Missing for a modern API.** A public command-buffer or explicit-barrier surface; a descriptor
  model that is not per-draw set construction; SPIR-V that is authored rather than preprocessed
  (`ShaderEffect` on Vulkan takes SPIR-V words, not the GLSL the CNAEXT engine layer writes — see
  `docs/cnaext-engine-layer.md`); and a real allocator, since today every resource takes its own
  `vkAllocateMemory`.
* **Blocking first.** The texture-transfer and render-target rows above. A modern layer built on
  them would inherit them.

---

## Scope boundaries

Carried from the brief and not re-litigated per row:

* **No modern CNAEXT graphics API.** What Vulkan infrastructure that future workstream can reuse is
  recorded at the end of this file; none of it is implemented here.
* **No performance work.** Pathological behaviour is reported, not optimized. No benchmarking
  against DirectX11/12 while parity is still moving.
* **No other renderer project** (WebGPU, OpenGL4, SDL_GPU, Metal). EasyGL and Software are touched
  only as reference renderers and for regression.
* **Evidence outranks convenience.** A RADV or Mesa behaviour, a stale fixture, a harness bug and
  undefined Vulkan behaviour are each named as such rather than absorbed into CNA.
