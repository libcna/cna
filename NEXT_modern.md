# NEXT_modern.md — running ledger for the CNAEXT engine layer

Continuity file for [`plan_modern.md`](plan_modern.md) (the `MOD-*` backlog implementing
[`CNAEXT.md`](CNAEXT.md)). Same role `NEXT_skia.md` has for the Skia renderer: read this first,
do not reconstruct the layer's state from the general `NEXT.md`.

---

## 1. Where the work stands

**Phase 0 (foundation) — in progress.** Nothing of the orchestration layer
(`RenderPipeline`, post-process passes, shadows, skybox/IBL, compute) exists yet; see
`docs/cnaext-engine-layer.md` for the honest capability boundary.

| Done | Task |
|---|---|
| ✅ | `MOD-1` — `CNA/Graphics/CNAEXT.hpp` master include + `CnaExtMasterIncludeTests` |
| ✅ | `MOD-2` — `cnaext` CMake configure/build preset |
| ✅ | `MOD-3` — `scripts/check_cnaext_guards.sh` |
| ✅ | `MOD-4` — `docs/cnaext-engine-layer.md` |
| ✅ | `MOD-14` — this file |
| ✅ | `MOD-100`/`101`/`103`/`104` — float render-target capabilities, derived from a per-format renderer verdict |
| ✅ | `MOD-115`/`116`/`117` — EasyGL really allocates RGBA16F/RGBA32F targets, probed at runtime |
| ✅ | `MOD-105`, `MOD-108`/`124`, `MOD-131` — unsupported formats refused, format-aware readback, and the end-to-end proof that values above 1.0 survive |
| ⛔ | `MOD-106` — lenient-substitution opt-out, dropped (it would preserve a behaviour that never existed) |
| ✅ | `MOD-107` — `RenderTargetCube` carries its `SurfaceFormat` to the renderer too (the path IBL needs) |
| ✅ | `MOD-118`–`MOD-123`, `MOD-125` — completeness diagnostic, depth/MSAA/mip/MRT on float targets, half-float filtering query |
| ✅ | `MOD-21`/`MOD-22` — `Uncharted2` and the settings fields the passes read |
| ✅ | Phase 2 core — `PostProcessContext`, `FullscreenPass`, `PostProcessPass`, `RenderTargetPool`, `BlitPass`, `PostProcessChain` (`MOD-16`, `19`, `200`–`208`, `225`–`228`) |
| ✅ | Phase 3 — `TonemapPass`, all five operators, shader verified against a CPU reference (`MOD-300`–`313`, `317`) |
| ✅ | Phase 7 core — `RenderPipeline`, the consumer `RenderPipelineSettings` never had (`MOD-700`–`737`) |
| ✅ | Phase 4 — `BloomPass`, wired ahead of tonemapping (`MOD-400`–`418`) |
| ✅ | Phase 6 — `FxaaPass`, wired after tonemapping (`MOD-600`–`606`) |
| ✅ | Phase 5 — `SsaoPass` and its pipeline wiring (`MOD-505`/`506`/`515`–`524`, `MOD-711`); depth and normals are caller-supplied |
| ✅ | **Phase 8 — directional shadows, complete end to end** (`MOD-800`–`861`, all but `MOD-854`) |
| ✅ | **Phase 9 — cascaded shadow maps, complete** (`MOD-900`–`917`, 18/18) |
| ✅ | **Phase 10 — point and spot shadows, complete** (`MOD-1000`–`1012`, 13/13) |
| ✅ | **Phase 11 — skybox, complete** (`MOD-1100`–`1116`, 17/17) |
| ✅ | **Phase 12 — image-based lighting, complete** (`MOD-1200`–`1248`; 4 rows ⛔ with reasons, 1 🟨) |
| ✅ | **Phase 13 — material system reconciliation, complete** (`MOD-1300`–`1315`, 16/16) |
| ✅ | **Phase 14 — instancing, LOD and culling, complete** (`MOD-1400`–`1414`, 15/15) |
| ✅ | **Phase 15 — compute shaders and storage buffers, complete** (`MOD-1500`–`1555`; 2 rows 🟨, 1 ⛔ with its measurement) |

**The HDR spine is complete and verified end to end.** A game can wrap its draw calls in
`RenderPipeline::begin`/`end`, enable HDR, bloom, a tonemapping operator and FXAA, and get them --
on EasyGL, in software, under Xvfb. With everything off it allocates nothing and renders exactly
what it would have rendered without a pipeline.

**Every post-process subsystem in the plan now exists and is wired into the pipeline**: SSAO,
bloom, tonemapping and FXAA, in that fixed order, each with a reason for its position.

**Shadows are visible.** `ShadowMap` generates the map (rigid and skinned casters), the four lit
effects carry the state through `IShadowReceiverEXT`, and every lit EasyGL program samples it
through a shared 3x3/5x5 PCF snippet. `RenderPipeline::setShadowScene` runs the pass at the top of
`begin()`, before the scene target is bound. The pieces worth remembering:

- The map holds **light-space distance, not depth** — CNA cannot sample a depth attachment as a
  texture on every renderer. `Single` (R32F) where the renderer has one, `Color` otherwise.
- **No V flip** when sampling it, unlike the XNA sample `easygl_shadowmapping_*` ports: a CNA
  render target's texel memory already matches the clip space it was rendered in. Pinned by moving
  the caster off centre along each axis and comparing the shadow centroid against the camera
  matrices alone (`ShadowVisibilityTest.TheShadowLandsWhereTheCasterIs`).
- Shadow attenuates **direct light only** — never ambient, never PBR's IBL term.
- A receiving draw is forced onto the **per-pixel** program whatever `PreferPerPixelLighting` says;
  a shadow evaluated at four corners and interpolated is a gradient, not a shadow.
- **Bias evidence** (`ShadowVisibilityTest.TheDefaultBiasSitsBetweenAcneAndPeterPanning`, printed):
  self-shadowed area 0.549 at bias 0, 0.093 at the default 0.0015, 0.000 at 0.2.
- **Shadow pass cost** (`cnaext_shadowmap_test --benchmark`, 12 casting triangles, Mesa llvmpipe):
  Low 0.10 ms, Medium 0.12 ms, High 0.20 ms, Ultra 0.52 ms. Independent of screen resolution.
- `MOD-854` (a skinned character self-shadowing golden) is **deliberately deferred**: a skinned quad
  with one identity bone proves the shader path, not self-shadowing, which needs a real animated
  mesh from the glTF fixtures.

**Cascades work too.** `CascadedShadowMap` splits the camera frustum 2-4 ways, fits each slice
sphere-based (rotation-stable) and texel-snapped (translation-stable), stores them in **one atlas**
rather than a texture array, and the same shared shader path samples them. `applyToReceiver` hands
an effect everything at once, because these values are only meaningful together. Facts worth
keeping:

- **XNA projection matrices are the Direct3D ones, so NDC z runs 0..1**, not -1..1. Assuming GL
  there put the "near" frustum corners half way to the camera.
- A cascade atlas must be allocated `RenderTargetUsage::PreserveContents`. With the default,
  binding it for cascade 1 discards cascade 0, and the finished atlas holds only the last cascade.
- Per-cascade frustum corners scale the **near** corners by `depth/near`. Scaling the far corners
  applies that ratio twice and fits each cascade to a volume tens of times too large -- every
  cascade then comes out covered edge to edge by the first caster.
- The PCF texel step is a **vec2**: an atlas is N times wider than tall.
- **Cost** (`cnaext_csm_test --benchmark`, 6 casting triangles, Mesa llvmpipe): single Medium map
  0.12 ms, 2 cascades 0.20, 3 cascades 0.49, 4 cascades 0.43 per frame.

**Punctual lights work too**, and they needed more than a shadow: XNA's lit effects carry three
*directional* lights and nothing else, so a point light's shadow had nothing to attenuate. The four
lit effects therefore gained a punctual lighting term (`PunctualLightEXT`, always compiled) and its
cube/spot lookup. Facts worth keeping:

- Both punctual maps store **distance from the light over its range**, not projected depth. A cube
  face's projected depth is defined by that face's own projection; distance is face-independent, so
  the receiver samples the cube by direction and compares directly. The range used to light must be
  the range the map was generated with.
- A cube render target needs `PreserveContents`, same trap as the cascade atlas.
- The cube face size is **capped at 1024** whatever the quality asks: six faces at 4096 is a hundred
  million texels for one light.
- The spot PCF needs **its own texel size**. Borrowing the directional map's meant a draw with no
  sun attached filtered with a texel of 1.0, clamped every tap to a corner, and produced a spot
  shadow that silently never appeared.
- **Cost** (`cnaext_pointshadow_test --benchmark`, 2 casting triangles, Mesa llvmpipe): directional
  1 map 0.05 ms, spot 1 map 0.04 ms, **point 6 faces 5.42 ms** -- a hundred times a single map, not
  six, because each face rebinds a cube attachment and clears it. That ratio is why point shadows
  are opt-in per light.

**One earlier defect fixed on the way** (`MOD-520`): SSAO produced *no* occlusion at all on this
container's Mesa build, at every radius, silently. The occlusion test offset the comparison depth by
the sample's z times `uRadius` -- a view-space formulation in a pass that has no view space, where
`uRadius` is a UV offset and the depths are a normalized texture. The offset swamped the difference
it was compared against. Only a narrow band of radii made both terms work at once, which is why it
had passed before. `AHigherIntensityDarkensMore` used `EXPECT_LE` and so passed throughout; it is
strict now.

**The sky and the environment are in.** `Skybox` draws an environment cube in one fullscreen pass;
`EnvironmentProcessor` converts an equirectangular panorama to a cube and convolves that cube into
the three split-sum products; `ImageBasedLightEXT` carries them to `PbrEffect`/`SkinnedPbrEffect`,
and EasyGL's two PBR programs light with them. Four decisions worth remembering:

- **The precompute is CPU-side.** A render-to-cube version needs float render targets, cube render
  targets and custom effects present *at once*, which no renderer in the committed scope offers
  together. On the CPU the *arithmetic* needs no capability gate and is seamless by construction
  (the sampler picks the face from the direction). It costs 6.2 s in a Debug build for a full set —
  load-time work, stated plainly in the docs rather than hidden. **Corrected in Phase 16.6:** this
  entry used to say it "works everywhere including Headless". It does not. The results still have to
  be written into a `TextureCube`, and Headless and Stub accept that call and store nothing, so the
  generators throw there. Measuring beat assuming.
- **`ImageBasedLightEXT` is in the XNA namespace**, not `CNA::Graphics` — an always-compiled effect
  surface cannot include a `CNA_CNAEXT`-only header. This overrides §OQ-4's recorded answer; see
  `MOD-1222` for why the third option beat both listed ones.
- **Flat ambient and IBL are exclusive.** `FillGpuDrawParams` zeroes `ambientColor` when a valid
  bundle is bound, so a renderer that ignores the IBL group renders an unlit ambient rather than a
  double-counted one.
- **Everything is 8-bit.** `Texture::ValidateFormat` admits `SurfaceFormat::Color` only, so the
  BRDF table is quantised and an environment brighter than 1.0 carries its brightness in
  `Intensity` instead of in its texels. `MOD-1208`'s two-format plan had nothing to choose between.

The white furnace (half intensity, albedo 1, exact = 128/255) measures 159/139/129/155 across
roughness 0.1/0.4/0.7/1.0 — a small energy gain at both ends, near-exact in the middle. Per-frame
cost is 0.064 ms flat-ambient against 0.066 ms image-based.

**`PbrMaterial` is now a lossless description of `PbrEffect`**, which it was not: it described a
subset and dropped the rest, so it could not be the serialization form it was meant to be.
`applyMaterial` / `extractMaterial` round-trip every field exactly (asserted field by field, and
across all 256 values of the one field that changes representation), `applyMaterialState` applies
the blending and culling a material implies, and `materialFromGltfEXT` builds one from the glTF
importer's own decoded record. What is worth remembering:

- **Emissive became a `Vector3` and alpha coverage became `AlphaModeEXT`** — the existing XNA-layer
  enumeration, not a new `CNA::Graphics::AlphaMode`; a second enum would have met the first in a
  conversion function nobody could delete.
- **Defaults moved to glTF's** (metallic 1, roughness 1, from 0 and 0.5) so that applying a default
  material to a default effect is genuinely a no-op — which is now asserted.
- **The glTF bridge is a template over a concept**, so `graphics-ext` links neither the content
  module nor `cgltf`. Both sides test it: a stand-in source here, the importer's real `MaterialOut`
  there, with a `static_assert` pinning the agreement.
- **The C ABI was not broken.** `docs/c-api/ABI_VERSIONING.md` forbids changing an existing name's
  meaning within a major, so `CNA_PbrMaterial` is frozen exactly as published and the current shape
  arrived as `CNA_PbrMaterialEXT` + `cna_pbr_material_ext_init`. The recorded baseline reports only
  additions, which the policy permits.
- **One value quantises**: glTF's float `baseColorFactor` becomes the material's 8-bit albedo. The
  two draw paths are otherwise identical, asserted at ≤1/255. Making the albedo factor a `Vector4`
  would close it and is an owner decision, because the C mirror is `Color`-shaped too.

**Ten thousand objects now cost one draw call.** `InstancedRendererEXT` owns the per-instance
transform stream (four `Vector4`s at `TextureCoordinate` 1-4, the layout the renderers already
bind to the stock shaders), `FrustumCullerEXT` removes what the camera cannot see before any of it
is uploaded, and `LodGroupEXT` picks a level by distance or by projected pixel size. Measured on
llvmpipe: 1 000 cubes instanced 0.96 ms against 51.5 ms looped (54x), 10 000 cubes 22.7 ms against
538 ms (24x) -- and the culled frame is pixel-identical to the unculled one.

Two decisions worth remembering: the per-instance fallback is **opt-in**, because one draw call per
instance is a different program rather than a slower one; and `LodGroupEXT` orders its levels
finest first, which is ascending distance in one mode and *descending* pixel size in the other, so
changing mode re-sorts.

**Compute is real, and it runs here.** `IComputeShaderRenderer`/`IStorageBufferRenderer` sit
beside the other renderer interfaces with inert defaults, EasyGL implements them against the
*runtime* context version, and `CNA::Graphics::ComputeShader`/`StorageBuffer` are the engine-layer
face. Verified on Mesa llvmpipe's ES 3.2 context, not by inspection: 1024 floats doubled, a 1 MB
buffer round-tripped byte-exact, GPU frustum culling agreeing with `FrustumCullerEXT` on 625 boxes
with zero disagreements, and 100 000 particles integrating at 0.881 ms against 2.401 ms on the CPU
with an exact match.

`AutoExposureEXT` is the first consumer inside the engine layer, and closes `MOD-308`: a log-average
luminance reduction in shared memory, asymmetric adaptation, one line to apply to the pipeline.

Three limits found and written down rather than worked around:

- **Image bindings are desktop-GL only.** GL ES needs an immutable texture (`glTexStorage2D`) and
  CNA allocates mutably, so `ComputeShader::bindImage` refuses with the reason instead of issuing a
  binding the driver drops. Making CNA's textures immutable is a change to the path every draw goes
  through — an owner decision, not a side effect of this phase.
- **A storage buffer cannot be bound as a vertex stream**, so GPU-resident particles come back
  through the CPU at 0.806 ms per 1.6 MB. That number is what a `StorageBuffer`/`VertexBuffer`
  aliasing API would save.
- **`Texture2D::GetData` never shows compute writes** — it answers from the CPU shadow copy.

**The second renderer changed what the layer knows about itself.** Running everything against a
real Vulkan device measured five Phase 16 rows and produced one addition that was not optional:
`GraphicsCapability::CustomEffects` says a renderer can compile *some* custom effect, not that it
takes this layer's shader language — the Vulkan renderer's `ShaderEffect` consumes SPIR-V bytecode
while every pass and caster here hands it GLSL source. Before `MOD-1699` added
`SupportsShadowSamplingEXT()` and `SupportsImageBasedLightingEXT()`, the shadow example on Vulkan
did not fail, it **crashed**: the caster's effect silently failed to compile and the draw went ahead
with no effect applied. Six examples and three test suites now SKIP there with a reason.

The eight Vulkan failures in the row above are pre-existing renderer issues, not engine-layer ones —
two custom-GLSL `Cnj` tests (the same SPIR-V mismatch), one stdout diagnostic, and five
`IndexedDrawDeferred` cases. No Vulkan renderer code was touched this session.

Next: the rest of Phase 16 (the other renderers, and implementing rather than measuring Vulkan),
then what remains of 17-19.

Smaller open rows: `MOD-203` (restore-on-exception around a pass), `MOD-209`/`MOD-210`,
`MOD-405`/`407`/`409`/`413`/`415`–`417` (bloom quality presets, perf, goldens),
`MOD-314`–`316`/`318`–`320` (tonemap goldens, example, docs), `MOD-501`–`504` (a prepass helper —
SSAO takes the textures directly, so this is convenience rather than capability), `MOD-5`, and
Phase 1's `MOD-130`–`MOD-141`.

**Owner decisions in force** (asked 2026-08-17): start with the HDR spine; EasyGL is the
reference renderer with Vulkan and D3D11 as the committed follow-ups and the rest opportunistic;
verify every task in a real `cmake-build-cnaext` build; one task = one commit, pushed as it lands.

---

## 2. Build environment (what a fresh container needs)

A fresh clone cannot configure CNA without these. Recorded here because the first session spent
real time rediscovering them one CMake error at a time.

```bash
# 1. Vendored submodules (non-recursive is sufficient and much faster)
git submodule update --init vendor/googletest third_party/SDL third_party/SDL_image third_party/SDL_mixer

# 2. Sibling checkouts CNA expects next to its own directory (NOT submodules)
cd .. && git clone https://github.com/openeggbert/sharp-runtime.git
        git clone -b develop https://github.com/openeggbert/easy-gl.git
        git clone -b develop https://github.com/openeggbert/meta-gl.git

# 3. System packages (Debian/Ubuntu). SDL3 is built from source at configure time and needs
#    the full X11 set; the FFmpeg headers are required by the media module.
apt-get install -y \
  libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxkbcommon-dev libwayland-dev \
  libdecor-0-dev libxtst-dev libxext-dev libxfixes-dev libxinerama-dev libdrm-dev libgbm-dev \
  libibus-1.0-dev libasound2-dev libpulse-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev ccache

# 4. Configure + build (first configure builds SDL3 into .sdl-prebuilt-<system>-<arch>/, ~minutes;
#    the first CnaTests build is ~1055 targets, so keep it to -j4 in a sandbox)
cmake --preset cnaext
cmake --build --preset cnaext --target CnaTests -j4
SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests --gtest_filter='CnaExt*'
```

Notes:

- `CNA_BUILD_EXAMPLES` is OFF in the preset; turn it on when a task needs the module's demos.
- `ccache` is picked up automatically (`CNA_USE_CCACHE=ON`); without it installed, a build tree
  configured with `CMAKE_CXX_COMPILER_LAUNCHER=ccache` fails with `/bin/sh: 1: ccache: not found`.
- Draco is absent here, so Draco-compressed glTF primitives throw at import time by design.

---

## 3. Test baselines

Recorded so "no regressions" is checkable rather than asserted. Update at each phase boundary
(`MOD-1711`).

| Date | Build | Suite | Result |
|---|---|---|---|
| 2026-08-17 | `cmake-build-cnaext` (OPENGLES3, `CNA_CNAEXT=ON`, Debug), base `origin/next` @ `05a9eab0` | full `CnaTests`, from the repo root, under Xvfb (`MOD-1710`) | **7548 ran · 7484 pass · 64 skip · 0 fail** |
| 2026-08-17 | same, with `MOD-105`/`108`/`124`/`131` applied | same | 7565 ran · 7501 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with `MOD-107` applied | same | 7567 ran · 7503 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, through Phases 2/3/4/6/7 (tonemap, bloom, FXAA, pipeline) | same | 7630 ran · 7566 pass · 64 skip · **0 fail** |
| 2026-08-17 | `cmake-build-debug` — the same branch with **`CNA_CNAEXT=OFF`** (the default) | same | 7544 ran · 7480 pass · 64 skip · **0 fail** |
| 2026-08-17 | `cmake-build-cnaext`, with SSAO added | same | 7640 ran · 7576 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with shadow generation and reception | same | 7659 ran · 7595 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 8 (visible shadows, skinned casters, pipeline integration) | same | 7679 ran · 7615 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 9 (cascaded shadow maps) | same | 7708 ran · 7644 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with all of Phase 10 (point and spot shadows) and the SSAO fix | same | 7730 ran · 7666 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with Phase 11 (skybox) and Phase 12.1 (the IBL precompute) | same | 7763 ran · 7699 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 12 (IBL consumption, shader and tests) | same | 7769 ran · 7705 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 13 (the material reconciliation) | same | 7787 ran · 7723 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 14 (instancing, LOD and culling) | same | 7806 ran · 7742 pass · 64 skip · **0 fail** |
| 2026-08-18 | same, with all of Phase 15 (compute, storage buffers, auto-exposure) | same | 7824 ran · 7758 pass · 66 skip · **0 fail** |
| 2026-08-18 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`** again, after Phases 11-15 (`MOD-1709`) | same | 7556 ran · 7492 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-cnaext`, with the Phase 17/18 verification and documentation work | same | 7829 ran · 7765 pass · 64 skip · **0 fail** |
| 2026-08-18 | **`cmake-build-vulkan`** — the same branch on a real Vulkan device (Mesa lavapipe 1.4) | Xvfb :99 | 7824 ran · 7652 pass · 164 skip · **8 fail**, none of them the engine layer |
| 2026-08-18 | **`cmake-build-multi`** (`CNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB"`), default HEADLESS — Phase 16.6 | same | 7814 ran · 7419 pass · 395 skip · **0 fail** |
| 2026-08-18 | same binary, `CNA_GRAPHICS_RENDERER=SOFTWARE` | same | 7814 ran · 7499 pass · 302 skip · **13 fail**, none in the engine layer |
| 2026-08-18 | same binary, `CNA_GRAPHICS_RENDERER=STUB` | same | 7814 ran · 7213 pass · 572 skip · **29 fail**, none in the engine layer |
| 2026-08-18 | `cmake-build-cnaext` (EasyGL) re-verified after the Phase 16.6 probes and the shader-execution sweep | Xvfb :99 | 7829 ran · 7765 pass · 64 skip · **0 fail** |
| 2026-08-18 | `cmake-build-debug` — **`CNA_CNAEXT=OFF`** re-verified after the same | Xvfb :99 | 7556 ran · 7494 pass · 62 skip · **0 fail** |

The `CNA_CNAEXT=OFF` row is the one that answers "can this break what already works". It configures,
builds and passes with the whole engine layer compiled out. Its lower test count is expected and not
a sign of anything missing: the pre-existing `DepthEffect`, `CRTEffect` and `AsciiPostProcessEffect`
suites are themselves `CNA_CNAEXT`-guarded, so they compile away alongside the new ones. What does
*not* compile away — the capability queries, the float render-target work and the HDR round trip --
runs in both configurations.

That is a clean run, and it is new. Measuring the first baseline on this branch turned up eight
failures and a segfault at ~7300 tests, all of them pre-existing on `next` (verified by rebuilding
the branch without any engine-layer change and getting the identical set). They were fixed on
`next` itself rather than worked around here — `next` @ `05a9eab0`:

- **`PLAT-46`** — a `Game` whose construction throws never ran `~Game`, so its platform stayed
  installed process-wide while the unwind destroyed it. Every later `Keyboard::GetState` /
  `StorageDevice` / `TitleContainer` call read freed memory through the ambient accessor; that was
  the segfault. Fixed with a scope guard that undoes the installation on the failed-construction
  path only.
- **`GLTF-374`** — filling in the renderer inventories for `igl`/`pixijs` exposed that IGL bound an
  opaque-white 1×1 stand-in into the *normal-map* slot, lighting every PBR material without a
  normal map as though its pixels were tilted 55°. It now binds the flat-normal texel.
- **EasyGL diagnostics** — the GL banner and capability dump moved off `std::cout` onto the logger.
- Two `GameEventSemanticsGoldenTest` cases (only reachable once the segfault was gone) now skip
  where the parameterised platform cannot back the build's renderer.

An earlier baseline in this file recorded 6360/6351 — measured on the **develop**-based tree before
the rebase onto `next`, so it never described this branch; replaced above.

Two things about how the suite is run matter more than they look:

- **Run it from the repository root**, not from the build directory. Content/media/audio tests
  resolve fixtures like `tests/assets/xnb/...` relative to the CWD; from `cmake-build-cnaext/` that
  is 116 failures of pure path noise.
- **A real Vulkan build now exists here** (`cmake-build-vulkan`, `-DCNA_CNAEXT=ON
  -DCNA_GRAPHICS_RENDERER=VULKAN`). It needed three packages this container did not have:
  `libvulkan-dev mesa-vulkan-drivers glslang-tools`. Mesa's **lavapipe** provides a software Vulkan
  1.4 device, so the whole suite and every engine-layer example run against a second renderer for
  real. That is what turned Phase 16's Vulkan rows from guesses into measurements — and what found
  the crash described in `MOD-1699`.

- **`CNA_TEST_DISPLAY` is now `:99` in `cmake-build-cnaext`.** It defaulted to `:0`, which does not
  exist here, so every registered engine-layer ctest skipped and the label suites looked green
  without running. Reconfigured with `-DCNA_TEST_DISPLAY=:99`; `ctest -L CnaExt` now runs all nine
  for real. The examples also catch `PlatformException` and exit 77 rather than aborting, so a tree
  configured without a display still *skips* instead of failing.

- **The C API is off in `cmake-build-cnaext`, and turning it on has a catch.** Phase 13 verified
  the C mirror by configuring that tree with `-DCNA_BUILD_C_API=ON`, then turned it back off. The
  reason: `libcna_c_api_static.a`'s generator reads `CMakeFiles/<target>.dir/link.txt`, which only
  the Makefile generator writes, so under Ninja that one target fails every build and takes
  `CApi_InstalledConsumer` with it. Everything else builds and passes. Two other C API gates fail
  here for environment reasons unrelated to any change: `CApiHeaderCompatibility` (this gcc has no
  `-std=c23`) and `CApiCoverageMatrix` (regenerating the inventory on this machine reports 388
  `planned` rows against the checked-in file's 0 — verified on a pristine tree, so the checked-in
  inventory was generated with a different doxygen).

- **Run it with a display.** Without one, every test whose fixture constructs a `GraphicsDevice`
  fails on "No available video device" (~1000 failures). `Xvfb :99 -screen 0 1280x720x24` plus
  `DISPLAY=:99 SDL_VIDEODRIVER=x11` gives Mesa llvmpipe, which reports **OpenGL ES 3.2** to
  EasyGL — enough for the whole Phase 1–7 HDR spine to be verified for real, in software.

```bash
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests
```

`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` segfaulted in an earlier run started
from the build directory (with no media fixtures resolvable); from the repo root it passes. Worth
remembering if a future run aborts mid-suite.

---

### Phase 16.6: what measuring the three no-op renderers actually found

`MOD-1692`/`MOD-1696`/`MOD-1697` were rows saying "same" — an assumption that the engine layer would
construct and pass through on Headless, Software and Stub. Running it produced 21 failures on
Headless alone, and the causes were not capability gaps. They were **limits with no capability to
ask about** and, in one case, an outright wrong promise:

| Renderer | What it will not do | How it fails without a probe |
|---|---|---|
| Headless | read a render target back to the CPU | every test that inspects a pass's output throws |
| Headless, Stub | store `TextureCube` face data | the IBL precompute throws where the docs said it worked |
| Headless | make a cube face the current target — the bind is recorded and ignored | the face-sized viewport that follows is rejected as out of the *back buffer's* bounds |
| Stub | bind a `RenderTarget2D` at all | `RenderPipeline` stops before it renders |
| Software, Headless | run a custom effect's shader source, while reporting `CustomEffects` | the sky renders the placeholder texture's white and calls itself supported |

Four of the five are now probed by `modules/graphics-ext/tests/CNA/Graphics/EngineTestSupport.hpp`,
which asks by *doing* rather than by reading a flag, so a probe cannot drift from the truth. The
fifth is a real API gap and became `ExecutesShaderEffectSourceEXT()`, now consulted by `Skybox` and
all four shadow casters as well as `PostProcessPass`.

Two details are worth keeping:

- **The cube-face probe uses a face larger than the back buffer on purpose.** A 16-pixel cube is
  accepted by a bind that does nothing, because the viewport that follows still fits inside the back
  buffer — the probe would pass and the renderer would draw the shadow map onto the screen. Sizing
  the probe above the back buffer is what makes a fake bind observable.
- **`CubeShadowMap::begin` marked the pass open before binding.** So the first unsupported face threw
  and every later one reported "a face pass is already open" — one refused face turning the object
  into a brick. It now opens the pass only after the bind and clear succeed, and unbinds on the way
  out.

The failures that remain under SOFTWARE (13) and STUB (29) are outside the engine layer and are
recorded rather than fixed: most assert facts about the **build's default** renderer, which cannot
hold when a non-default one is forced through `CNA_GRAPHICS_RENDERER`; the rest are XNA-layer
`TextureCube` and content tests that need real cube storage. Neither set is a regression, and both
predate this phase.

---

## 4. Open questions

The ten `OQ-*` entries at the end of `plan_modern.md` each carry a default that is being followed.
Four were answered by the owner on 2026-08-17 (`OQ-1`, `OQ-7`, `OQ-8`, `OQ-9` — see §1); the rest
stay on their defaults until raised.
