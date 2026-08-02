# Skia CPU-raster release gate

Status: passed

Scope: experimental `CNA_GRAPHICS_BACKEND=SKIA` CPU-raster 2D backend at the SKIA-114 checkpoint.
This is not a Ganesh/Graphite, general 3D, or full EasyGL feature-equivalence claim.

## Plan and architecture sign-off

- All 114 SKIA plan rows have a concrete disposition and evidence. Conditional SKIA-6 is closed
  as not applicable to the selected raster release, not as an accelerated implementation.
- The accepted [surface-mode ADR](skia-surface-mode-adr.md) selects raster, records ownership and
  platform boundaries, and requires a successor plan to reopen Ganesh/OpenGL acceleration.
- The accepted [3D ADR](skia-3d-emulation-adr.md) keeps the backend 2D-only after bounded
  SkVertices and CPU depth/stencil/geometry/effect prototypes. Production calls refuse uniformly.
- The 248-entry [API parity ledger](skia-easygl-parity-ledger.md) and 347-entry
  [test matrix](skia-easygl-test-matrix.md) cover the current interfaces, capability enum, public
  `GraphicsDevice` surface, EasyGL registrations, manual tools, and oracle assets.

Engineering sign-off: PASS

## Capability sign-off

These are the exact live results of `SkiaGraphicsBackend::SupportsCapability`. A true value means
only the contract named by the enum; in particular `Texture3D` means bounded CPU transfer storage,
not volume sampling or a 3D renderer.

| Capability | Reported | Demonstration / decision |
|---|---|---|
| `GraphicsCapability::ThreeD` | `false` | `Skia_3D_Refusal`; complete 37-feature decision audit |
| `GraphicsCapability::DepthStencilBuffer` | `false` | target depth policy plus CPU prototype/production-refusal ADR |
| `GraphicsCapability::MultiSampleAntiAliasing` | `false` | zero-sample backbuffer/target matrix in `Skia_RenderTarget2D_MsaaPolicy` |
| `GraphicsCapability::MultipleRenderTargets` | `false` | atomic 2/3/4-target refusal in `Skia_MRT_Rejection` |
| `GraphicsCapability::AnisotropicFiltering` | `false` | exact level-zero Linear fallback at MaxAnisotropy 1/4/9999; no native feature claim |
| `GraphicsCapability::WireFrame` | `false` | rasterizer policy and rejected production geometry route |
| `GraphicsCapability::OcclusionQuery` | `false` | framebuffer-diff/replay infeasibility proof; safe false/zero object properties |
| `GraphicsCapability::CustomEffects` | `false` | arbitrary effects reject; the explicit bounded SkSL extension does not widen this enum |
| `GraphicsCapability::Texture3D` | `true` | bounded mip/slice/box CPU SetData/GetData storage and shared exhaustive contracts |

The release audit extracts this table and compares its complete enum coverage and true set with the
live backend implementation. Any new enum or capability change must update evidence before the
audit can pass.

## Direct / bounded emulation / refusal coverage

| EasyGL/CNA family | Final Skia disposition | Evidence boundary |
|---|---|---|
| Clear, presentation, resize, coordinate transforms, readback | Direct CPU `SkSurface` plus SDL upload | presentation/lifecycle/display-scale tests and 64 presenter reconstructions |
| Texture2D, SpriteBatch, SpriteFont, transforms, source rectangles, tint, flip and sort | Direct 2D canvas/image path | shared EasyGL fixtures plus nine-scene real-XNA oracle |
| Point/Linear, Clamp/Wrap/Mirror | Direct Skia sampling/tile modes | axis/seam/sampler transition tests |
| Anisotropic on level-zero SpriteBatch | Bounded exact Linear fallback, capability false | non-uniform 2×2 readback at MaxAnisotropy 1/4/9999 |
| Blend presets, one custom tuple, independent alpha and target-0 write masks | Direct modes plus bounded runtime blenders | exhaustive selector and exact pixel/oracle coverage |
| RenderTarget2D and single RenderTargetCube face binding | Direct raster target / six-surface bounded emulation | readback, sampling, usage, pass, lifecycle and transfer contracts |
| TextureCube/Texture3D transfer storage | Bounded CPU face/voxel emulation | checked limits, mips, partial transfers, disposal and exhaustive contracts |
| Explicit `CNA_SKIA_SKSL_V1` SpriteBatch fragment effects | Bounded opt-in extension | compiler/ABI/uniform/texture/security-limit tests |
| Mips for mutable Texture2D/RenderTarget2D, non-Color formats, unproven blends | Refused after focused feasibility/policy work | stable pre-draw or construction diagnostics and recovery tests |
| MRT, depth/stencil, wireframe, 3D, stock 3D effects, cube/volume sampling, queries | Refused after emulation investigation | MRT/3D/query ADRs and atomic refusal suite |
| Ganesh/Graphite acceleration | Not selected or advertised | surface-mode ADR; zero `Accelerated` CTests in the raster build |

Every unsupported EasyGL family therefore has a direct, bounded-emulation, conditional-device, or
refusal decision. No row relies on a silent no-op or an implicit EasyGL fallback.

## Validation performed

- `Skia_ParityLedger_Audit`, `Skia_TestMatrix_Audit`, `Skia_3DDecision_Audit`, and
  `Skia_ReleaseGate_Audit`: PASS.
- Updated `Skia_Sampler_MipmapFilterPolicy` and `Skia_RenderTarget2D_MsaaPolicy`: 2/2 PASS on the
  real display after adding exact anisotropy fallback and full sample-count matrices.
- Complete Debug Skia suite: 133/133 PASS on real display `:0` in 61.14 seconds with
  `--parallel 8` (16 Raster, 113 Display, four Audit). The 64-frame SpriteBatch stress fixture
  completes deterministically in 61.03 seconds and therefore has its own 120-second CTest limit;
  the previous shared 30-second smoke limit expired before the successful final hash assertion.
- Focused Release policy tests: 2/2 PASS in 1.76 seconds. The same two tests pass 2/2 under
  ASan+UBSan in 2.28 seconds with `detect_leaks=0`, `halt_on_error=1`, and only the already
  documented no-RTTI `vptr` boundary disabled by the build. No accelerated test exists in the
  pinned raster build.
- SKIA-112 clean exported-source build: 132/132 at that checkpoint, plus build instructions and
  three-frame demo smoke.
- SKIA-113 fresh backend matrix: Skia/EasyGL/SDL_Renderer/Software/Vulkan/BGFX compile; D3D11
  runtime 41/41 and D3D12 runtime 2/2 pass through their translation-layer engagement gates.

## Known release boundaries

- The only supported Skia artifact is pinned CPU raster on the documented GNU/Clang ELF build
  shape. Native Windows/MSVC, Emscripten, and accelerated Skia artifacts are not claimed.
- SDL may use a GPU renderer to upload the completed CPU image. This is not a Skia GPU surface.
- Real MSAA, native anisotropy, depth/stencil, general 3D, MRT, queries, mutable Texture2D/2D-target
  mips, non-RGBA8 resources, and cube/volume sampling remain unavailable as documented.
- Windowed LSan retains the already isolated, non-growing Mesa GLX process-exit baseline; the same
  64-cycle ownership test is clean with SDL dummy/software presentation.
- The SKIA backend remains labelled experimental because its packaged-platform matrix is narrower
  than CNA's established backends. Within the documented raster scope, the gate is complete and
  release-ready.

## Reopening rule

Any change to the Skia revision/GN flags, execution mode, capability true set, alpha convention,
sampling fallback, or 3D decision must reopen SKIA-114, update this checklist and the relevant ADR,
and rerun the complete release audit and pixel suite before being advertised.
