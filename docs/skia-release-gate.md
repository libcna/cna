# Skia CPU-raster release gate

Status: passed for the SKIA-1–114 CPU-raster baseline

Successor status: SKIA-115–170 is active and is not covered by this passed baseline gate. A
successor feature may be advertised only after its individual evidence and promotion task pass;
SKIA-170 remains the final gate for the successor set as a whole.

SKIA-124 arbitrary-raster-blend promotion: PASS. This promotes one bounded feature inside the
experimental Skia backend; it does not mark the successor expansion complete.

SKIA-133 mutable 2D mip promotion: PASS. Texture2D and Color RenderTarget2D complete-chain
construction, transfer, generation and sampling are promoted for the CPU-raster backend. This
does not promote non-Color formats, MSAA, 3D, cube/volume sampling or the successor set as a whole.

SKIA-135 packed Texture2D promotion: PASS. `Bgr565`, `Bgra4444`, and `Rgba1010102` have exact
typed transfer, native-precision mip, sampling and lifecycle evidence. This promotion is
Texture2D-only: non-Color targets and every other non-Color resource route remain refused.

SKIA-136 colour Texture2D promotion: PASS. `ColorBgraEXT` preserves exact BGRA transfer bytes;
`ColorSrgbEXT` preserves exact encoded bytes and performs one sRGB decode with linear-light mips.
Both are Texture2D-only; non-Color target, cube, and volume routes remain refused.

Scope: experimental `CNA_GRAPHICS_BACKEND=SKIA` CPU-raster 2D backend at the SKIA-114 checkpoint.
This is not a Ganesh/Graphite, general 3D, or full EasyGL feature-equivalence claim.

## Plan and architecture sign-off

- All 114 SKIA plan rows have a concrete disposition and evidence. Conditional SKIA-6 is closed
  as not applicable to the selected raster release, not as an accelerated implementation.
- The accepted [surface-mode ADR](skia-surface-mode-adr.md) selects raster, records ownership and
  platform boundaries, and requires a successor plan to reopen Ganesh/OpenGL acceleration.
- The accepted [3D ADR](skia-3d-emulation-adr.md) keeps the backend 2D-only after bounded
  SkVertices and CPU depth/stencil/geometry/effect prototypes. Production calls refuse uniformly.
- The 249-entry [API parity ledger](skia-easygl-parity-ledger.md) and 347-entry
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
| All nine TextureFilter min/mag/mip combinations, Clamp/Wrap/Mirror | Direct level sampling plus bounded affine LOD/inter-level shader | selector/matrix raster oracle; integer/fractional, NPOT, crop, transform and axis pixels |
| Anisotropic Texture2D sampling | Bounded exact complete-Linear fallback, capability false | byte-identical fractional-mip frame and false capability |
| All valid raster blend selector tuples, independent alpha, live constants and target-0 write masks | Direct modes plus one bounded generated runtime blender | exhaustive 714,025-tuple classifier, 62-scene public oracle, state/batch regressions |
| RenderTarget2D and single RenderTargetCube face binding | Direct raster target / six-surface bounded emulation | readback, sampling, usage, pass, lifecycle and transfer contracts |
| TextureCube/Texture3D transfer storage | Bounded CPU face/voxel emulation | checked limits, mips, partial transfers, disposal and exhaustive contracts |
| Explicit `CNA_SKIA_SKSL_V1` SpriteBatch fragment effects | Bounded opt-in extension | compiler/ABI/uniform/texture/security-limit tests |
| Mipmapped Texture2D construction/storage/generation/sampling | Bounded CNA-owned CPU chain and synchronous raster views | exact level/property/transfer/generation plus affine LOD, mip interpolation, strict crops, addressing, generated-level and stale-cache evidence |
| Texture2D per-mip transfer | Direct checked CPU-chain transfer | exact full/partial SetData/GetData at every valid level; invalid requests and caller memory remain unchanged |
| RenderTarget2D mip storage/transfer/sampling | Bounded CNA-owned surfaces and exact shadows; dirty descendants resolve once after upload/pass writes | `Skia_RenderTarget2D_MipStorage`, `Skia_RenderTarget2D_MipGeneration`, `Skia_EasyGL_RenderTarget2D_MipComplete`; SKIA-131–132 |
| Packed `Texture2D` formats | Direct `Bgr565`/`Rgba1010102` storage plus exact `Bgra4444` conversion shadow | `Skia_Texture2D_PackedFormats` 42/42 in Debug, Release and ASan+UBSan; SKIA-135 |
| BGRA/sRGB `Texture2D` formats | Direct `kBGRA_8888`; exact encoded `kSRGBA_8888` with linear-sRGB working metadata and linear-light mips | `Skia_Texture2D_ColourFormats` 31/31 including explicit linear/sRGB destinations; SKIA-136 |
| Non-Color target formats | Refused pending the format phase | stable pre-allocation diagnostics; SKIA-142–143 |
| MRT, depth/stencil, wireframe, 3D, stock 3D effects, cube/volume sampling, queries | Refused after emulation investigation | MRT/3D/query ADRs and atomic refusal suite |
| Ganesh/Graphite acceleration | Not selected or advertised | surface-mode ADR; zero `Accelerated` CTests in the raster build |

Every unsupported EasyGL family therefore has a direct, bounded-emulation, conditional-device, or
refusal decision. No row relies on a silent no-op or an implicit EasyGL fallback.

## Validation performed

- SKIA-124 successor promotion: complete Debug Skia suite 140/140 PASS in sequential Xvfb blocks
  (19 Raster, 116 Display, five Audit), including first-read, stress, and 2D demo smoke. The focused
  blend/effect set passes 17/17; the startup/classifier/generator/public-state/public-corpus set
  passes 5/5 in Release and 5/5 under ASan+UBSan (`detect_leaks=0`, both halt-on-error).
- Nine EasyGL blend/write reference regressions pass 9/9 on Xvfb: four presets, Additive golden,
  target-0 write masks, independent functions, independent factors, and BlendFactor propagation.
  All SKIA-124 builds used at most `--parallel 2`; no real display was used.
- SKIA-126 successor checkpoint: complete Debug Skia suite 142/142 PASS in sequential Xvfb blocks
  (20 Raster, 117 Display, five Audit). The construction/policy/shared-transfer set passes 3/3 in
  Debug, Release, and ASan+UBSan; its unchanged EasyGL branch passes 1/1. Builds use at most
  `--parallel 2`, and all windowed execution uses Xvfb.
- SKIA-127 successor checkpoint: complete Debug Skia suite 144/144 PASS in sequential virtual-X11
  blocks (20 Raster, 119 Display, five Audit). The new transfer, shared mip-round-trip, and shared
  70-check transfer-range fixtures pass 3/3 in Debug and Release and 3/3 under ASan+UBSan with
  only the documented Mesa GLX leak check disabled. All builds use at most `--parallel 2`.
- SKIA-128 successor checkpoint: complete Debug Skia suite 145/145 PASS in sequential virtual-X11
  blocks (20 Raster, 120 Display, five Audit). The deterministic-generation, transfer, and shared
  range fixtures pass in Debug, Release, and ASan+UBSan (`detect_leaks=0` for the documented Mesa
  GLX residual); unchanged EasyGL transfer controls pass 2/2. All builds use at most
  `--parallel 2`.
- SKIA-129 successor checkpoint: complete Debug Skia suite 146/146 PASS in sequential virtual-X11
  blocks (21 Raster, 120 Display, five Audit). The LOD raster oracle plus public sampling,
  generation, source-crop and SkSL integration set passes 5/5 in Release and 5/5 under
  ASan+UBSan (`detect_leaks=0` only for the documented Mesa GLX residual). All builds use at most
  `--parallel 2`.
- SKIA-130 successor checkpoint: complete Debug Skia suite 147/147 PASS in sequential virtual-X11
  execution (21 Raster, 121 Display, five Audit). `Skia_Texture2D_ContentMips` passes in Debug,
  Release, and ASan+UBSan (`detect_leaks=0` only for the documented Mesa GLX residual). DDS
  DXT1/DXT3/DXT5 and XNB Color/DXT5 preserve all four authored levels; single-level, incomplete,
  truncated, and cross-boundary fixtures follow the documented non-fabrication policy. Eighteen
  existing XNB and FromStream format/resize regressions pass on Xvfb. All builds use at most
  `--parallel 2`.
- SKIA-131 storage checkpoint: `Skia_RenderTarget2D_MipStorage`, the transition mip policy, and
  target SetData pass 3/3 in Debug, Release, and ASan+UBSan (`detect_leaks=0` only for the known
  external Mesa GLX residual). Sixteen existing target, mip-sampler, pass-boundary, and recovery
  regressions pass on virtual `:99`; all builds use at most `--parallel 2`. Full rendered-level
  descendant invalidation/generation remains SKIA-132 and is not claimed here. The complete Debug
  Skia suite then passes 148/148 sequentially: 21 Raster, 122 Display, and five Audit tests in
  215.52 seconds.
- SKIA-132 resolve checkpoint: the complete Debug tree builds with `--parallel 2`; 25 target, mip,
  pass-boundary, cube-target, MRT-refusal and recovery regressions pass on virtual `:99`. The five
  focused Texture2D/RenderTarget2D generation, storage, SetData and unchanged EasyGL completeness
  fixtures pass in Release and under ASan+UBSan (`detect_leaks=0` only for the documented external
  Mesa GLX residual). Exact dirty-generation counts, failed-bind atomicity, self-sampling ordering
  and dirty presenter recovery are covered. SKIA-133 owns the final full-suite/release closure.
- SKIA-133 mutable-2D-mip promotion: complete sequential Debug Skia suite 150/150 PASS on virtual
  `:99` in 202.09 seconds (21 Raster, 124 Display, five Audit). `Skia_SpriteBatch_Stress` passes in
  2.19 seconds and `Skia_ResourceBudget` in 3.86 seconds; target mip fixtures return chain, surface
  and snapshot counters to baseline. The five focused generation/storage/SetData/EasyGL-parity
  tests pass in Release and under ASan+UBSan (`detect_leaks=0` only for the documented external
  Mesa GLX residual). The complete tree builds with `--parallel 2`; no real display is used.
- SKIA-135 packed-Texture2D promotion: complete sequential Debug Skia suite 152/152 PASS on virtual
  `:99` in 198.13 seconds (21 Raster, 125 Display, six Audit). The 42-check packed-format fixture
  passes in Debug, Release, and ASan+UBSan; the complete tree builds with `--parallel 2`. Non-Color
  render targets and non-Color cube/volume resources remain refused.
- SKIA-136 BGRA/sRGB-Texture2D promotion: complete sequential Debug Skia suite 153/153 PASS on
  virtual `:99` in 238.67 seconds (21 Raster, 126 Display, six Audit). The 31-check colour-format
  fixture plus constructor/refusal contracts pass as a three-test set in Debug, Release and
  ASan+UBSan (`detect_leaks=0` only for the documented external Mesa GLX residual). The complete
  tree builds with `--parallel 2`; non-Color targets and non-Color cube/volume resources stay
  refused.
- `Skia_ParityLedger_Audit`, `Skia_TestMatrix_Audit`, `Skia_3DDecision_Audit`,
  `Skia_ReleaseGate_Audit`, `Skia_SuccessorContracts_Audit`, and
  `Skia_SurfaceFormats_Audit`: PASS. The last audit covers all 27 SKIA-134 format rows and does not
  itself promote a non-Color public route.
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
- Real MSAA, native anisotropy, depth/stencil, general 3D, MRT, queries, 2D-target mips,
  non-RGBA8 resources, and cube/volume sampling remain
  unavailable as documented.
- Windowed LSan retains the already isolated, non-growing Mesa GLX process-exit baseline; the same
  64-cycle ownership test is clean with SDL dummy/software presentation.
- The SKIA backend remains labelled experimental because its packaged-platform matrix is narrower
  than CNA's established backends. Within the documented raster scope, the gate is complete and
  release-ready.

## Reopening rule

The completed SKIA-1–114 evidence remains a regression baseline. Any successor change to the Skia
revision/GN flags, execution mode, capability true set, alpha convention, sampling fallback, or 3D
decision must update this checklist and the relevant ADR, then pass SKIA-170 and the complete
release audit/pixel suite before being advertised.
