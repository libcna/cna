# SDL GPU Graphics Renderer — classic XNA / EasyGL parity plan and execution log

> **Current verdict (audit opened 2026-09-09; platform re-audit opened 2026-09-10; final sweep
> 2026-09-11): B. CLASSIC
> SDL GPU ↔ EASYGL PARITY NOT YET REACHED across the renderer's advertised platforms.** On the
> actually tested Linux/Vulkan configuration, every audited ordinary XNA 4.0 graphics family is
> observably equivalent or is
> one of three rigorously documented SDL_gpu API limits: arbitrary `MultiSampleMask`, exact
> half-rate `PresentInterval::Two`, and `OcclusionQuery`. Exact half-rate presentation and
> occlusion queries are the two remaining EasyGL capabilities SDL_gpu cannot express;
> `MultiSampleMask` is unimplemented in EasyGL too. SDL GPU reports the query limitation and
> refuses construction instead of fabricating results. `SDLGPU-92` now gives every built-in shader
> a pinned ShaderCross route to native D3D12/Metal formats while preserving direct SPIR-V on
> Vulkan. `SDLGPU-93` now routes compiled XNA effects through that same static compiler. However,
> `SDLGPU-99` proves that a real D3D12 device accepts all 26 production stock shaders through the
> exact renderer construction path without requiring a window; `SDLGPU-100` additionally executes
> a stock colored pipeline into a real offscreen target and byte-verifies draw and clear pixels;
> `SDLGPU-101` reaches a real windowless public `GraphicsDevice`, exact backbuffer clear/readback
> and `RenderTarget2D` clear/readback through that same D3D12 driver. `SDLGPU-102` additionally
> runs the canonical, unchanged Game-based `Texture2D`/`SpriteBatch` scene for 120 frames through
> the public lifecycle and real D3D12 driver while replacing only presentation with the virtual
> backbuffer. The expanded stock-effect sweep then exposed a real D3D12 `SkinnedEffect` pipeline
> rejection; `SDLGPU-103` replaces its vertex storage-buffer bone palette with an exact RGBA32F
> sampler-texture representation and now passes the complete shared skinned-term oracle plus four
> focused skinned/PBR regressions on both Vulkan and D3D12.
> `SDLGPU-104` makes the entire cross-compiled SDL GPU CTest inventory directly runnable through
> a fresh per-test Xvfb, SDL dummy video, the virtual backbuffer and an authenticated Wine/
> vkd3d-proton transport; no Windows test registration inherits the host `DISPLAY` anymore.
> `SDLGPU-105` uses that transport to close the shared D3D12 blend/depth/stencil/rasterizer and
> sampler matrix at 10/10 fixtures.
> `SDLGPU-106` closes D3D12 Texture2D/3D/Cube formats, transfers, mips, compressed native/fallback
> paths and public range semantics at 24/24 tests.
> `SDLGPU-107` fixes the D3D12/Metal shader-blit zero-extent defect for thin/rectangular NPOT
> render-target mip chains; the complete shared 851-assertion target-mip oracle now passes on
> D3D12 as well as all 63 isolated Vulkan legs.
> `SDLGPU-108` separates the optional modern ShaderEffect MRT diagnostics from the mandatory
> classic MRT oracle; D3D12 passes all 40 stock/compiled-effect MRT assertions while Vulkan retains
> the complete 55/55 superset.
> `SDLGPU-109` completes the D3D12 render-target family sweep at 33/33 registrations, including
> 2D/cube targets, MRT, formats, depth/stencil, MSAA, mips, readback, sampling, state restoration,
> pass ordering and deferred lifetime.
> `SDLGPU-110` completes the D3D12 buffer/draw sweep at 25/25 registrations, covering all public
> buffered/user indexed and non-indexed routes, offsets/ranges, dynamic updates, multistream,
> instancing, declarations, topologies, ordering and deferred source lifetime.
> `SDLGPU-111` executes all five reused EasyGL model oracles on D3D12: model draw, child-bone
> hierarchy, 32-bit mesh indices, per-mesh effects and skinned animation playback all pass.
> `SDLGPU-112` makes the canonical 39-case compiled-XNA-effect runtime independently runnable;
> all 39 reflection, state, draw, SpriteBatch, sampler, texture-lifetime and malformed-bytecode
> cases pass on both Vulkan and D3D12.
> `SDLGPU-113` restores the Unix fixtures' per-process contract on Windows and passes all 33
> backbuffer plus 40 bound-target/Present lifecycle legs on D3D12; six presentation/reset/resize
> programs pass alongside them.
> `SDLGPU-114` makes the constructor rollback oracle headless-aware without weakening its native
> swapchain route: D3D12 passes 291/291 applicable checks and Vulkan still passes 299/299.
> `SDLGPU-115` makes all 191 native and all 258 Windows SDL GPU integration registrations fail on
> Vulkan validation or standard D3D12 debug-layer errors/warnings, preserving narrower historical
> failure expressions instead of replacing them.
> `SDLGPU-116` gives each native platform its own SDL video-driver default and guarantees that
> offscreen/dummy registrations clear both X11 and Wayland display variables; the real Vulkan
> smoke passes 30/30 with `SDL_VIDEODRIVER=offscreen`, `DISPLAY=` and `WAYLAND_DISPLAY=`.
> `SDLGPU-117` reruns the complete post-portability adversarial audit: 191/191 native integration
> tests, 47/47 SDL GPU renderer-unit tests, all 226 contract hooks and all 246 EasyGL examples are
> accounted for with no validation diagnostic or newly unclassified classic gap.
> `SDLGPU-118` closes a lifetime hole found by the continued adversarial sweep: deferred ordinary
> and SpriteBatch compiled-effect draws now retain their MojoShader shader records until replay,
> instead of relying on driver-dependent survival of `SDL_GPUShader*` handles after the public
> `Effect` is destroyed or disposed. The expanded compiled-effect corpus passes 42/42 on both
> Vulkan and D3D12, and the focused Vulkan renderer aggregate passes 50/50.
> `SDLGPU-119` investigated the adjacent compiled-SpriteBatch texture lifetime and refuted it as a
> defect: the renderer's deliberately non-owning queued pointer is covered by SpriteBatch's own
> `shared_ptr<ITextureRenderer>` through renderer `End()`. Three shared contract tests pin that
> ordering, the real Vulkan regression survives destruction of the public wrapper, and a focused
> ASan build of the renderer/replay boundary passes the complete 43/43 compiled-effect corpus.
> `SDLGPU-120` independently reproduces the reported EasyGL letterbox failure and separates its
> two causes: EasyGL classifies the physical default presentation rectangle as a caller viewport,
> and its backbuffer readback flips Y with the logical rather than physical height. SDL GPU passes
> the identical public discriminator, so neither EasyGL defect is copied.
> `SDLGPU-121` closes a platform-specific sampler hole discovered after that sweep: SDL documents
> native `mip_lod_bias` as a Metal no-op, so every stock SpriteBatch/XNA-effect sample now applies
> its captured per-slot bias through the shader instead. The exact mip-colour oracle passes on real
> Vulkan and cross-compiled D3D12.
> `SDLGPU-122` applies the same semantic to arbitrary compiled XNA Effects: MojoShader's linked
> combined-sampler SPIR-V is rewritten before native shader creation, replay pushes one captured
> bias per D3D9 register, and Apple ShaderCross consumes that same representation rather than a
> direct-MSL bypass. Vulkan and headless D3D12 pass the 43-case compiled corpus with native bias
> deliberately rejected; the 27-pass structural corpus proves both combined and split sampler
> shapes preserve register identity.
> `SDLGPU-123` fixes the next adversarial finding: SpriteBatch draws into a cube face now use the
> face's target-local dimensions even when backbuffer logical and physical sizes differ. The
> focused test failed only its new opposite-corner discriminator before the fix and passes 25/25
> on Vulkan and headless D3D12 afterward; the 11-test native SpriteBatch/presentation companion
> slice remains green.
> `SDLGPU-124` hardens the compiled-effect pipeline cache across renderer drivers: linked programs
> now carry a monotonic MojoShader-context identity instead of using recyclable native shader
> wrapper addresses as the long-lived cache identity. Eight destroy/recreate A/B generations and
> the complete 44-case compiled-effect corpus pass on both Vulkan and headless D3D12.
> `SDLGPU-125` closes the resource-lifetime issue exposed by that same regression: compiled-effect
> pipelines no longer accumulate until renderer destruction. Each live Effect and each already-
> queued draw holds a shared program lease; releasing the last lease immediately retires that
> exact program's immutable pipelines without invalidating deferred replay. The deterministic
> cache-cardinality checks and all 44 compiled-effect cases pass on Vulkan and D3D12.
> `SDLGPU-126` closes the next resource-retention finding: SDL GPU's immutable sampler cache no
> longer retains every unrestricted ordinary `SamplerState` value until device destruction. A
> 256-entry cross-frame LRU preserves complete sampler identity, hands evicted handles to SDL's
> command-buffer-aware deferred destruction, and recreates evicted states correctly. The new
> five-case target passes on offscreen Vulkan and headless D3D12; the six existing native
> sampler/descriptor/lifetime fixtures remain green.
> `SDLGPU-127` bounds the matching immutable graphics-pipeline retention: every stock family and
> every live compiled-effect program now keeps its 256 most recently used complete state/layout
> identities and releases the least-recently-used native pipeline through SDL's command-buffer-
> aware lifetime path. The pre-fix depth-bias stress retained 298 entries and grew to 299 when an
> old state was reused; it now remains exactly 256 and recreates the evicted state with exact
> pixels on Vulkan and D3D12. Seven affected native pipeline/effect fixtures pass together.
> `SDLGPU-128` re-synchronizes the fail-closed audits after merge commit `b4508d38d` brought
> `next` into `sdlgpu`: all 266 live renderer hooks and all 247 EasyGL examples are classified,
> including the modern texture-array/storage contracts and EasyGL's CNA-specific volume-sampler
> example. The merged complete SpriteBatch sampler hook reaches SDL GPU directly and fans out to
> legacy renderer hooks by default. The audit found one ordinary-XNA verification gap, recorded as
> `SDLGPU-129`: the new XNA-measured deferred sampler-publication oracle is now shared by EasyGL,
> Vulkan and SDL GPU; all nine timing/state/pixel checks pass on Vulkan and D3D12.
> `SDLGPU-130` closes the reproducible adjacent sampler-oracle failure exposed by that focused run.
> `SDLGPU-121` had changed even a zero LOD bias from an implicit lookup to SPIR-V's explicit Bias
> operand; SDL_gpu/Vulkan then rounded 60 linearly sampled pixels one blue LSB differently according
> to the otherwise inactive minification filter. The zero-bias path is implicit again while nonzero
> bias retains the Metal-portable shader path; the strict EasyGL-equivalent oracle returns to 70/70
> on Vulkan and D3D12.
> `SDLGPU-131` closes the compiled-runtime teardown hole exposed by the merged EasyGL fix: an
> ordinary compiled Effect/runtime could outlive its `GraphicsDevice`, then ask MojoShader to delete
> shaders through the already-freed renderer context. SDL GPU now detaches every live runtime while
> the context/device remain valid; the pre-fix-failing ASan contract and the complete 45-case corpus
> pass on Vulkan, and the same corpus passes on D3D12.
> `SDLGPU-94`–`SDLGPU-96` remain open: D3D12 swapchain presentation/recovery, Metal and
> Android/Vulkan still require native runtime evidence.
> Nine EasyGL defect findings (eight distinct
> behavior families) are deliberately not copied. The last full 191/191 SDL integration sweep,
> prior 51/51 renderer-unit aggregate, new 5/5 sampler-cache and 71/71 pipeline-cache focused
> targets, and 33/33 EasyGL
> oracle and 32/32 two-renderer corpus gates remain green on Linux/Vulkan. This verdict supersedes historical
> completion banners below; those remain implementation history, not current truth.

## 2026-09-10 platform portability re-audit

The behavioral closeout below was accurate for its recorded Linux/Vulkan device, but its verdict
was not qualified narrowly enough. The post-close audit started at
`8cd9ab7f45a05bef3a727598c8d9ef5cf8b04442`: `SdlGpuRendererDescriptor` uses
`AlwaysAvailable`, while the constructor passes only `SDL_GPU_SHADERFORMAT_SPIRV` to
`SDL_CreateGPUDevice` and all 26 construction shaders pass that same format to
`SDL_CreateGPUShader`. Vendored SDL 3.5 exposes Vulkan/SPIR-V, D3D12/DXBC-or-DXIL and
Metal/MSL-or-metallib drivers. The current implementation therefore cannot substantiate the
unqualified cross-platform capability that its descriptor advertises.

This is an ordinary-XNA gap rather than modern CNAEXT work: `GraphicsDevice` construction and all
stock effects/SpriteBatch reach these shaders. The pinned MojoShader SDL_gpu adapter provides a
useful implementation precedent: it dynamically loads SDL_shadercross, asks which native formats
can be produced from SPIR-V, reflects the resource layout and compiles for the selected device.
That adapter alone is insufficient today because device creation ignores those formats, CNA does
not supply the ShaderCross runtime, and the built-in shaders bypass MojoShader.

| Item | Current evidence |
|---|---|
| Re-audit starting branch / commit | `sdlgpu` / `8cd9ab7f45a05bef3a727598c8d9ef5cf8b04442` |
| Newly created tasks | 41 (`SDLGPU-91`–`SDLGPU-131`) |
| Completed / open | 38 / 3 (`SDLGPU-91`–`93`, `SDLGPU-97`–`131` complete; `SDLGPU-94`–`96` open) |
| Ending evidence commit | `SDLGPU-131` (including the `SDLGPU-119` focused ASan follow-up) |
| Proven runtime configuration | Linux/Vulkan remains the complete behavioral configuration; D3D12 now has a real no-window device, all-26-stock-shader construction, stock-pipeline exact-pixel proof, a public windowless `GraphicsDevice` with exact backbuffer/RT2D clear readback, the unchanged public Game/Texture2D/SpriteBatch 2D scene for 120 frames, all nine shared classic stock-effect fixtures, the complete ten-fixture state/sampler matrix, the 24-test texture/format/transfer matrix, all 33 render-target registrations, all 25 buffer/draw registrations, all five model oracles and the current 45-case compiled-effect corpus. The target evidence includes the 851-assertion mip/readback oracle and 40-assertion classic MRT matrix; the draw evidence includes instancing, multistream and declaration semantics. Six presentation/reset/resize programs, 73 individually isolated backbuffer/bound-target/Present lifecycle legs and 291/291 applicable constructor/lazy-resource rollback checks also pass through D3D12. There are now 193 registered native and 260 registered Windows SDL integration tests; the pre-platform full sweep plus the focused portability gates remain the Linux behavioral baseline. The D3D12 portability probes pass 2/2, 3/3, 6/6 and 3/3; the five skinned-effect/PBR executables add 25/25 discriminating assertions. The expanded 45-case compiled-effect corpus passes Vulkan, focused ASan Vulkan and headless D3D12; the immutable-sampler target passes 5/5 on both drivers, while the immutable-pipeline target passes 71/71 on Vulkan and all 64 applicable classic checks on D3D12. The nine-case sampler-publication oracle also passes on both drivers. Unchanged dependencies were reused from the stable builds. |
| Available local cross tools | MinGW-w64, Wine 10.0, DXVK v3.0.2-58 and vkd3d-proton 3.1.0 are present. There is no Apple SDK/device or `xcrun`/`xcodebuild`/`metal`. The Android PATH audit located `adb`/platform-tools but did not locate an NDK/toolchain, `sdkmanager` or `avdmanager`, and `adb devices` reported no running/attached target. The project owner subsequently confirmed that an Android emulator is available on this host outside those searched paths, so emulator absence is not a blocker; Android execution is intentionally deferred at the owner's request. No system `dxc`, `spirv-cross`, SDL_shadercross executable or SDL_shadercross shared library was found; CNA uses its pinned static ShaderCross dependency instead. |
| Display constraint | All further Linux SDL tests must use `SDL_VIDEODRIVER=offscreen`; Windows GUI tests must use a headless/virtual display if runnable. Never use the host display. |

### SDLGPU-91 — correct the parity verdict's platform scope ✅

- **Problem/public behavior:** the prior top-level verdict generalized a Linux/Vulkan result to an
  unqualified renderer-wide result. An application can select the advertised SDL GPU renderer on
  Windows or Apple platforms, but its ordinary `GraphicsDevice` construction has no native shader
  route there.
- **EasyGL/SDL evidence:** EasyGL builds its stock programs for the active OpenGL profile. SDL GPU's
  descriptor is `AlwaysAvailable`, yet its constructor and all construction shaders hardcode
  SPIR-V. SDL's vendored headers enumerate native D3D12 and Metal shader formats that CNA never
  supplies.
- **Location:** top-level verdict, this dated re-audit and the new remediation ledger in this plan.
- **Acceptance/test:** qualify every existing runtime claim to its actual Linux/Vulkan evidence;
  identify exact source and API evidence; create bounded implementation and platform-validation
  tasks without weakening the completed Linux behavior gates.
- **Result (2026-09-10):** verdict B now records the platform gap explicitly while preserving the
  fully green Linux/Vulkan result. `SDLGPU-92`–`96` separate portable shader construction,
  compiled-effect integration and each unavailable platform validation so one external SDK or
  device cannot block independent work.

### SDLGPU-92 — compile built-in SPIR-V shaders for the active native SDL_gpu driver ✅

- **Problem/public behavior:** SpriteBatch and every built-in XNA effect depend on 26 construction
  shaders, all tagged SPIR-V. SDL_gpu's D3D12 and Metal drivers cannot consume those blobs.
- **EasyGL/SDL evidence:** EasyGL compiles renderer-native stock GLSL; SDL_shadercross exposes
  SPIR-V reflection plus native `SDL_GPUShader` compilation and the pinned MojoShader adapter
  already demonstrates its ABI and dynamic-loader names.
- **Location:** SDL GPU dependency wiring and a renderer-local shader-format/ShaderCross bridge;
  central `ConstructionResources::CreateShader` call path.
- **Acceptance/test:** retain direct SPIR-V creation on Vulkan; when the selected device does not
  support SPIR-V, reflect and compile every stock vertex/fragment shader to a device-supported
  native format; fail with a precise dependency/format diagnostic rather than a generic device
  error; unit-test selection, loading and failure cleanup; rerun constructor exception safety,
  renderer unit tests and validation-fatal stock-effect/SpriteBatch slices offscreen.
- **Result (2026-09-10):** the renderer now requests direct SPIR-V plus every format provided by a
  statically linked, pinned SDL_shadercross/SPIRV-Cross pair. Vulkan retains the direct
  `SDL_CreateGPUShader` fast path; a Metal/D3D12 device selects a central reflected ShaderCross
  path for all 26 stock shader objects. Reflection is checked against CNA's explicit sampler,
  storage and uniform counts before pipeline use, and a disjoint format set fails explicitly.
  ShaderCross initialization is process-refcounted so two renderer instances cannot make each
  other's compiler functions invalid; both successful teardown and every injected constructor
  failure release the session after all device resources. The dependency defaults on for Windows
  and Apple and off for already-native Linux/Android, with an explicit diagnostic if an opt-out
  build has no SPIR-V driver.

  The official dependencies are pinned to SDL_shadercross
  `1ff05bec573988a98ef9e0260b4da44f512b8367` and SPIRV-Cross
  `vulkan-sdk-1.4.350.0`. CNA disables DXC and all unrelated CLI/test/tool targets; the sole narrow
  upstream patch suppresses an unused install export that rejects an in-tree static dependency.
  The stable Linux build used already downloaded sources and `--parallel 2`. Four pure
  format-selection tests pass, and the offscreen/Vulkan forced-compiler constructor path reflects,
  creates and balances all 26 real stock shaders: expanded constructor safety is **299/299**.
  Reconfiguring that same stable tree with `CNA_SDL_GPU_SHADERCROSS=OFF` rebuilt only the affected
  target and its direct-SPIR-V constructor matrix remained **295/295**, proving the default Linux
  path does not acquire or require ShaderCross.
  Validation-fatal smoke and stock-effect integration tests also pass, with no host-display use.
  The post-close re-audit's hand-written count of 25 was corrected to the live 26; the historical
  baseline's own 25 count remains accurate at its recorded starting commit.

### SDLGPU-93 — align ordinary compiled-effect device formats with the portable shader route ✅

- **Problem/public behavior:** at re-audit start, the ordinary XNA compiled-effect adapter could
  dynamically invoke SDL_shadercross only after device creation, while the renderer created a
  SPIR-V-only device before MojoShader could advertise cross-compiled formats and CNA supplied no
  matching ShaderCross runtime.
- **EasyGL/SDL evidence:** EasyGL's MojoShader route generates GLSL for its active GL profile. The
  pinned SDL_gpu adapter ORs `SDL_ShaderCross_GetSPIRVShaderFormats()` into its supported-format
  result and cross-compiles linked shaders only when the device lacks its native profile.
- **Location:** renderer device-format selection, SDL GPU compiled-effect dependency/package path
  and pinned MojoShader adapter integration.
- **Acceptance/test:** device creation requests the same native formats available to stock and
  compiled shaders; locally force the non-native compiler branch and pixel-verify a representative
  custom XNA effect; missing ShaderCross is detected before creating an unusable device; Linux
  compiled-effect parity remains green. Actual non-SPIR-V-driver execution remains the explicit
  platform gate in `SDLGPU-94`/`SDLGPU-95` rather than being inferred here.
- **Result (2026-09-10):** the pinned MojoShader SDL_gpu adapter now consumes CNA's static
  SDL_shadercross target instead of probing for a separately deployed shared library. It shares
  the renderer-owned process session, advertises the exact native format set used at device
  creation, never unloads compiler functions while another renderer can own them, and passes the
  current `resource_info` ABI rather than relying on the historical metadata-prefix layout.
  Reflection failure and both shader-creation failures now release their partial allocations.

  A forced-compiler test on the real offscreen Vulkan device proves the branch rather than merely
  its descriptor: with no SDL_shadercross shared object installed, it constructs a synthetic
  compiled XNA effect, cross-compiles its linked programs through the static route, executes the
  shared buffered/user plus indexed/non-indexed draw matrix and verifies distinct read-back
  colours. The complete focused renderer slice is **47/47** with no validation diagnostics.

### SDLGPU-94 — validate ordinary parity on Windows D3D12 ⬜

- **Problem/public behavior:** no current runtime evidence proves that SDL GPU constructs or draws
  the ordinary XNA surface through SDL_gpu's D3D12 driver.
- **EasyGL/SDL evidence:** Windows is an EasyGL platform and SDL advertises a D3D12 driver accepting
  DXBC/DXIL; the current local host has MinGW-w64 and Wine but no confirmed headless D3D12 runtime.
- **Location:** stable cross-build configuration and portable renderer/oracle test registration.
- **Acceptance/test:** cross-compile incrementally, run constructor plus discriminating
  SpriteBatch/stock-effect/texture/RT/state/readback/compiled-effect slices on the D3D12 driver,
  make D3D validation diagnostics fatal where available, and record exact environment evidence.
- **Status:** open. The stable MinGW tree is configured with the existing Windows SDL3 package and
  now links its complete smoke PE. Two external `sharp-runtimenext` portability defects required
  build-only workarounds: an empty temporary `<poll.h>` because the include is outside that
  dependency's existing Windows guard, and its missing `bcrypt` link for `BCryptGenRandom`.
  Neither external checkout nor CNA source was changed for those workarounds. A plain Wine launch
  creates the D3D12 device but its mismatched system `dxgi` crashes while constructing SDL's
  swapchain; the repository's matched Proton path gets farther and asks Vulkan for the real
  800x480 presentation surface, but Xvfb has no DRI3 and reports that the surface is unsupported.
  No host display was used. `SDLGPU-99` independently proves the native D3D12 stock-shader path
  without a window, `SDLGPU-100` proves a real stock pipeline/upload/render/download sequence with
  byte-exact pixels, and `SDLGPU-101` proves public `GraphicsDevice` construction plus exact
  backbuffer and `RenderTarget2D` clears/readbacks. `SDLGPU-102` also runs the canonical public
  Game/Texture2D/SpriteBatch scene for 120 frames. The first comprehensive stock-effect pass found
  a real `SkinnedEffect` graphics-pipeline rejection (`E_INVALIDARG`); `SDLGPU-103` fixes it and
  all nine shared classic effect fixtures and the shared instanced-draw fixture now execute through
  D3D12, with the skinned path adding 25/25 focused assertions. `SDLGPU-105` closes the shared
  blend/depth/stencil/rasterizer/sampler matrix at 10/10, and `SDLGPU-106` closes Texture2D/3D/
  Cube format, transfer and sampling coverage at 24/24. `SDLGPU-107` fixes and closes the complete
  shared render-target mip/readback matrix at 851/851 assertions, and `SDLGPU-108` closes the
  mandatory classic MRT matrix at 40/40. `SDLGPU-109` closes the complete 33-test render-target
  family, `SDLGPU-110` closes the 25-test buffer/draw family, and `SDLGPU-111` closes all five
  model oracles. `SDLGPU-112` also runs the complete canonical 39-case compiled-effect suite on
  D3D12. `SDLGPU-113` adds six presentation/reset/resize programs plus 33 isolated backbuffer and
  40 isolated bound-target/Present lifecycle legs. `SDLGPU-114` closes the headless constructor-
  failure matrix at 291/291 applicable checks. Only real swapchain presentation/acquire/minimize/
  recovery remains open here;
  presentation still requires a presentation-capable isolated compositor or real Windows runner.
  The 2026-09-11 retry used Wine 10.0 plus matching local DXVK v3.0.2-58 and vkd3d-proton 3.1.0
  binaries from Proton Experimental. It selected the real AMD Radeon 780M, initialized the native
  D3D12 device and reached `dxgi_vk_swap_chain_init` at 800x480 before reporting `No DRI3 support
  detected - required for presentation` and `Surface is not supported for presentation` on the
  fresh Xvfb. SDL's offscreen driver cannot substitute here because its window has no Win32 HWND;
  Xvfb, including nested Xephyr/glamor, exposes no DRI3; and no headless Wayland compositor is
  installed. This is an exact runner/display-stack blocker, not renderer success or failure.

### SDLGPU-95 — validate ordinary parity on Apple Metal ⬜

- **Problem/public behavior:** no macOS/iOS build or Metal runtime evidence exists.
- **EasyGL/SDL evidence:** SDL_gpu's Metal driver consumes MSL/metallib, while CNA currently ships
  only SPIR-V stock shaders and this Linux host has no Apple SDK or device.
- **Location:** Apple build/package integration and the portable renderer corpus.
- **Acceptance/test:** build on a supported Apple SDK and execute the same discriminating ordinary
  corpus through the Metal driver with native diagnostics enabled; record any true SDL limitation
  rather than inferring success from source inspection.
- **Status:** open; runtime validation has an external hardware/SDK dependency. The 2026-09-11
  environment audit found no `xcrun`, `xcodebuild` or `metal`, no Apple SDK and no Apple device, so
  neither an honest Metal build nor runtime test can be produced on this Linux host. The pure
  renderer-unit format-selection test does prove that MSL/metallib-only device formats select the
  ShaderCross route, but it is not misreported as a Metal runtime result.

### SDLGPU-96 — validate the retained SPIR-V route on Android/Vulkan ⬜

- **Problem/public behavior:** Android is an SDL GPU target but has no current construction,
  lifecycle or ordinary-draw evidence in this plan.
- **EasyGL/SDL evidence:** the built-in SPIR-V representation should remain usable through Android
  Vulkan, but the claim is untested and window/swapchain lifecycle differs from desktop.
- **Location:** Android build configuration plus a compact portable SDL GPU corpus.
- **Acceptance/test:** cross-build with a supported NDK and run construction/lifecycle,
  SpriteBatch, stock effect, texture, render-target and readback checks on an Android Vulkan
  device/emulator without weakening the desktop suite.
- **Status:** open; execution is intentionally deferred at the project owner's request. The
  2026-09-11 PATH audit found `/usr/lib/android-sdk/platform-tools/adb` but did not locate an
  NDK/toolchain, `sdkmanager` or `avdmanager`, and `adb devices` returned an empty running/attached
  inventory. The owner subsequently confirmed that an Android emulator is available on this host
  outside those searched paths; its absence is therefore **not** a blocker and must not be used as
  a reason to skip this task. Desktop Vulkan's complete SPIR-V proof is relevant implementation
  evidence but cannot replace Android surface and
  lifecycle execution.

### SDLGPU-97 — make optional ShaderEffect compilation target-correct ✅

- **Problem/public behavior:** the Windows cross-build selected the build host's absolute
  `/usr/lib/x86_64-linux-gnu/libshaderc.so.1`. That ELF library cannot be linked into a PE binary,
  and even a target-native shaderc would currently produce raw SPIR-V that SDL_gpu's D3D12/Metal
  drivers cannot consume. Nevertheless SDL GPU reported `CustomEffects` unconditionally.
- **EasyGL/SDL evidence:** EasyGL delegates GLSL compilation to the active GL driver. SDL GPU's
  CNA-specific `ShaderEffect` path requires a runtime compiler in addition to the portable stock
  and compiled-XNA-effect routes. This feature is outside classic-XNA parity, but must not break a
  supported cross-build or make a false capability claim.
- **Location:** SDL GPU dependency discovery, `SupportsCapability`, `CompileProgram`, smoke
  assertions and conditional ShaderEffect test registration.
- **Acceptance/test:** never search explicit host library directories while cross-compiling;
  retain and execute the native Linux/Vulkan ShaderEffect test; on targets without a complete
  target-native GLSL-to-driver route, report `CustomEffects == false`, return an invalid effect
  with a precise diagnostic, and cross-compile the affected SDL GPU renderer/smoke translation
  units without a host ELF input. Do not alter compiled XNA effects, which use
  MojoShader/ShaderCross. The full Windows executable link remains part of `SDLGPU-94`, so a later
  dependency outside these translation units cannot disguise whether this defect itself is fixed.
- **Result (2026-09-10):** dependency discovery now runs only for targets whose SDL_gpu driver can
  consume the generated SPIR-V; the explicit `/usr/lib` fallback is native-build-only. The feature
  compile switch is renderer-private, so toggling this optional path does not rebuild the complete
  graphics module. On an unavailable target `SupportsCapability(CustomEffects)` is false,
  `CompileProgram` deterministically returns a diagnostic naming the missing target-native path,
  and the positive ShaderEffect integration test is not registered. The MinGW configuration
  reports the disabled path, its generated Ninja graph contains neither the host shaderc path nor
  the enable define, and both changed Windows translation units compile successfully. The full
  target then reaches an independent pre-existing `sharp-runtimenext` failure where its Windows
  build unconditionally includes POSIX `poll.h`; that external dependency blocker is recorded
  under `SDLGPU-94`, not misattributed here. On native Linux/Vulkan, the real ShaderEffect pixel
  test remains **4/4** and smoke remains **30/30**, both run with
  `SDL_VIDEODRIVER=offscreen`, `DISPLAY=` and no validation diagnostic.

### SDLGPU-98 — order the static ShaderCross closure before SDL on GNU/MinGW ✅

- **Problem/public behavior:** the first complete MinGW smoke link contained every required
  library but placed `libSDL3.dll.a` before static `libSDL3_shadercross.a`. GNU ld's one-pass
  archive resolution therefore left ShaderCross calls such as `SDL_SetError`,
  `SDL_GetStringProperty` and `SDL_CreateGPUComputePipeline` unresolved, preventing any Windows
  SDL GPU application from linking.
- **EasyGL/SDL evidence:** this is not a behavioral EasyGL difference; it is a portability defect
  introduced by the new static translation closure. The identical native Linux link happened to
  succeed because shared-library resolution/linker behavior did not expose the ordering error.
- **Location:** SDL GPU renderer target dependency order.
- **Acceptance/test:** put SDL after every renderer-owned static archive that calls it; link the
  MinGW smoke PE without unresolved SDL symbols; retain an incremental native smoke link/run.
- **Result (2026-09-10):** `cna_sdl_shadercross` now publishes a MinGW `RESCAN` link group
  containing static ShaderCross followed by SDL, while other linkers retain their ordinary
  ordered dependency. The resulting PE link line contains
  `--start-group libSDL3_shadercross.a libSDL3.dll.a --end-group` and the complete smoke executable
  links without any unresolved SDL symbol. The only extra link flag, `bcrypt`, compensates for the
  separately recorded external SharpRuntime defect and is not part of this change. Native Linux
  was incrementally relinked and its offscreen/Vulkan smoke remains **30/30**, with no validation
  diagnostic and no host display.

### SDLGPU-99 — prove production stock-shader construction on real headless D3D12 ✅

- **Problem/public behavior:** the available D3D12 device path could not reach ordinary renderer
  construction under Xvfb because renderer construction claims a window and SDL immediately needs
  a presentation-capable swapchain. That left the central `SDLGPU-92` claim — that all stock
  SpriteBatch/effect shaders actually translate into a format accepted by D3D12 — supported only
  by Vulkan's forced-compiler test and a Windows cross-link.
- **EasyGL/SDL evidence:** EasyGL compiles every stock program for its active GL implementation.
  SDL_gpu device and shader creation do not require a claimed window; only swapchain presentation
  does. The renderer already owns one transactional list of 26 shader descriptors, including the
  exact sampler/storage/uniform counts checked against ShaderCross reflection.
- **Location:** `SdlGpuRenderer` construction-resource helper and the existing smoke executable's
  explicit portability-probe mode. No public XNA/CNA graphics API was added.
- **Acceptance/test:** reuse the exact production device-format negotiation and all 26 stock shader
  descriptors on a caller-selected driver; create and release them without creating or claiming a
  window; require SDL to report the requested driver; run the MinGW PE with `DISPLAY=`, SDL's dummy
  video driver and a version-gated vkd3d-proton D3D12 device; retain the native Vulkan constructor
  and smoke regressions. Do not infer swapchain or pixel parity from this narrower proof.
- **Result (2026-09-10):** all renderer construction shader calls now pass through one
  `CreateConstructionShaders` helper, used unchanged by both normal windowed construction and the
  no-window probe. The probe requests SPIR-V plus ShaderCross's target formats, requires the exact
  named SDL driver, reflects every resource layout and creates all 26 real native shader objects;
  its transactional owner releases every shader, the device and the compiler session on success
  or failure. The Windows smoke PE was incrementally rebuilt and executed with `DISPLAY=`,
  `SDL_VIDEODRIVER=dummy`, `SDL_GPU_DRIVER=direct3d12`: vkd3d-proton authenticated itself as
  **3.1.0**, reported Shader Models 6.6–6.8 and DX Ultimate, SDL reported `direct3d12`, and the probe
  passed **2/2**. The same probe passed **2/2** on native Vulkan with
  `SDL_VIDEODRIVER=offscreen`; the ordinary validation-fatal Vulkan smoke remained **30/30** and
  constructor exception/lifetime safety remained **299/299**. No test opened a host-display window.

### SDLGPU-100 — pixel-verify a production stock pipeline on real headless D3D12 ✅

- **Problem/public behavior:** creating shader objects does not prove that their translated input
  signatures, uniforms and fragment output can form and execute the graphics pipeline used by an
  ordinary `BasicEffect` colored draw. The window/swapchain blocker in `SDLGPU-94` prevented the
  existing public renderer corpus from answering that narrower question on D3D12.
- **EasyGL/SDL evidence:** EasyGL's corresponding `VertexPositionColor` path is extensively
  pixel-tested. SDL_gpu supports render targets, vertex uploads, render passes and texture
  downloads without a window or swapchain, so this behavior can be tested honestly despite Xvfb's
  missing DRI3 presentation support.
- **Location:** a test-only `SdlGpuRenderer` no-window stock-draw probe and the existing smoke
  executable's explicit portability mode. The ordinary renderer's shader descriptors remain the
  single construction source.
- **Acceptance/test:** on a named native driver, create all production shaders, build the exact
  colored stock VS/FS pair with the `VertexPositionColor` layout, upload a non-white/non-primary
  RGBA triangle, push identity/diffuse/vertex-color/fog uniform blocks, draw it into RGBA8, download
  the target and byte-verify both an interior texel and an untouched clear texel. Require the two
  signatures to differ and retain the equivalent native Vulkan proof and ordinary smoke test.
- **Result (2026-09-10):** the no-window path now performs the complete upload → render → download
  command sequence through the production `colored3d` shader objects. Its 8x8 target yields the
  exact interior RGBA **(17,83,201,239)** and exact untouched clear RGBA **(3,7,13,255)**, so a
  missing draw, wrong vertex declaration, ignored uniform, swapped channel or fabricated clear
  cannot satisfy both checks. The incrementally rebuilt MinGW PE passes **3/3** with `DISPLAY=`,
  `SDL_VIDEODRIVER=dummy` and the real `direct3d12` driver; vkd3d-proton 3.1.0's version gate and
  device logs are present. Native Vulkan passes the identical **3/3** bytes with
  `SDL_VIDEODRIVER=offscreen`, and the normal validation-fatal Vulkan smoke remains green. This is
  a real D3D12 pipeline/readback proof, but deliberately does not claim swapchain or full public
  API parity for `SDLGPU-94`.

### SDLGPU-101 — enable windowless public GraphicsDevice validation on SDL GPU ✅

- **Problem/public behavior:** the D3D12 probes could execute native shaders and commands, but the
  ordinary XNA-facing `GraphicsDevice`, `RenderTarget2D` and `GetBackBufferData` seams still could
  not be reached without SDL claiming a presentation window. CNA already exposes the narrow
  `PresentationParameters::HeadlessEXT` transport used by its D3D12 renderer specifically for
  off-screen public-API tests; SDL GPU rejected the resulting null window despite SDL_gpu device,
  target and transfer commands being window-independent.
- **EasyGL/SDL evidence:** EasyGL remains the behavioral oracle on the fully tested Linux path.
  SDL_gpu only requires a window for swapchain commands; `SDLGPU-99`/`100` already proved that its
  D3D12 device, shaders, pipelines, render targets and downloads work without one. Vendored SDL's
  D3D12 `ClaimWindow` creates a swapchain and is the exact boundary that fails under dummy/Xvfb.
- **Location:** `SdlGpuRenderer` construction/frame submission/backbuffer-format state, existing
  `PresentationParameters::HeadlessEXT` documentation, and the smoke executable/CTest. This adds
  no new public API and receives no classic parity credit of its own; it is test transport for the
  ordinary surface.
- **Acceptance/test:** construct a real public `GraphicsDevice` with no window/swapchain; select
  and verify a named native driver; preserve requested viewport dimensions; execute exact nontrivial
  RGBA public `Clear` → `GetBackBufferData`; execute public `RenderTarget2D` clear/readback over
  every texel; run identically on native Vulkan and MinGW/Wine/vkd3d D3D12 with no host display;
  retain ordinary swapchain smoke and constructor resource-balance gates.
- **Result (2026-09-10):** a null SDL window now selects a real renderer-owned RGBA8 (or BGRA8
  fallback) off-screen backbuffer and skips only claim/configure/acquire/present operations. Every
  pipeline/cache path reads one constructor-captured backbuffer format, so windowed behavior is
  unchanged and headless code never queries an absent swapchain. The proxy is also the headless
  readback source, while render targets use their normal production path. Native Vulkan passes the
  new registered validation-fatal test **6/6**, ordinary offscreen swapchain smoke remains
  **30/30**, and constructor exception/lifetime safety remains **299/299**. The incrementally
  rebuilt MinGW PE passes the identical **6/6** via real `direct3d12`; vkd3d-proton authenticates
  version **3.1.0** and reports Shader Models 6.6–6.8/DX Ultimate. Both runs used `DISPLAY=`; no
  host display or presentation claim was made. Real D3D12 presentation remains `SDLGPU-94`.

### SDLGPU-102 — reuse the ordinary Game/SpriteBatch corpus without a swapchain ✅

- **Problem/public behavior:** `SDLGPU-101` could validate hand-authored public calls against a
  null-window `GraphicsDevice`, but the existing renderer-neutral and SDL GPU examples construct a
  normal `Game`/`GraphicsDeviceManager` window. Under the local isolated Wine/vkd3d runner, SDL's
  D3D12 device works while the swapchain does not, so those canonical tests could not reach their
  ordinary public texture, batching and frame-lifecycle code.
- **EasyGL/SDL evidence:** `sdlgpu_2d_test.cpp` is the established public 2D vertical slice: an
  unchanged `Game` performs a real `Texture2D` upload and 120 frames of `SpriteBatch` tint, alpha,
  rotation, flip and sampler variants. SDL_gpu requires a window only for claim/swapchain/present;
  its render targets, uploads, pipelines and downloads are independent, as the preceding D3D12
  probes demonstrated.
- **Location:** renderer construction/window registration and `OnSurfaceChanged`, plus a second
  CTest invocation of the canonical 2D executable. No public API or test-scene clone is added.
- **Acceptance/test:** preserve the normal swapchain invocation; opt in only through an explicitly
  test-named environment value; keep the real dummy/offscreen window, registry, resize and Game
  lifecycle while skipping only GPU window claim/swapchain operations; reject the opt-in unless
  SDL is using `dummy` or `offscreen`; run the same executable on Vulkan and D3D12 without the host
  display and retain validation-fatal policy.
- **Result (2026-09-10):** `CNA_SDLGPU_TEST_FORCE_HEADLESS=1` now selects the same virtual
  backbuffer as null-window headless mode while retaining and registering a non-null platform
  window. `OnSurfaceChanged` therefore continues to exercise the public window lifecycle. The
  switch is accepted only when SDL reports its `dummy` or `offscreen` video driver, and Windows
  CTest registration explicitly clears both host-display variables. The original and virtual
  Vulkan invocations both pass **3/3**; the unchanged MinGW executable passes **3/3** for 120
  frames through real `direct3d12`, with vkd3d-proton **3.1.0** authenticated. That local Windows
  run used SDL `dummy` inside a fresh `xvfb-run` display and cleared `WAYLAND_DISPLAY`; no host
  display or swapchain was used. An earlier invalid manual invocation omitted the opt-in and
  inherited `DISPLAY=:0`; it crashed in Wine's swapchain path and contributes no capability
  evidence. The added driver guard and isolated CTest environment prevent that configuration from
  being accepted as a headless test.

### SDLGPU-103 — make the full SkinnedEffect bone palette portable to D3D12 ✅

- **Problem/public behavior:** the first expanded D3D12 stock-effect sweep passed BasicEffect,
  AlphaTestEffect, DualTextureEffect and EnvironmentMapEffect, but both the shared
  `parity_skinned_terms` fixture and the older minimal `SdlGpu_Skinned` executable failed before
  drawing: `SDL_CreateGPUGraphicsPipeline` rejected the production skinned pipeline with
  `E_INVALIDARG`. Ordinary XNA `SkinnedEffect` therefore could not render at all on this backend.
- **EasyGL/SDL evidence:** EasyGL uploads all 72 matrices and its bone/lighting corpus is green.
  SDL GPU's Vulkan route also passed, including a translation at bone index 71, but its vertex
  shader represented the 4608-byte palette as a graphics storage buffer. ShaderCross produced a
  valid DXBC shader object, then SDL_gpu's D3D12 PSO creation rejected that storage-buffer variant;
  the failure reproduced with both triangle strip and triangle list, ruling out the fixture's
  topology. Pushed uniform data is not a correct fallback because the existing Vulkan binary-search
  test measured a real 4096-byte per-slot limit.
- **Location:** the three SDL GPU skinned vertex shaders, their construction resource counts,
  deferred bone-palette upload/bind/lifetime code and generated SPIR-V header. The existing PBR
  skinned variants share the representation and are changed only to preserve their already-shipped
  behavior; this does not expand modern CNAEXT scope.
- **Acceptance/test:** retain all 72 exact column-major matrices without truncation; make both
  colored and uncolored `SkinnedEffect` pipelines create and draw through D3D12; preserve
  weighted bones, last-slot access, Vector4/Byte4 blend indices, lighting, specular, vertex color
  and nonidentity world-normal behavior; keep Vulkan validation clean; retain skinned PBR pixels.
  Reuse the same renderer-neutral oracle rather than cloning it.
- **Result (2026-09-10):** each deferred draw now uploads its issue-time palette to a 288x1
  `R32G32B32A32_FLOAT` sampler texture—four adjacent column texels per matrix—and binds it at the
  vertex stage. The single 4608-byte row is naturally 256-byte aligned for D3D12 transfer rules,
  `texelFetch`/ShaderCross preserves exact values, and the transient texture is released with the
  command's other uploaded resources. Shader reflection now truthfully declares one vertex
  sampler and zero storage buffers for all classic and PBR skinned variants.

  Native offscreen Vulkan passes the shared skinned-term fixture plus minimal, PBR, vertex-color,
  world-normal and fog regressions (**6 executables, all green**) with no validation diagnostic.
  The incrementally rebuilt MinGW binaries pass the shared 13-assertion fixture, minimal **3/3**,
  PBR **3/3**, vertex-color **2/2** and world-normal **4/4** through real `direct3d12`: **25/25**
  discriminating assertions across five executables. In particular bone 71, 50/50 blending,
  distinct one/three-light output, specular, both blend-index declarations and nonuniform normal
  transforms are observed in pixels. Every Windows run used SDL `dummy`, forced virtual
  backbuffer mode and a fresh isolated Xvfb display with `WAYLAND_DISPLAY` cleared; no swapchain or
  host display was used. D3D12 debug-layer validation is unavailable under this Wine/vkd3d setup,
  so the result records clean SDL debug output and exact pixels, not an invented native-validation
  claim.

### SDLGPU-104 — make the Windows D3D12 parity corpus safely CTest-runnable ✅

- **Problem/public behavior:** after `SDLGPU-102`, cross-compiled tests still had to be invoked one
  executable at a time. CTest had no PE emulator and its ordinary SDL GPU registrations inherited
  the historical `SDL_VIDEODRIVER=x11;DISPLAY=:0` cache values. That could neither execute a
  Windows binary directly nor provide the isolated dummy/headless transport required for honest
  D3D12 verification, and it made accidentally touching the host display too easy.
- **EasyGL/SDL evidence:** the canonical renderer-neutral fixtures are already registered once for
  each renderer; no clone is needed. The proven local D3D12 route requires Wine plus authenticated
  vkd3d-proton, while vkd3d's DXGI adapter enumeration requires an X server even though SDL uses
  `dummy` and the renderer creates no swapchain. Manually combining these pieces passed, but CTest
  did not encode them.
- **Location:** SDL GPU example/test target registration and a narrow repository test wrapper. No
  production renderer or public API behavior changes.
- **Acceptance/test:** attach a cross-compiling emulator to every SDL GPU integration target;
  allocate a fresh isolated Xvfb per invocation; force SDL `dummy`, D3D12 and the guarded virtual
  backbuffer; clear Wayland state; keep the existing vkd3d-proton positive-engagement gate; ensure
  CTest metadata contains no host `DISPLAY`; run multiple existing pixel fixtures directly with
  `ctest --test-dir cmake-build-sdlgpu-windows` and no outer display setup.
- **Result (2026-09-10):** every cross-compiled target created by `cna_sdlgpu_test` now carries a
  CMake `CROSSCOMPILING_EMULATOR` chain to `run-wine-vkd3d-headless.sh`. The wrapper creates a new
  1024x768 Xvfb, unconditionally selects SDL dummy video, `direct3d12` and
  `CNA_SDLGPU_TEST_FORCE_HEADLESS=1`, clears Wayland, then delegates to the existing vkd3d-proton
  runner whose version log is mandatory. Cross-CTest environment metadata now contains those
  explicit headless values and no `DISPLAY` entry; native Windows and Linux registration are not
  changed.

  Configuration regenerated without building. CTest then directly launched the already-built
  skinned and instanced fixtures **2/2**, and after one incremental target build launched the
  previously absent independent-UV DualTexture fixture **1/1**. Thus three distinct shaders/input
  paths pass through the self-contained transport, including the newly repaired vertex-sampler
  path, native instancing and two texture-coordinate streams. `bash -n` passes; `shellcheck` is not
  installed. No host display or swapchain was used.

### SDLGPU-105 — verify the immutable-state and sampler matrix on D3D12 ✅

- **Problem/public behavior:** Linux/Vulkan had exhaustive shared EasyGL parity evidence for the
  state families most vulnerable to immutable pipeline/sampler cache-key omissions, but D3D12 had
  only one colored pipeline. Shader portability alone does not prove that the D3D12 backend accepts
  every descriptor combination or produces the XNA-visible result.
- **EasyGL/SDL evidence:** the existing renderer-neutral fixtures compute discriminating pixel
  signatures rather than merely inspecting descriptors: all blend factors/equations and separate
  alpha, all depth comparisons, stencil operations/comparisons, A→B→A restoration, both winding
  directions, scissor, viewport, depth bias, wireframe, filter/mip modes, anisotropy,
  `MaxMipLevel`, LOD bias and SpriteBatch sampler propagation. These are the same sources already
  green on EasyGL and Vulkan.
- **Location:** no production change; reuse the ten canonical sources under
  `modules/graphics/examples/parity/` through `SDLGPU-104`'s cross-CTest transport.
- **Acceptance/test:** incrementally build only the ten missing targets; execute them sequentially
  through real `direct3d12`; require exact/invariant pixel assertions, SDL debug cleanliness and
  authenticated vkd3d-proton for every process; do not use a swapchain or host display.
- **Result (2026-09-10):** `blend_states`, `depth_states`, `stencil_states`, `stencil_compare`,
  `rasterizer_viewport`, `fill_mode_wireframe`, `sampler_filters`, `sampler_max_mip_level`,
  `sampler_lod_bias` and `sprite_sampler_state` pass **10/10** through the MinGW/Wine/vkd3d D3D12
  build. Each CTest received its own Xvfb and SDL dummy/virtual backbuffer, and every invocation's
  vkd3d-proton gate passed. No SDL debug failure or CTest validation/error pattern appeared.
  D3D12's native debug layer remains unavailable in this environment, so this closes observable
  state/sampler behavior—not the separate native-validation or presentation portions of
  `SDLGPU-94`.

### SDLGPU-106 — verify classic texture formats and transfers on D3D12 ✅

- **Problem/public behavior:** D3D12 had only one ordinary `Texture2D`/SpriteBatch smoke scene.
  It did not prove the classic packed/compressed formats, native-versus-converted storage,
  authored mips, partial regions, all cube faces, volume boxes or public SetData/GetData range
  semantics that EasyGL and the complete Vulkan audit support.
- **EasyGL/SDL evidence:** the existing exact EasyGL sources and SDL-specific matrices separate
  every relevant behavior: packed 16-bit and DXT sampling, truthful construction refusals, DXT1/3/5
  native and forced decode paths, NPOT/partial blocks, authored tail mips, DDS/XNB content, six
  cube faces, independent volume slices/boxes and nonzero destination offsets. Running those same
  sources on the native driver is stronger evidence than cloning expectations for Windows.
- **Location:** no production change; reuse the existing Texture2D, Texture3D, TextureCube and
  cube/volume transfer targets through `SDLGPU-104`'s D3D12 CTest transport.
- **Acceptance/test:** incrementally build only missing texture targets; run both native format and
  renderer-conversion variants; byte/pixel-verify readback and sampling; retain exact public range
  and invalid-format results; require SDL debug cleanliness and authenticated vkd3d-proton without
  a swapchain or host display.
- **Result (2026-09-10):** the 2D/cube group passes **16/16**: EasyGL DXT, packed16 and refusal
  oracles; both native and forced-fallback 2D matrices; four cube face/partial/mip/content oracles;
  the compressed-cube fallback; both native and fallback cube matrices; and both compressed-content
  routes. The volume group passes **8/8**: the full SDL Texture3D matrix, create-validity test, four
  EasyGL slice/partial/readback/mip oracles, and the shared cube/volume GetData and SetData contracts.
  Total D3D12 texture evidence is therefore **24/24** tests, including DXT1/3/5, packed 16-bit,
  Color volume storage and every ordinary format boundary claimed by the Linux matrix. Each
  process passed the vkd3d-proton gate under its own Xvfb/SDL dummy/virtual backbuffer and emitted
  no SDL debug failure. Native D3D12 debug-layer validation remains unavailable and is not claimed.

### SDLGPU-107 — preserve thin/NPOT render-target mip chains on shader-blit drivers ✅

- **Problem/public behavior:** the first comprehensive D3D12 render-target run failed 70 of 851
  assertions. Generated late mips of ordinary mipmapped `RenderTarget2D`s such as 13x7, 1x13,
  13x1, 2x13 and 13x2 became exactly transparent black, with the same bad value observed both by
  direct `GetData` and by sampling. MSAA=4 and MSAA=0 both failed, so this was mip generation—not
  resolve or transfer. Public `LevelCount` remained correct, making the advertised levels real but
  observably unpopulated.
- **EasyGL/SDL evidence:** EasyGL and SDL GPU/Vulkan pass the renderer-neutral oracle. Vendored
  SDL_gpu's common shader blit used by D3D12 and Metal computes UV denominators as
  `base >> sourceMip`, while their render-pass setup likewise uses an unclamped shifted target
  extent. For a legal XNA chain whose shorter axis has already clamped to one, one of those native
  values becomes zero. CNA already passed correct `max(1, ...)` blit regions, but the shared SDL
  implementation recomputed the invalid zero internally. Vulkan's direct image-blit path does not
  have this failure.
- **Location:** renderer-local `GenerateRenderTargetMipChain`; no public API or vendored SDL source
  change.
- **Acceptance/test:** retain the direct GPU path for levels with nonzero native shifted extents;
  generate every later level through valid single-level GPU resources and copy it into the public
  target; preserve post-resolve ordering, all MRT attachments, sampling and deferred lifetime;
  pass every MSAA/non-MSAA square, odd, rectangular, 1-wide/1-high, repeated-use, MRT, cube and
  sampled-final-mip leg on Vulkan and D3D12 without host-display use or validation diagnostics.
- **Result (2026-09-10):** once a destination's raw shifted axis would become zero, the renderer
  creates a one-level scratch target at the true clamped extent. The shader blit therefore always
  reads/writes mip zero of valid resources; a following GPU copy pass stores the result into the
  corresponding public mip, and that scratch becomes the next level's source. Earlier levels keep
  the existing direct path. Scratch handles join the renderer's post-submit texture-release queue,
  so deferred command replay cannot outlive them.

  Before the change, the D3D12 fixture reported **781/851** and its 13x7 final mip was exactly
  `(0,0,0,0)`. After an incremental renderer/test relink, the unchanged shared fixture passes
  **851/851** through real `direct3d12` under the per-test Xvfb/SDL-dummy/virtual-backbuffer wrapper.
  The native Vulkan supervisor passes all **63/63** isolated legs, including both MSAA modes,
  every thin orientation, MRT, cube and two repeated sample/readback cycles. No host display,
  swapchain or new modern CNAEXT feature was used; no SDL/Vulkan validation diagnostic appeared.

### SDLGPU-108 — keep classic MRT verification independent of optional ShaderEffect ✅

- **Problem/public behavior:** the cross-compiled `SdlGpu_MRT` executable mixed the mandatory
  ordinary XNA compiled-effect/stock MRT oracle with fifteen diagnostics for CNAEXT
  `ShaderEffect`. The D3D12 build correctly reports `CustomEffects == false` because it has no
  target-native runtime GLSL compiler, but the test still constructed and executed those invalid
  effects. Their fallback stock draws warmed the sprite cache and failed the custom-output pixels,
  causing the whole test to fail even though its later classic compiled Effect independently wrote
  distinct `oC0`/`oC1` values correctly.
- **EasyGL/SDL evidence:** the ordinary API ownership boundary is the compiled Effect path, which
  uses the same renderer-neutral synthetic Effect Framework 9.1 program that is green on EasyGL
  and Vulkan. `ShaderEffect` is CNAEXT and optional; `SDLGPU-97` deliberately makes its unsupported
  D3D12 state truthful. A classic parity gate must neither require that excluded feature nor skip
  the ordinary MRT checks with it.
- **Location:** `sdlgpu_mrt_test.cpp` capability partition and expected-check accounting only; no
  production or public API change.
- **Acceptance/test:** query `GraphicsCapability::CustomEffects` before constructing any
  ShaderEffect; exclude only its count/output/write-mask/segment subcorpus when false; still require
  stock SpriteBatch target-count/depth/MSAA/write-mask cache identity and every compiled-effect
  output/format/blend assertion; preserve the full ShaderEffect superset on Vulkan; run both without
  a host display.
- **Result (2026-09-10):** target resources shared by the classic cache matrix are still created,
  while the four runtime-compiled effects and exactly fifteen dependent assertions are gated by the
  renderer's truthful capability. The expected count is now explicit as 30 mandatory stock MRT
  assertions, plus ten when ordinary compiled effects are built, plus fifteen only when
  ShaderEffect is supported. Native offscreen Vulkan remains **55/55**. The unchanged MinGW
  compiled-effect route passes **40/40** through real `direct3d12`: independent `oC0`/`oC1`, mixed
  Color/RGBA16F targets, per-slot masks with additive blending, target-count/depth/MSAA/write-state
  pipeline identities and A→format→state→A restoration all remain mandatory. CTest used its fresh
  Xvfb/SDL-dummy/virtual-backbuffer wrapper, no swapchain or host display, and emitted no SDL debug
  failure.

### SDLGPU-109 — execute the complete render-target family on D3D12 ✅

- **Problem/public behavior:** after texture/state/effect portability closed, D3D12 still had only
  scattered render-target evidence. Ordinary XNA target behavior spans much more than construction:
  2D/cube formats and properties, all faces, MSAA/depth combinations, preserve/discard semantics,
  SetData/GetData, target-to-target/backbuffer sampling, viewport/scissor restoration, pass order,
  first use and destruction after deferred draws. Passing one clear/readback probe could not cover
  those seams.
- **EasyGL/SDL evidence:** the live SDL GPU inventory already registers 33 render-target-related
  tests, including renderer-neutral parity sources and three EasyGL sources compiled unchanged
  against SDL GPU. The first D3D12 batch passed nine and exposed exactly two failures: the real NPOT
  mip bug fixed by `SDLGPU-107`, and the out-of-scope ShaderEffect coupling fixed by `SDLGPU-108`.
  This makes the remaining inventory a concrete verification task rather than inferred support.
- **Location:** no production change; existing SDL-specific, renderer-neutral parity and reused
  EasyGL render-target registrations through `SDLGPU-104`'s isolated cross-CTest transport.
- **Acceptance/test:** execute every relevant registration; require real pixel/byte or public
  property assertions and existing validation-fatal patterns; cover both MSAA and zero-MSAA,
  cube faces, MRT independent outputs, HDR formats, mip/readback/sampling, state restoration,
  multi-pass producer/consumer ordering and deferred lifetime; no host display or swapchain.
- **Result (2026-09-10):** the initial targeted batch contributed **9/9** green registrations;
  the two discovered failures now pass after `SDLGPU-107`/`108`. A second incremental build/run
  added **14/14**: cube plural binding/GetData/usage/MSAA-face, target lifetime, viewport, scissor,
  blend factor, sampling orientation, producer→consumer, backbuffer consumer, first use, Effect
  source and the MSAA/depth contract. The final **8/8** parity/oracle set covers backbuffer MSAA
  plus reset, generated target mips, HDR render targets, reused EasyGL 2D/cube property and cube
  depth-format sources, and viewport/scissor reset. The resulting family total is **33/33**.

  Builds named only the missing targets and used two compiler jobs; tests ran sequentially, each
  under a fresh Xvfb with SDL dummy video and the renderer's virtual backbuffer. Every CTest passed
  its vkd3d-proton engagement gate and emitted no configured SDL/validation failure pattern. This
  is observable off-screen D3D12 target parity; native D3D12 debug-layer validation and real
  presentation remain separately unclaimed in `SDLGPU-94`.

### SDLGPU-110 — execute the complete buffer/draw family on D3D12 ✅

- **Problem/public behavior:** D3D12's earlier stock-effect and instancing results did not cover
  the full ordinary draw surface. `DrawPrimitives`, `DrawIndexedPrimitives`, both user variants,
  16/32-bit indices, nonzero starts/offsets, dynamic replacement options, buffer readback/disposal,
  unusual semantic declarations, multi-stream input, primitive topologies and chronological replay
  each have independent failure modes in a deferred immutable-pipeline renderer.
- **EasyGL/SDL evidence:** thirteen EasyGL buffer/draw sources are compiled verbatim for SDL GPU;
  three renderer-neutral parity fixtures discriminate instancing, split streams and semantic
  declaration selection. Nine SDL-specific tests add real line/strip winding, descriptor limits,
  mixed SpriteBatch/3D ordering, deferred source ownership and upload-at-pass-boundary behavior.
  Together these sources are the existing mechanically classified oracle for this family.
- **Location:** no production change; existing shared EasyGL, parity and SDL-specific registrations
  through the isolated D3D12 CTest transport.
- **Acceptance/test:** execute every selected source with its original pixel/byte/exception
  assertions; retain 16/32-bit and buffered/user distinctions; require dynamic
  None/Discard/NoOverwrite, offsets/ranges, disposal, GetData, instancing/multistream, declaration,
  topology, ordering and lifetime checks; run sequentially without swapchain or host display.
- **Result (2026-09-10):** the shared set passes **16/16**: all thirteen unchanged EasyGL
  buffer/draw programs plus instanced draw, split multi-stream and semantic-declaration parity.
  The SDL-specific set passes **9/9**: baseline 3D, descriptor-capacity contract, line topology,
  front-face and triangle-strip winding, draw chronology, SpriteBatch↔3D order, deferred source
  lifetime and pass-boundary upload. Total D3D12 buffer/draw evidence is therefore **25/25**.

  Only the 24 previously absent binaries were built (the already-proven instancing executable was
  reused), with two compiler jobs; CTest ran one process at a time. Every process used a fresh Xvfb,
  SDL dummy video and the virtual backbuffer, authenticated vkd3d-proton engagement, and emitted no
  configured SDL/validation failure pattern. Native D3D12 debug-layer validation and presentation
  remain explicit `SDLGPU-94` boundaries.

### SDLGPU-111 — execute the complete model oracle family on D3D12 ✅

- **Problem/public behavior:** the stock-effect and draw sweeps exercised the primitives used by
  models, but did not prove the public `Model` orchestration that binds mesh-part buffers/effects,
  composes parent-bone transforms, preserves separate effects across meshes, consumes 32-bit mesh
  indices and feeds animated bones into `SkinnedEffect`.
- **EasyGL/SDL evidence:** five renderer-neutral-enough EasyGL programs are already reused verbatim
  by SDL GPU for exactly these seams. They are the classified model corpus; duplicating their
  scenes or inferring model support from lower-level triangles would be weaker evidence.
- **Location:** no production change; existing reused EasyGL model registrations through the
  isolated D3D12 CTest transport.
- **Acceptance/test:** run the unchanged model draw, child hierarchy, JSON 32-bit index, two-mesh
  independent-effect and skinned-animation-playback programs; require their existing transform,
  index, effect and pixel assertions; no host display or swapchain.
- **Result (2026-09-10):** all five incrementally built executables pass **5/5** through real
  `direct3d12`. The tests observe `Model.Draw`, a child mesh's absolute parent-bone transform,
  32-bit model indices, consecutive mesh parts retaining distinct effects, and
  `AnimationPlayer`-produced bones driving the now-portable `SkinnedEffect` palette. Each ran in a
  fresh Xvfb with SDL dummy video and the virtual backbuffer, passed the vkd3d-proton gate, and
  emitted no configured SDL/validation failure pattern. Native D3D12 debug-layer validation and
  presentation remain separate `SDLGPU-94` boundaries.

### SDLGPU-112 — execute the complete compiled-XNA-effect runtime on D3D12 ✅

- **Problem/public behavior:** the prior D3D12 evidence included compiled effects in the MRT and
  stock-effect paths, but did not execute the canonical renderer runtime suite. That left
  reflection, pass/state publication, malformed bytecode, clone isolation, sampler persistence,
  vertex-layout linking, every public draw route, SpriteBatch, render-target/cube/volume samplers
  and deferred sampled-resource lifetime inferred from narrower tests.
- **EasyGL/SDL evidence:** `SdlGpuCompiledEffectTests.cpp` is the SDL GPU implementation of the
  shared compiled-effect conformance contracts also exercised by EasyGL and the other capable
  backends. Its 39 cases use committed effect binaries and discriminating readback rather than
  treating successful parser or pipeline construction as render proof.
- **Location:** no production change; the canonical unit-test source is registered as a small,
  focused SDL GPU executable instead of requiring the unrelated all-renderer unit-test aggregate.
- **Acceptance/test:** execute the same source unmodified on native Vulkan and cross-compiled
  D3D12; require all 39 cases to pass, make configured validation diagnostics fatal, use the
  repository's isolated dummy/Xvfb/vkd3d wrapper, and do not touch the host display.
- **Result (2026-09-10):** native Vulkan passes **39/39** in 8.17 seconds with SDL offscreen video,
  an explicitly empty `DISPLAY`, a real Vulkan device and debug validation enabled. Windows
  D3D12 passes the identical **39/39** source in one 33.71-second CTest process through the
  authenticated vkd3d-proton route. The suite covers 13 runtime/reflection/state cases, four
  declaration/link cases and 22 draw/SpriteBatch/sampler/target/lifetime cases. No configured
  validation failure pattern was emitted. Only this source and its small test runner were built;
  the large `CnaRendererTests` aggregate was deliberately not compiled or relinked.

### SDLGPU-113 — preserve process-isolated lifecycle oracles on Windows ✅

- **Problem/public behavior:** the shared backbuffer, bound-target-lifetime and Present-lifecycle
  executables isolate each destructive or first-frame leg with `fork`/`exec` on Unix. Their Win32
  fallback instead ran most legs in one device. That invalidated first-read/initial-size premises,
  omitted two-frame legs and let one leg's state affect another; a Windows green result would not
  have been equivalent evidence.
- **EasyGL/SDL evidence:** the shared sources explicitly define process isolation as part of their
  oracle. The initial D3D12 all-in-one run demonstrated the defect: the 37x23 first leg caused the
  later 41x29/64x32/63x17/64x17/65x17 legs to retain stale storage, producing 28/29 and 20/28
  results even though every matching isolated leg passed.
- **Location:** Windows-only SDL GPU CTest registration; production renderer and canonical shared
  fixture sources remain unchanged. Native Unix retains one supervisor registration per source.
- **Acceptance/test:** give every Win32 backbuffer, bound-target and nonfatal Present leg its own
  executable process; exercise cross-frame G1/G2/A13; require the deliberate C2 unhandled refusal
  to exit nonzero; retain validation-fatal policy; rerun the complete set through isolated D3D12.
- **Result (2026-09-10):** all **33/33** backbuffer legs pass independently: eight dimension/
  resize cases, thirteen first-read/completion/row-pitch cases and twelve success/validation-order
  cases. All **18/18** bound-target lifetime and **22/22** Present-lifecycle legs also pass,
  including cross-frame resource retention, reset, MRT, cube, MSAA, repeated cycles and the
  deliberate C2 abnormal exit. The five unchanged EasyGL presentation oracles plus SDL GPU's
  synthetic surface test pass **6/6**, for **79/79** lifecycle/presentation registrations in this
  task's D3D12 sweep. Every process used the authenticated vkd3d-proton headless wrapper and no
  host display. Native registration remains five Unix supervisor tests; Windows registration is
  now 73 isolated legs, increasing its SDL GPU CTest inventory from 190 to 258 without adding or
  relinking another executable.

### SDLGPU-114 — execute transactional construction and lazy rollback on D3D12 ✅

- **Problem/public behavior:** SDL GPU defers or lazily acquires devices, shaders, command buffers,
  pipelines, samplers and fallback textures. The constructor exception-safety oracle was neither
  linkable as a small MinGW executable after modularization nor semantically valid in the forced
  headless D3D12 route: it required one window claim per successful construction and injected a
  failure into that skipped stage.
- **EasyGL/SDL evidence:** the existing renderer-specific oracle uses resource callbacks to prove
  balanced acquisition/release, retry after every failure, independent renderer instances,
  truthful depth/stencil capability and ShaderCross lifetime. In headless mode the production
  constructor intentionally performs no `SDL_ClaimWindowForGPUDevice`, while all other 32
  construction stages still execute.
- **Location:** constructor exception-safety fixture plus its target-local static archive link
  group; no production renderer change. The archive group contains input, graphics, SDL GPU and
  the compiled-effect bridge because those four form a real static-link cycle for this direct-
  renderer executable.
- **Acceptance/test:** skip only the inapplicable window-claim injection when the explicit test
  headless switch is active; require zero rather than one claim in every successful retry; retain
  the complete 33-stage swapchain oracle on native Vulkan; build and run both focused targets with
  validation diagnostics visible and no host display.
- **Result (2026-09-10):** isolated Windows D3D12 passes **291/291** checks across the 32 applicable
  construction failures, three lazy resource failures, two fallback-texture failures, independent
  instances, forced no-depth/stencil capability and ShaderCross construction. The same source with
  SDL offscreen video and a real Vulkan swapchain still executes the window claim and passes its
  full **299/299** matrix. Both are debug devices; neither emitted a configured validation failure
  or fixture failure. Only the fixture source and its focused executable were rebuilt.

### SDLGPU-115 — make every SDL GPU integration registration validation-fatal ✅

- **Problem/public behavior:** only selected high-risk SDL GPU CTests rejected Vulkan validation
  diagnostics, and none centrally recognized the standard `D3D12 ERROR:` / `D3D12 WARNING:`
  debug-layer prefixes. A passing pixel assertion could therefore conceal invalid API usage in an
  older or newly added fixture.
- **EasyGL/SDL evidence:** SDL GPU creates debug devices in these Debug configurations. The
  existing per-test policy already treats `Validation Error`, `Validation Warning` and bare
  `VUID-` output as failure, but it was repeated on only part of the inventory and was written for
  Vulkan output. Direct3D 12's debug layer uses the separate `D3D12 ERROR/WARNING:` form.
- **Location:** renderer-local CTest registration only; no production renderer or shared test
  helper change.
- **Acceptance/test:** append, rather than overwrite, a common Vulkan/D3D12 failure policy to every
  test registered in the SDL GPU examples directory; verify mechanically in both stable generated
  test inventories; do not rebuild or relink unchanged binaries.
- **Result (2026-09-10):** the directory-final policy appends all three expressions to every
  current and future SDL GPU registration while preserving focused historical expressions. CMake
  JSON inventory checks report **191/191 native** and **258/258 Windows** tests containing
  `Validation (Error|Warning)`, `VUID-` and `D3D12 (ERROR|WARNING):`; zero registrations are
  missing the policy. Regeneration changed no compiled input, so this task performed no rebuild or
  relink.

### SDLGPU-116 — isolate SDL GPU tests from unrelated host display transports ✅

- **Problem/public behavior:** the Linux stable tree correctly selected SDL's offscreen video
  driver, but every generated test still carried the cached EasyGL Xvfb value `DISPLAY=:179`.
  SDL offscreen ignored it, yet the registration did not make that isolation auditable. A fresh
  native Windows configuration also inherited the Linux-specific `x11` default before the cross-
  Wine override replaced it.
- **EasyGL/SDL evidence:** EasyGL needs an X server on this host; SDL GPU's Vulkan offscreen path
  creates a real window, swapchain and GPU device without one. SDL's native driver names are
  platform-specific (`windows`, `android`, `cocoa` and `uikit`), while only the X11 selection needs
  `CNA_TEST_DISPLAY`.
- **Location:** renderer-local test environment/default selection; no production renderer change.
- **Acceptance/test:** select a valid default driver for each native target; explicitly clear
  `DISPLAY` and `WAYLAND_DISPLAY` for offscreen/dummy runs; preserve X11's configured virtual-
  display route and the Windows cross wrapper's fresh per-test Xvfb; mechanically inspect all
  native registrations and execute a real Vulkan smoke without any host display variable.
- **Result (2026-09-10):** fresh Windows, Android, macOS and iOS/tvOS configurations now default to
  `windows`, `android`, `cocoa` and `uikit`; Unix desktop retains `x11`. The stable Linux inventory
  reports **191/191** SDL GPU registrations with `SDL_VIDEODRIVER=offscreen`, empty `DISPLAY` and
  empty `WAYLAND_DISPLAY`, with no nonempty display transport. Validation-fatal real-Vulkan smoke
  then passes **30/30**, including a real swapchain and 60 `Clear()+Present()` frames. Stable
  Windows registrations remain SDL dummy/forced-headless and their launcher alone creates an
  isolated Xvfb for Wine adapter enumeration. CMake regeneration required no rebuild or relink.

### SDLGPU-117 — perform the post-portability adversarial closeout sweep ✅

- **Problem/public behavior:** the prior Linux closeout recorded 188 integration and 42 focused
  renderer-unit tests before the portability work added registrations and unit cases. A final
  verdict must rerun the live inventory and repeat the contract/example/source audit rather than
  carrying those historical totals forward.
- **EasyGL/SDL evidence:** the machine-checkable inventories contain 226 common renderer hooks and
  246 EasyGL examples. The SDL GPU directory now registers 191 native tests, 112 carrying the
  explicit `Parity` label, and the focused renderer aggregate contains 47 `SdlGpu*` cases.
- **Location:** generated test inventories, renderer-local source audit and this authoritative
  status/ledger; no production code change.
- **Acceptance/test:** execute every native SDL GPU integration test with the isolated offscreen
  environment and global validation-fatal policy; execute every focused SDL GPU renderer-unit
  case; rerun both mechanical audits; search ignored parameters, inherited defaults, unsupported
  paths, null returns, hard-coded formats/state and cache/lifetime seams; state every remaining
  difference and environment blocker precisely.
- **Result (2026-09-11):** the stable incremental Vulkan tree passes **191/191** SDL GPU integration
  tests sequentially in **159.19 s**, including all **112** parity-labelled registrations and all
  32 shared renderer-neutral fixtures. It then passes **47/47** `SdlGpu*` renderer-unit cases.
  No build or relink was needed. Every integration registration had empty X11/Wayland display
  variables and the common Vulkan/D3D12 fatal-diagnostic expressions; no validation diagnostic was
  emitted. The contract checker remains **226 classified** (`141 =`, `80 out`, five query hooks
  `⛔` under `SDLGPU-80`). The example checker remains **246 classified** (128 covered, 52 direct,
  two query-limit examples, three EasyGL defects, nine EasyGL-specific, 52 modern-out and zero
  unclassified/new-test-needed). The source sweep found no new ordinary-XNA difference: ignored
  requested depth-format parameters return the actually applied device format; cube-face MRT is
  refused at the same seam in EasyGL; custom ShaderEffect instancing/uniform arrays are CNAEXT;
  all remaining null/unsupported paths have explicit capability or validation coverage.

  The exact renderer-wide result is therefore still verdict **B**, solely because the three open
  platform evidence tasks cannot be executed in this environment: real D3D12 swapchain recovery
  needs DRI3 or a real Windows runner (`SDLGPU-94`), Metal needs an Apple SDK/device (`SDLGPU-95`),
  and Android still needs its toolchain and the owner-confirmed emulator to be located/configured
  for the deferred runtime run (`SDLGPU-96`). On the complete Linux/Vulkan runtime
  surface and the extensively exercised windowless D3D12 surface, no remediable classic-XNA parity
  gap remains. The only known EasyGL capability differences are exact half-rate
  `PresentInterval::Two` and `OcclusionQuery`, both rigorously documented underlying SDL_gpu API
  limits; non-default `MultiSampleMask` is an SDL_gpu limit but not an EasyGL difference because
  EasyGL does not implement it either.

### SDLGPU-118 — retain compiled-effect shaders across deferred replay ✅

- **Problem/public behavior:** an ordinary `Draw*` call or `SpriteBatch.End()` can accept a draw
  whose SDL_GPU encoding happens only when the frame or target is later consumed. The queued
  binding copied raw `SDL_GPUShader*` handles, but `Effect::Dispose` and `Effect::~Effect` reset the
  owning compiled runtime immediately. Destroying the effect between queue and replay could
  therefore invalidate an already-issued draw, contrary to XNA's chronological draw semantics.
- **EasyGL/SDL evidence:** EasyGL binds the compiled runtime and executes `draw_arrays`,
  `draw_elements` or the SpriteBatch pass loop synchronously, before the public call returns. In
  SDL GPU, `BuildCompiledEffectBindingEXT` returned only native handle values;
  `SdlGpuCompiledEffect::~SdlGpuCompiledEffect` calls `MOJOSHADER_deleteEffect`, whose effect
  backend calls `MOJOSHADER_sdlDeleteShader`. At the last reference the vendored SDL_gpu adapter
  removes linker-cache programs and calls `SDL_ReleaseGPUShader` for both native modules. The
  initial Vulkan regression happened to retain enough deferred SDL state to draw the right pixel,
  but there was no ownership edge permitting pipeline creation from those released handles; that
  driver-dependent survival is not a valid renderer contract.
- **Location:** renderer-local `CompiledEffectBinding`, `BuildCompiledEffectBindingEXT` and
  `SdlGpuRenderer` teardown ordering; SDL GPU compiled-effect lifetime tests. No shared XNA API or
  modern CNAEXT surface changes.
- **Acceptance/test:** retain the exact MojoShader vertex/pixel records used by each queued binding;
  acquire ownership only after every binding operation that can throw; keep it through a failed
  submit/retry and release it after a successful submit or orderly renderer teardown; ensure the
  MojoShader context outlives commands and compiled pipelines; pixel-test both public ordinary
  draw destruction and explicit SpriteBatch effect disposal before replay on Vulkan and D3D12.
- **Result (2026-09-11):** each binding now owns a shared `CompiledEffectShaderLease` that balances
  `MOJOSHADER_sdlShaderAddRef` with `MOJOSHADER_sdlDeleteShader`. Copies made while queueing a
  SpriteBatch command share one lease; rejected/incomplete queue construction is exception-safe;
  commands retained after a failed submit retain the lease; successful command clearing releases
  it only after SDL has accepted the command buffer. Renderer teardown now clears deferred draws,
  releases compiled pipelines and only then destroys the MojoShader context, all before destroying
  the SDL_GPU device. Two new 8x8 readback tests destroy or explicitly dispose the sole public
  `Effect` after queueing and recover the compiled shader's discriminating colour; a third leaves
  the draw and target unconsumed so device teardown exercises the corrected context order. The
  focused compiled-effect source passes **42/42** on native offscreen Vulkan and headless real
  D3D12; the Vulkan `SdlGpu*` renderer aggregate passes **50/50**. The D3D12 CTest retained the
  global fatal debug-diagnostic policy, and neither run emitted a validation failure.

### SDLGPU-119 — prove compiled-effect SpriteBatch cannot outlive its texture wrapper ✅

- **Provenance — this row records a finding, not a verified defect.** It was stated by the
  autonomous `cnasdlgpu` session at 2026-09-10T22:33:50Z, immediately before that session stopped
  mid-task while building the regression target for it. No test, no reproduction and no fix exist
  yet, and the claim has not been checked against the source by anyone else. It is written down
  here only so that it does not exist solely inside a session rollout log
  (`~/.codex/sessions/2026/09/09/rollout-2026-09-09T19-35-03-01a0873c-c9e7-7620-8ae3-6bb41ddea3a0.jsonl`),
  which is where it was when `SDLGPU-118` was pushed. Recorded by the `cnanextmerge` session on
  2026-09-11 at the project owner's instruction.
- **The finding, verbatim as stated (Czech, as written):**

  > Další konkrétní nález: compiled-effect SpriteBatch má ještě jednu mezifrontu mezi `Draw` a
  > `End`. Ta sice drží životnost nativní textury, ale zároveň si nechává holý `ITextureRenderer*`
  > a při `End` z něj znovu čte rozměry/formát. FNA v tomto místě drží spravovanou referenci
  > `Texture2D`; v CNA tedy zánik texture wrapperu po `Draw` může způsobit UAF navzdory tomu, že
  > GPU resource stále žije. Zakládám `SDLGPU-119` regresí, která texture wrapper zničí před `End`.

- **Translation of the claim:** the compiled-effect SpriteBatch path has a second intermediate
  queue between `Draw` and `End`. That queue keeps the NATIVE texture alive, but it also retains a
  bare `ITextureRenderer*` and re-reads dimensions/format from it during `End`. FNA holds a managed
  `Texture2D` reference at the same point, so in CNA destroying the texture WRAPPER after `Draw`
  may be a use-after-free even though the GPU resource itself is still alive.
- **Relationship to `SDLGPU-118`:** that task fixed the analogous lifetime edge for compiled
  *shaders* (`CompiledEffectShaderLease`). This is the same shape one level over: the queue owns
  the GPU resource but not the wrapper object it later reads metadata from. `SDLGPU-118`'s own
  evidence notes that driver-dependent survival is not a valid renderer contract; the same
  argument would apply here.
- **Acceptance/test (as the session intended it):** a regression that destroys the public
  `Texture2D` wrapper after `Draw` and before `End` on the compiled-effect SpriteBatch path, and
  shows either a correct draw or an explicit refusal rather than reading freed memory. Run under a
  sanitizer, because a plain pixel check can pass on freed-but-unreused memory — the same trap
  `SDLGPU-118` documented.
- **VERDICT (2026-09-11): REFUTED as a defect, and the invariant it depends on is now pinned.**
  The structural half of the claim is exactly right and was worth raising: `PendingSpriteEXT`
  really does hold a bare, non-owning `const ITextureRenderer* texture` beside an owning
  `SdlGpuSampledTextureEXT nativeTexture`, and `FlushPendingCompiledSpritesEXT` really does
  dereference it — `QueueSprite(*sprite.texture, ...)` — from inside `End()`. What the claim missed
  is where CNA holds its managed reference. It is not on the `Texture2D` wrapper, as FNA's is; it
  is one level down:

  1. `SpriteBatch::pushSprite` takes `texture.GetRendererWeak().lock()` — a real
     `shared_ptr<ITextureRenderer>` copy — and stores it in `SpriteInfo::texture` at Draw time.
  2. In every non-Immediate mode that sprite sits in `spriteQueue_` until `End()`.
  3. `SpriteBatch::End()` runs `flushBatch()`, then `renderer_->End()`, and only then
     `spriteQueue_.clear()`. Its own comment says why: *"Deferred renderers may submit their final
     texture group only from End(). Retain every queued texture renderer through that call, then
     release the queue."*

  So the renderer's `Draw` is called from inside `End()` while the strong references are still
  held, `FlushPendingCompiledSpritesEXT` replays inside the same `End()`, and the bare pointers
  are covered for exactly as long as they can be dereferenced. Destroying the public `Texture2D`
  between `Draw` and `End` frees the wrapper and not the `ITextureRenderer`.

  The `!immediateMode_` guard closes the other half: in Immediate mode nothing is queued at all,
  because the renderer's `Draw` runs inside `SpriteBatch::Draw` while the caller's own wrapper is
  necessarily alive.

- **Why this still produced work rather than a one-line dismissal.** The guarantee is a
  CROSS-MODULE invariant: it is provided by `Microsoft::Xna::Framework::Graphics::SpriteBatch` and
  depended on by every whole-frame-deferred renderer, and **nothing enforced it or even recorded
  it**. Reordering three lines in `SpriteBatch::End()` would have reintroduced the
  use-after-free this row describes, in every deferred renderer at once, with no test failing.
  `PendingSpriteEXT::texture` now documents what it depends on, and
  `SpriteBatchTextureLifetimeTest` (3 tests, `modules/graphics/tests/.../SpriteBatchTests.cpp`)
  pins it.

- **Test evidence, including the version that did not work.** The first draft used the existing
  `RecordingSpriteBatchRenderer`, which copies what it needs at Draw and never looks at the texture
  again — so it cannot observe this contract, and **all three tests passed with the defect
  injected**. They were rewritten against a `DeferringSpriteBatchRenderer` shaped like
  `PendingSpriteEXT`: it stores the bare pointer at Draw and reports at `End()` whether the object
  is still alive. With `spriteQueue_.clear()` moved before `renderer_->End()`, the two deferred
  tests now FAIL with *"the ITextureRenderer was freed before the renderer's End() replayed the
  sprites that point at it"*, and the Immediate test correctly stays green because it does not
  depend on the retention. Each test also asserts the texture renderer IS released after `End()`
  returns, so none of them can pass on a leak.

- **Device-level evidence, and whose it is.** The autonomous `cnasdlgpu` session had already
  written the end-to-end regression before it was cut off — `SdlGpuCompiledEffectDrawTest.`
  `DeferredCompiledSpriteSurvivesTextureDestructionBeforeEnd`, 38 lines left UNCOMMITTED in the
  working tree. **That work is kept and is not rewritten.** It draws through a compiled Effect in a
  Deferred batch, destroys the `Texture2D` between `Draw` and `End`, and reads the rendered pixel
  back from a `RenderTarget2D`, so it proves the draw genuinely completed rather than merely not
  crashing. Verified 2026-09-11 on the real `SDL_GPU` renderer (`cmake-build-sdlgpu`, Vulkan
  backend, headless Xvfb): it PASSES, and the whole
  `cna_test_sdlgpu_compiled_effect_runtime` binary is **43/43**.

  It was found only because a `git status` in this worktree fails on a broken `SDL_image` submodule
  path and silently reports nothing; `--ignore-submodules=all` shows it. Worth knowing before
  trusting a clean-tree claim here.

- **ASan follow-up (2026-09-11):** the renderer/replay boundary was rebuilt with AddressSanitizer
  instrumentation in `SdlGpuRenderer.cpp` and the compiled-effect regression translation unit,
  while reusing the stable build's unchanged libraries to avoid an unnecessary whole-repository
  rebuild. With `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1`,
  `SDL_VIDEODRIVER=offscreen`, `DISPLAY=` and `WAYLAND_DISPLAY=`, the focused destruction-before-
  `End` regression passes on real Vulkan, then the complete binary passes **43/43** with no ASan or
  Vulkan-validation diagnostic. Leak detection was intentionally outside this focused address-
  lifetime check; the stronger proof of the positive lifetime invariant remains the deferring
  cross-module test double and its injected-ordering failure described above.

### SDLGPU-120 — classify the EasyGL letterbox failure without copying it ✅

- **Problem/public behavior:** the newly reported
  `PresentationRectangleTest.ALetterboxedDefaultViewportIsNotACustomSubViewport` failure appeared
  adjacent to SDL GPU's presentation work and had no entry in this parity plan. A full-logical-size
  sprite must fill the physical letterbox rectangle while `GetBackBufferData` addresses every
  physical backbuffer row.
- **EasyGL/SDL evidence:** the same unchanged test was run from the existing stable binaries, with
  no rebuild. OPENGL33 on SDL's offscreen video driver reproduces the reported 800x480/240x240
  failure and prints the apparent `(0,245)-(792,476)` non-clear bounds. SDL GPU on offscreen Vulkan
  reports the same `(160,0,480x480)` presentation rectangle, renders within the coarse expected
  `(168,0)-(636,476)` sample bounds and passes. The scanner was then hardened to count only pixels
  matching the ink, not every value different from the clear colour. A one-translation-unit
  alternate EasyGL relink now reports `EMPTY` rather than inventing ink from zero reads and retains
  the expected centre failure; the incrementally rebuilt SDL test retains its same passing bounds.
  Neither route used the host display.
- **Diagnosis:** this is two independently observable EasyGL defects. The stock EasyGL sprite flush
  compares the mapped physical viewport with the full target extent and therefore calls a default
  letterbox rectangle “custom”; it then divides logical sprite coordinates by that physical
  viewport size. Separately, EasyGL `ReadBackbuffer` obtains its default-framebuffer Y-flip height
  from logical `GetViewportSize`, so reads at `y >= 240` ask GL for negative rows and leave zeroes
  in the caller buffer. Those zeroes differ from the chosen clear colour and were incorrectly
  reported by the test's coarse diagnostic scanner as the sprite's bounds; source inspection proves
  that description was not a measurement of ink. The compiled-effect EasyGL sprite route also
  resets the default viewport to the entire physical drawable, a distinct wrong treatment of the
  same default presentation rectangle.
- **Location/disposition:** `EasyGLSpriteBatchRenderer::FlushBatch`,
  `FlushBatchWithCompiledEffect` and `EasyGLRenderer::ReadBackbuffer`; recorded as
  `EASYGL-PARITY-8/9` and corrected in `misc/known_bugs.md`. The shared diagnostic now matches the
  ink colour explicitly. No SDL production change is appropriate: `SDLGPU-68` already implements
  and pixel-verifies the logical-projection/physical-viewport split, and the live public
  discriminator remains green.
- **Acceptance/test:** reproduce both renderer outcomes without the host display; trace the public
  logical viewport mapping, native sprite projection and physical backbuffer readback separately;
  document a future EasyGL-local correction rather than changing SDL to match a reference defect.
- **Result (2026-09-11):** accepted with the exact one-test A/B, the corrected negative diagnostic
  and source-level proof of both causes. The SDL parity matrix remains `=` for logical presentation,
  default/custom viewport discrimination and backbuffer readback. The EasyGL defect ledger grows
  from seven findings/six distinct families to nine findings/eight families; the example-
  classification CSV is unchanged because this evidence is renderer-neutral rather than one of
  the 246 EasyGL example sources. Only `CnaGraphicsTests` was requested from the SDL build; the
  EasyGL confirmation compiled one changed test object and relinked an alternate binary from its
  unchanged stable objects instead of triggering the pending 134-object configuration cascade.
  On the same machine and in the same minute, the unchanged
  `PresentationRectangleTest.ALetterboxedDefaultViewportIsNotACustomSubViewport` exited 0 from
  `cmake-build-sdlgpu/CnaGraphicsTests` (`SDL_GPU`) and exited 1 from the stable
  `cmake-build-debug` OPENGL33 binary.
- **Already repaired on `next`; do not cherry-pick here.** Substantive commit `2f3dca5f9` compares
  against `GetDefaultViewportRect()` and hardening commit `7bcf3f496` records default-versus-custom
  when the viewport is set instead of inferring it from stale GL state. A cross-branch check made
  for this follow-up originally measured **439** commits in `next` but absent from `origin/sdlgpu`;
  the local refs now measure **440** after another `next` documentation commit landed. That moving
  distance includes merge `93ca4ffdf`, which changed the adjacent EasyGL sprite flush and sampler
  path. Copying the two fixes onto this older EasyGL would create a stale fork and a future merge
  conflict. Integrating `next` into `sdlgpu` is the honest direction and remains the repository
  owner's sequencing decision; this branch records the provenance and makes no EasyGL change.

### SDLGPU-121 — apply stock sampler LOD bias in shaders on every SDL_gpu driver ✅

- **Problem/public behavior:** ordinary `SamplerState.MipMapLevelOfDetailBias` must add to the
  implicit mip level selected by SpriteBatch and every stock XNA effect. The renderer captured the
  value correctly and put it in `SDL_GPUSamplerCreateInfo::mip_lod_bias`, but the vendored SDL 3.5
  contract explicitly says that member is a no-op for the Metal driver and that Metal requires
  shader-side bias. The prior platform verdict therefore overstated the stock route on Apple.
- **EasyGL/SDL evidence:** desktop EasyGL applies `GL_TEXTURE_LOD_BIAS` and the shared oracle proves
  bias 0/+1/+2 select deliberately different authored mip colours. SDL GPU passed that oracle on
  Vulkan and D3D12 only because those drivers consume the native descriptor field. The exact
  vendored `SDL_gpu.h` note at the sampler structure is the underlying evidence for Metal's
  divergence; WebGPU's existing SPIR-V/WGSL implementation independently proves that an explicit
  fragment-sample Bias operand is a reasonable renderer-side emulation.
- **Implementation/location:** all seven sampled stock fragment programs now declare a fixed
  eight-slot bias block in SDL_gpu fragment-uniform set 3 and every implicit 2D/cube lookup uses
  its own slot's explicit Bias operand. Replay pushes the already captured command values for
  stock SpriteBatch, BasicEffect (unlit/lit and SkinnedEffect's shared shader), AlphaTestEffect,
  DualTextureEffect's two independent slots and EnvironmentMapEffect's base/cube slots. Existing
  PbrEffect/SkinnedPbrEffect support receives the same treatment for all seven material samplers so
  changing the shared sampler creation path cannot regress that CNA-specific route. Native sampler
  bias is zero only for these shader-emulated paths, preventing a double application on Vulkan and
  D3D12. The custom ShaderEffect route retains its native sampler path; the compiled-effect route
  subsequently gained the same cross-driver shader semantic in `SDLGPU-122`.
- **Acceptance/test:** regenerate checked-in SPIR-V; require all 26 construction shaders to reflect
  with their exact updated layouts through the forced ShaderCross constructor route; byte-verify
  mip selection for positive, zero and negative bias on stock 3D while a test-only guard rejects
  every nonzero native descriptor bias (so Vulkan/D3D12 cannot make the proof vacuous), and verify
  SpriteBatch independently; rerun the stock
  effect/multi-slot sampler family; execute the exact LOD fixture through both real Vulkan and the
  cross-compiled D3D12 backend; reject validation/debug diagnostics.
- **Result (2026-09-11):** the unchanged `parity_sampler_lod_bias` discriminator passes **7/7** on
  offscreen Vulkan and **7/7** through the authenticated headless Wine/vkd3d-proton D3D12 route:
  bias 0/+1/+2 selects levels 0/1/2 and -1 clamps to level 0. Both CTest registrations enabled the
  native-bias rejection guard, so neither backend could satisfy the result through its descriptor
  field. The independent SpriteBatch fixture,
  constructor exception/forced-ShaderCross matrix and five stock-effect/dual/environment sampler
  contracts all pass on Vulkan (**7/7 CTests** in the focused companion run), with no Vulkan
  validation diagnostic. D3D12 emits no wrapper-fatal debug-layer warning/error and confirms that
  ShaderCross preserves the explicit SPIR-V Bias operand. No host display was used.

### SDLGPU-122 — emulate compiled-XNA-effect LOD bias on Metal ✅

- **Problem/public behavior:** an ordinary compiled XNA Effect may declare
  `MipMapLevelOfDetailBias` in its pass sampler state, and the same pass may be used by buffered,
  user, instanced or SpriteBatch draws. `BuildCompiledEffectBindingEXT` captures the correct value,
  but MojoShader emits implicit sample instructions and replay supplies the value only through
  native `SDL_GPUSamplerCreateInfo::mip_lod_bias`. SDL documents that field as a Metal no-op, so
  compiled effects still silently ignore this public state on that driver after SDLGPU-121.
- **EasyGL/SDL evidence:** desktop EasyGL and SDL GPU Vulkan/D3D12 pass the shared compiled-effect
  sampler pixel contract, including its authored-mip bias section. SDL_gpu's own header is direct
  negative evidence for Metal. The shared `SpirvSamplerLodBias` transformer used by WebGPU already
  rewrites `OpImageSampleImplicitLod` per D3D9 sampler register and supplies a renderer-owned bias
  UBO, so absence of a native Metal sampler field is not a reasonable-impossibility result.
- **Implementation/location:** the pinned MojoShader SDL_gpu adapter/link boundary, the shared
  `modules/renderers/common/mojoshader` SPIR-V bias transformer and
  `SdlGpuRenderer::BindCompiledEffectForDrawEXT`. The adapter now accepts a transform callback only
  before its first link, reports whether the bound pixel program was rewritten and, when static
  ShaderCross is present, selects SPIR-V even on Apple so the Metal route cannot bypass the rewrite
  through the direct MSL profile. `InjectSamplerLodBias` now understands both MojoShader's original
  combined bindings (`binding=register`) and WebGPU's split bindings, with independent sampler and
  uniform descriptor sets. SDL replay pushes a captured 256-byte register block at fragment UBO
  slot 1 and zeroes native bias only when the linked module consumes that block. No public Effect
  API or modern ShaderEffect surface changed.
- **Acceptance/test:** insert a bias uniform/rewrite before the Metal ShaderCross/native-shader
  route diverges; preserve sampler-register identity, shader lifetime and reflected resource
  counts; make the existing shared compiled-effect sampler pixel contract pass with forced shader
  emulation on Vulkan (so released/unreused memory or an unavailable Apple device cannot make the
  proof vacuous); retain the Vulkan and D3D12 native-route results and validation cleanliness. A
  real Metal run remains part of SDLGPU-95's platform evidence, not a prerequisite for testing the
  transformation semantics locally.
- **Result (2026-09-11):** complete. The exact `SdlGpu_CompiledEffectRuntime` registration passes
  **43/43** on offscreen Vulkan and **43/43** through the repository's authenticated headless
  Wine/vkd3d-proton D3D12 route. Both runs set
  `CNA_SDLGPU_TEST_REJECT_NATIVE_LOD_BIAS=1`; the shared sampler pixel contract's bias +1 selects
  authored mip 1, so neither backend's working native descriptor could make the test pass. The
  independent `MojoShaderSpirvSamplerLodBiasTest` passes **2/2** and walks all 27 committed effect
  passes: original combined and WebGPU-split representations rewrite the same nonzero sample
  count, every attributable sample reads its own D3D9 register, and the multi-register passes stay
  distinct. ShaderCross reflection accepted the extra fragment UBO on D3D12; direct Vulkan accepted
  the explicit two-buffer resource count. No validation or wrapper-fatal diagnostic appeared and
  no host display was used. Metal hardware execution remains honestly open under `SDLGPU-95`, but
  the previously missing renderer-side semantic and its cross-format translation path are now
  implemented and locally discriminated.

### SDLGPU-123 — isolate RenderTargetCube SpriteBatch coordinates from presentation scaling ✅

- **Problem/public behavior:** `GraphicsDevice::SetRenderTarget(RenderTargetCube*, face)` resets the
  public viewport to that face's dimensions, and an ordinary `SpriteBatch` destination is expressed
  in those target-local pixels. `SdlGpuRenderer::QueueSprite`, however, treats only an active
  `RenderTarget2D` as offscreen. With a cube face bound it enters the backbuffer logical/physical
  conversion and rescales the face viewport whenever the window uses a different logical size.
- **EasyGL/SDL evidence:** EasyGL's SpriteBatch reads the live GL viewport and uses its dimensions
  directly when that viewport differs from the physical backbuffer, so its cube-face path remains
  target-local. SDL GPU already handles 2D targets this way and already knows the bound cube's
  exact size elsewhere in the same method; the missing cube branch is renderer-local, not an
  SDL_gpu limitation.
- **Location:** `SdlGpuRenderer::QueueSprite` and the existing real Vulkan cube/SpriteBatch
  compatibility executable.
- **Acceptance/test:** under deliberately mismatched logical and physical backbuffer sizes, draw
  an 8x8 sprite into the top-left quarter of a 16x16 cube face and byte/tolerance-verify both a
  covered interior pixel and an untouched opposite-corner pixel. The test must fail before the
  fix, pass after it, keep the existing 2D/backbuffer/viewport legs green and emit no validation
  diagnostic.
- **Result (2026-09-11):** complete. `QueueSprite` now gives an active cube face the same explicit
  offscreen-target treatment as `RenderTarget2D` and applies logical/physical presentation scaling
  only when neither target kind is bound. Before the fix the focused offscreen Vulkan test passed
  every existing **24/24** check but failed the new opposite-corner discriminator (**24/25**
  overall): the 8x8 sprite incorrectly covered the far corner of its 16x16 cube face. After the
  fix the executable passes **25/25** on both Vulkan and the authenticated headless D3D12 route.
  The native 11-test SpriteBatch/presentation companion slice also passes. No Vulkan validation or
  D3D12 wrapper-fatal diagnostic appeared and no host display was used.

### SDLGPU-124 — give compiled pipelines a non-recyclable program identity ✅

- **Problem/public behavior:** `SdlGpuRenderer::compiledEffectPipelines_` outlives individual public
  `Effect` objects, but its key identified the compiled program only by the two raw
  `SDL_GPUShader*` wrapper addresses. Destroying an effect removes its MojoShader linker-cache
  program and releases those wrappers while the already-created immutable renderer pipeline stays
  cached. A later ordinary XNA compiled effect can therefore receive the same allocator addresses
  for a different shader pair/layout and be sent through the stale cached pipeline, producing the
  previous effect/pass's pixels.
- **EasyGL/SDL evidence:** EasyGL makes its GL program ready synchronously from the live
  `EasyGLCompiledEffect`; it has no separate renderer-global immutable-pipeline map keyed by freed
  native wrapper addresses. Vendored SDL commit `cbe3fbe9f367340dcd924de29c225c9f4ffea1f5`
  makes the platform distinction concrete: Vulkan pipelines increment both `VulkanShader`
  reference counts and thus incidentally pin the addresses, while D3D12 `ReleaseShader` frees its
  bytecode/wrapper immediately and Metal clears the native objects and frees its wrapper
  immediately. Neither freed address is a durable semantic identity. The first bounded pre-fix
  runtime probe honestly did **not** reproduce address recycling in 64 generations on either
  Vulkan or Wine/D3D12 and was removed; allocator luck is not used as defect proof or acceptance.
- **Implementation/location:** the pinned MojoShader SDL_gpu adapter now assigns every newly linked
  program a non-zero monotonically increasing identity owned by its context, preserves that value
  on linker-cache hits and exposes it through `MOJOSHADER_sdlGetProgramIdentity`. The renderer
  snapshots it beside the deferred shader lease and folds it into `GetOrCreatePipelineCompiledEffect`
  instead of either native wrapper address. The remaining vertex-buffer/attribute description and
  complete render-state/target identity stay in the key unchanged. A missing identity is rejected
  before a draw can enter the deferred queue. No public XNA or modern CNAEXT graphics API changed.
- **Acceptance/test:** repeatedly destroy and recreate the same compiled effect while alternating
  two pass programs with distinct channel swizzles; require a new non-zero identity after every
  program destruction, require a same-pass relink/cache hit to preserve identity, and pixel-verify
  every generation. Run the entire compiled-effect corpus on real offscreen Vulkan and the
  authenticated headless D3D12 driver, with fatal renderer diagnostics still enabled. Reconfigure
  twice to prove the new MojoShader patch remains an idempotent member of the pinned series.
- **Result (2026-09-11):** complete. The new regression runs eight destroy/recreate generations,
  queues the chosen pass twice per generation, checks all identity invariants and distinguishes
  identity `(51,128,204)` from swizzled `(128,204,51)` output. It passes on Vulkan and D3D12; the
  full `cna_test_sdlgpu_compiled_effect_runtime` binary is now **44/44** on both. The first configure
  applied all 14 pinned patches and the next reported the exact series already applied. The
  non-production native-reference ratchet remains at its previous ceiling. No Vulkan validation,
  D3D12 wrapper-fatal diagnostic or host-display access occurred.

### SDLGPU-125 — retire compiled pipelines with their last Effect/draw lease ✅

- **Problem/public behavior:** the renderer-global `compiledEffectPipelines_` cache retained every
  immutable pipeline until `GraphicsDevice` destruction, even after the public `Effect`, its native
  shaders and the linked-program identity had all become unreachable. An ordinary long-running XNA
  title that repeatedly creates, draws with and destroys compiled Effects (for example during
  content reload) therefore grows native GPU-pipeline ownership monotonically instead of returning
  to its steady-state footprint.
- **EasyGL/SDL evidence:** `EasyGLCompiledEffect::~EasyGLCompiledEffect` deletes the native effect;
  MojoShader's effect destructor deletes its shaders, and `MOJOSHADER_glDeleteShader` removes every
  linker-cache program that refers to the deleted shader. SDL GPU already released effect-owned
  shaders but kept its separate immutable pipeline objects. A deterministic pre-fix regression
  observed cache cardinality grow **1, 2, ... 8** across eight create/draw/destroy generations and
  remain nonzero after every `Effect::Dispose`; this is resource ownership evidence rather than an
  allocator- or pixel-dependent inference.
- **Implementation/location:** `SdlGpuCompiledEffect` now obtains one strong renderer cache lease
  for each linked-program identity it actually uses. Every `CompiledEffectBinding` snapshots a
  shared copy for deferred ordinary and SpriteBatch replay. The renderer keeps only weak canonical
  leases and indexes pipelines first by the exact 64-bit identity, then by that program's immutable
  state/layout hash. Releasing the final Effect/queued-draw lease evicts and releases every pipeline
  for that exact program. A renderer-owned weak lifetime state makes a late Effect destructor
  harmless after renderer teardown. No public XNA or modern CNAEXT graphics API changed.
- **Acceptance/test:** require one reused cache entry while each generated Effect remains live and
  exactly zero after its destruction; retain the existing distinct program-identity and pixel
  discriminators; destroy an Effect before replay for both ordinary and compiled SpriteBatch draws,
  verify their pixels, then require the queued lease to retire the pipeline. Also preserve safe
  renderer destruction with an abandoned queued draw. Run the focused lifetime slice and complete
  44-case compiled-effect corpus on real offscreen Vulkan and authenticated headless D3D12 with
  fatal driver diagnostics enabled.
- **Result (2026-09-11):** complete. The pre-fix cache sequence above fails the new exact lifetime
  assertions; after the lease implementation the focused four-case Vulkan slice passes **4/4**,
  each live generation reports one pipeline, and every post-destruction/replay observation reports
  zero. The complete compiled-effect executable passes **44/44** on Vulkan and D3D12; the focused
  D3D12 lifetime slice also passes after the final exact-key cleanup. No Vulkan validation,
  D3D12 wrapper-fatal diagnostic, host-display access or unnecessary full rebuild occurred.

### SDLGPU-126 — bound immutable sampler retention across ordinary state churn ✅

- **Problem/public behavior:** `SamplerState.MaxMipLevel` is an unrestricted ordinary-XNA integer
  and `MipMapLevelOfDetailBias` is an unrestricted float. SDL GPU needs immutable native sampler
  descriptors, but its renderer-global cache retained every distinct description until
  `GraphicsDevice` destruction. A long-running title that rotates sampler values could therefore
  grow native sampler ownership monotonically even when it retains only one texture and one public
  sampler at a time.
- **EasyGL/SDL evidence:** EasyGL owns one mutable GL sampler per slot and rewrites its parameters;
  its ownership is bounded by slot count. SDL's cache was an unbounded map from the complete
  `SamplerCacheKeyEXT` to `SDL_GPUSampler*`. A deterministic pre-fix public SpriteBatch regression
  queued 288 distinct `MaxMipLevel` values, submitted them, and observed all **288** objects still
  retained where the new steady-state ceiling is 256. This is exact cache-cardinality evidence,
  not an allocator, process-RSS or driver-memory inference. FNA3D's current SDL driver has the same
  unbounded cache shape, but EasyGL is the parity reference and indefinite ownership is not an XNA
  observable that CNA should reproduce.
- **Implementation/location:** the complete sampler key is unchanged, preserving `SDLGPU-63/64`
  identity. Each entry now records a monotonically increasing last-use serial and the renderer
  retains the 256 most recently used descriptions across frames. A miss beyond the ceiling evicts
  the least-recently used handle and calls `SDL_ReleaseGPUSampler`. This is safe during replay:
  SDL's Vulkan/D3D12 backends track a bound sampler on the recording command buffer and their
  documented release queues physical destruction until those references finish; future CNA draws
  retain descriptions rather than raw sampler handles and therefore resolve a fresh object after
  eviction. The 256-entry ceiling is also sixteen times SDL's maximum sampler slots in one shader
  stage, so constructing a later binding in the same draw cannot evict an earlier not-yet-bound
  entry from that draw.
- **Acceptance/test:** exceed the ceiling through the public `GraphicsDevice`/`SpriteBatch` path,
  require exact rendered output and at most 256 retained native objects after submission, then
  reuse the least-recently used state and require correct recreation/pixels without increasing the
  cache. Preserve complete key identity, constructor rollback, stock/dual/environment sampler
  pixels, descriptor-capacity stress and fatal Vulkan validation. Run the focused regression on
  both Vulkan and the authenticated headless D3D12 route without a host display.
- **Result (2026-09-11):** complete. The exact pre-fix failure was **288 > 256**; after the LRU
  implementation `SdlGpu_SamplerCacheLifetime` passes **5/5** on offscreen Vulkan and headless
  D3D12, including recreation of evicted state zero and its white pixel. The focused native
  sampler/descriptor/lifetime set passes **7/7** (`SamplerCacheLifetime`, constructor exception
  safety, `SamplerState`, stock, dual-texture, environment-map and the 29-check descriptor stress).
  No Vulkan validation, D3D12 wrapper-fatal diagnostic or host-display access occurred. Only
  targeted incremental targets were compiled and linked.

### SDLGPU-127 — bound immutable graphics-pipeline retention across state churn ✅

- **Problem/public behavior:** every stock renderer family and each live compiled-effect program
  cached immutable `SDL_GPUGraphicsPipeline` objects until `GraphicsDevice` destruction. Ordinary
  `RasterizerState.DepthBias` and `SlopeScaleDepthBias` are unrestricted floats and participate in
  pipeline identity, so a long-running title that animates either value could grow native pipeline
  ownership monotonically while retaining only one public state and target at a time.
- **EasyGL/SDL evidence:** EasyGL updates mutable GL rasterizer state and does not create a native
  program/pipeline object for every bias value. SDL GPU's stock caches were unbounded maps from the
  complete state/layout hash to native handles; the compiled-effect cache had the same unbounded
  inner map for each lifetime-bounded program. A deterministic pre-fix public draw regression
  submitted 288 new bias values after its ten legitimate setup variants and observed **298**
  colored pipelines retained, then **299** after requesting the evicted-candidate zero state where
  the new steady-state ceiling is 256.
- **Implementation/location:** all fixed stock caches and the per-program compiled-effect caches
  now share a complete-key 256-entry LRU. Cache hits refresh one renderer-wide monotonic serial;
  insertion beyond the per-cache ceiling erases the least-recently-used entry and calls
  `SDL_ReleaseGPUGraphicsPipeline`. This is safe after a recorded bind: SDL's Vulkan and D3D12
  implementations retain pipelines on the command buffer, Metal's native encoder retains the
  bound object, and future CNA commands retain public state/layout rather than a raw pipeline
  handle. The separate optional `ShaderEffect` wrapper cache remains bounded by that wrapper's
  own lifetime and is not part of the classic renderer-global retention defect.
- **Acceptance/test:** exceed the limit using ordinary public `RasterizerState`/draw calls, require
  exact final pixels and exactly 256 retained entries after submission, reuse the oldest zero-bias
  state and require correct pipeline recreation without cache growth. Preserve A→B→A identity,
  render-state, instancing, MRT, stock effects and compiled-effect behavior. Run the stress on real
  offscreen Vulkan and authenticated headless D3D12 with fatal driver diagnostics enabled.
- **Result (2026-09-11):** complete. The pre-fix **298/299** retention sequence fails both exact
  cardinality assertions; after the LRU implementation `SdlGpu_DepthBias` passes **71/71** on
  Vulkan and all **64/64** applicable classic checks on D3D12, including exact blue/green pixels
  before and after eviction. The seven affected native registrations (depth bias, render state,
  instancing, MRT, stock effects, skinned/fog and compiled effects) pass **7/7**; the complete
  compiled-effect executable separately passes **44/44** on D3D12. No Vulkan validation,
  D3D12 wrapper-fatal diagnostic, host-display access or full rebuild occurred.


### SDLGPU-128 — re-synchronize fail-closed parity inventories after the `next` merge ✅

- **Problem/public behavior:** merge `b4508d38d` added two modern resource interfaces, forty
  renderer hooks and one EasyGL example after `SDLGPU-117`'s closeout. Both parity checkers failed
  closed: the contract generator refused unknown interfaces/methods and the corpus classifier left
  `easygl_texture3d_addressw_test.cpp` unclassified. A stale audit could otherwise hide a newly
  inherited default or mistake a CNA-specific shader diagnostic for an ordinary XNA gap.
- **EasyGL/SDL evidence:** the merged `SpriteBatch` calls one complete seven-field
  `ISpriteBatchRenderer::SetSamplerState`; SDL GPU overrides it directly, while the default fans
  out to the three legacy sampler hooks used by other families. The new EasyGL volume-address test
  constructs `ShaderEffect`, whose source-shader API is CNA-specific; ordinary XNA sampler W/LOD
  behavior remains owned by `SDLGPU-64/79/121/122`. `ITexture3DRenderer` and
  `ITextureCubeRenderer` format getters are unused renderer metadata: the public XNA wrappers own
  their `SurfaceFormat`, and neither renderer samples through those getters.
- **Location:** `tools/check_sdlgpu_renderer_contract_audit.py`, generated
  `plans/sdlgpu_renderer_contract_audit.csv`,
  `tools/check_sdlgpu_easygl_example_classification.py`, the generated example manifest and this
  execution log. No renderer or shared graphics behavior changes.
- **Acceptance/test:** enumerate every live hook and example with zero unresolved/unclassified
  rows; classify the merged modern surface explicitly; retain exact-signature fail-closed checks
  for SDL GPU's complete sampler override versus inherited compatibility hooks; create a concrete
  task for any ordinary behavior with weaker SDL evidence.
- **Result (2026-09-11):** both stable compile databases parse without a build. The contract audit
  is **266/266**: 144 equivalent, five `SDLGPU-80` query limitations and 117 renderer-specific or
  modern `out`, with zero `~`, `≠` or `∅`. The corpus audit is **247/247**: 128 covered, 52 direct,
  two query-limit examples, three EasyGL defects, nine EasyGL-specific, 53 modern-out and zero
  unclassified/new-test-needed. The only new ordinary-XNA evidence gap is the merged
  `SpriteBatch.SamplerStates[0]` publication oracle, now recorded separately as `SDLGPU-129`.

### SDLGPU-129 — run the merged SpriteBatch sampler-publication oracle on SDL GPU ✅

- **Problem/public behavior:** XNA 4.0 publishes a SpriteBatch's sampler into
  `GraphicsDevice.SamplerStates[0]` when render state is prepared: during `Begin` for Immediate,
  but only during the `End` flush for Deferred. The assigned state remains observable by a later
  3D draw. Merge `b4508d38d` brought the shared, XNA-runtime-measured discriminator and registered
  it for EasyGL and Vulkan, but not SDL GPU.
- **EasyGL/SDL evidence:** EasyGL and Vulkan compile the identical
  `modules/graphics/examples/spritebatch_sampler0_publication_test.cpp`. SDL GPU's source reaches
  its complete sampler override, but source shape alone does not prove the deferred timing or the
  later draw's wrap-versus-clamp pixel. This is verification weakness (`~`), not a demonstrated
  implementation defect.
- **Likely location:** register the unchanged shared source in
  `modules/renderers/sdl-gpu/examples/CMakeLists.txt`; change renderer/shared code only if the
  discriminator exposes a real difference.
- **Acceptance/test:** under offscreen Vulkan, require all nine timing/state/pixel legs to pass,
  including Deferred A→B publication, Immediate publication and the later stock `BasicEffect`
  draw inheriting `PointWrap`; keep validation diagnostics fatal and rerun the adjacent sampler
  fixtures. If portable Windows configuration accepts the source, run the same target on headless
  D3D12 as a portability check.
- **Result (2026-09-11):** the unchanged shared source is now the 193rd native and 260th Windows
  SDL GPU integration registration. It passes **9/9** on real offscreen Vulkan and the same CTest
  passes through authenticated headless D3D12, with validation/debug-layer output fatal. A focused
  native sampler slice passes **12/13** registrations: every new publication leg and all adjacent
  sampler/cache/mip/address fixtures pass except the independently reproducible pre-existing
  `SdlGpu_TextureFilterOrdinalContract` F2 assertion. That binary is **69/70** on four consecutive
  runs: only four non-anisotropic pairs with the same magnification half differ, each at exactly 60
  pixels. This is recorded as `SDLGPU-130` rather than misreported or folded into this test-only
  task. The Linux target added one translation unit/link; the Windows target's first build after
  the 440-commit merge rebuilt its required 191-step dependency closure at `-j2`, with no other
  heavy build running.

### SDLGPU-130 — classify and repair the texture-filter same-magnification divergence ✅

- **Problem/public behavior:** the renderer-neutral `TextureFilterOrdinalContract` requires two
  non-anisotropic sampler states with the same magnification filter to render identical pixels in
  its 8×4→16×8 SpriteBatch leg. SDL GPU deterministically passes every individual filter partition
  but fails only F2: `Linear`/`LinearMipPoint` versus each of
  `MinPointMagLinearMipLinear`/`MinPointMagLinearMipPoint` differ at exactly 60 pixels.
- **EasyGL/SDL evidence:** the same source is registered as `EasyGL_TextureFilterOrdinalContract`
  and passes **70/70** on the merged EasyGL renderer under isolated Xvfb. SDL GPU's descriptor trace
  gives all four states `mag_filter=LINEAR`, distinct complete cache hashes/handles and repeatable
  cache hits while their minification/mipmap fields intentionally differ. Four consecutive Vulkan
  runs produced the same **69/70** result, with no validation diagnostic. Every one of the 60
  differences was exactly one blue-channel LSB; the four affected cross-group pairs shared the
  same coordinates and values, while both within-group comparisons were byte-identical.
  The failure is adjacent evidence discovered by `SDLGPU-129`, not caused by that task's CMake-only
  registration. Historical plan evidence says this oracle was green, so the current state must not
  be accepted without locating whether renderer behavior or the exact-equality invariant changed.
- **Likely location:** first compare identical EasyGL output and trace all four SDL native sampler
  descriptors; then isolate the first introducing commit or the LOD boundary. Fix renderer state/
  cache identity if SDL selects the wrong filter, or narrow the shared oracle only if EasyGL/XNA
  evidence proves exact equality is an invalid invariant. Do not weaken the discriminating F1,
  point/linear, queued-state or address-mode legs.
- **Acceptance/test:** reproduce before the fix; obtain EasyGL oracle evidence; make all **70/70**
  checks pass on offscreen Vulkan and the applicable headless D3D12 path with fatal diagnostics;
  rerun the focused sampler slice and document whether this was an SDL defect, a shared-test defect
  or a rigorously justified rasterization/API difference.
- **Result (2026-09-11):** this was an SDL shader regression introduced by `SDLGPU-121`, not a
  sampler descriptor/cache defect and not a reason to weaken the oracle. Its portable LOD-bias
  implementation emitted SPIR-V's explicit Bias operand even when the public bias was zero. A
  uniform zero/nonzero shader branch now uses the original implicit lookup for zero and retains the
  explicit operand only for nonzero bias. The pre-fix **69/70** binary returns to **70/70** on real
  offscreen Vulkan; the native-bias-rejection LOD oracle and all-26-shader constructor/reflection
  target pass beside it (**3/3 CTests**) with validation diagnostics fatal. The complete adjacent
  native sampler/mipmap slice then passes **14/14**. Bounded failure output now reports the first
  eight coordinates and RGBA pairs if exact same-magnification equality ever regresses again. The
  first Windows attempt did not execute the renderer (`wine` loader status `c0000135`) because its
  executable predated the merged dependency staging; after the one affected target was rebuilt,
  the same strict **70/70** fixture passes through authenticated headless D3D12 with debug-layer
  diagnostics fatal.

### SDLGPU-131 — detach compiled runtimes before renderer context teardown ✅

- **Problem/public behavior:** an ordinary compiled XNA `Effect` owns its renderer runtime and may
  remain referenced after the owning `GraphicsDevice` is destroyed (the merged CNAEXT engine
  post-process graph is one concrete owner). `SdlGpuCompiledEffect` retained a bare
  `MOJOSHADER_sdlContext*`; `SdlGpuRenderer::~SdlGpuRenderer` destroyed that context without first
  releasing live effects, so the runtime's later destructor called `MOJOSHADER_deleteEffect` and
  its shader callback through freed renderer state. Destruction order must be safe even though
  attempts to render through a disposed device remain invalid.
- **EasyGL/SDL evidence:** merge `b4508d38d` brought EasyGL's equivalent teardown repair and its
  concrete engine-layer failure history. SDL GPU had the same shared-context/live-effect lifetime
  shape but no registry. A plain Vulkan regression passed on freed-but-unreused memory; after
  instrumenting the focused renderer and MojoShader SDL_GPU adapter, the unchanged pre-fix test
  failed with a heap access at `MOJOSHADER_sdlDeleteShader` line 868, reached from
  `MOJOSHADER_deleteEffect` and `SdlGpuCompiledEffect::~SdlGpuCompiledEffect`, after
  `MOJOSHADER_sdlDestroyContext` had freed the allocation from renderer destruction.
- **Likely location:** register each successfully constructed/cloned `SdlGpuCompiledEffect` with
  its `SdlGpuRenderer`; during renderer destruction, after queued shader leases/pipelines are
  retired but before the shared context/device, delete each registered native effect, clear its
  program leases and detach its native pointers. A normally destroyed effect unregisters itself;
  an outliving wrapper must never re-enter its dead renderer.
- **Acceptance/test:** the focused runtime must report attached while its device lives and detached
  immediately after device destruction, then destruct without native access. Require the exact
  pre-fix ASan reproducer to pass after the fix; run the complete compiled-effect corpus on Vulkan
  under ASan and the ordinary validation gate, and on authenticated headless D3D12.
- **Result (2026-09-11):** SDL GPU now keeps a renderer-local live-effect registry. Teardown clears
  all queued bindings and immutable pipelines first, releases/detaches all effects while the
  MojoShader context and SDL_GPU device are valid, then destroys that context/device. The new
  deterministic attachment assertion prevents the ordinary non-sanitized suite from becoming
  vacuous. The focused pre-fix ASan failure is gone and the complete instrumented Vulkan corpus
  passes **45/45**; the ordinary Vulkan CTest also passes **45/45** with validation diagnostics
  fatal, and the freshly rebuilt headless D3D12 corpus passes **45/45** with its authenticated
  vkd3d/debug-layer gate.


## 2026-09-10 parity audit final status

| Item | Current evidence |
|---|---|
| Starting branch / commit | `sdlgpu` / `3a44315fdffe974e02a664cacae4fd388c9728aa` |
| Ending parity-behavior/test commit | `9285b36e39106aecd85402c6f14688a4fa2f0fcb` (`SDLGPU-89`); the Linux audit closeout is `SDLGPU-90`; the platform/adversarial closeout now includes `SDLGPU-131` and the focused ASan follow-ups |
| Renderer contract | `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`; exact audit in `plans/sdlgpu_renderer_contract_audit.csv` |
| Reference renderer | `modules/renderers/easygl/{include,src,examples}` |
| Renderer under test | `modules/renderers/sdl-gpu/{include,src,tests,examples}` |
| EasyGL / SDL GPU example sources | 246 / 38 `.cpp` files at audit start; 247 / 45 now (source count, not capability count) |
| EasyGL / SDL GPU renderer unit-test sources | 2 / 2 `.cpp` files |
| SDL GPU registered integration tests | 193 CTests (85 baseline plus 108 parity/remediation registrations) |
| Shared EasyGL parity fixtures available | 32 renderer-neutral sources in `modules/graphics/examples/parity` |
| Shared parity fixtures registered for SDL GPU | 32/32 (all renderer-neutral sources, including the nine classic stock-effect fixtures) |
| Tasks created by this audit | 36 (`SDLGPU-55`–`SDLGPU-90`); the later platform/adversarial re-audit creates 41 more (`SDLGPU-91`–`131`) |
| Completed / open / proven unavoidable | This closed Linux/Vulkan phase completed 36 / 0 tasks; the current renderer-wide ledger is 74 / 3. Three capability fields (`BlendState.MultiSampleMask`, exact half-rate `PresentInterval::Two`, and `OcclusionQuery`) are proven unavailable in current SDL_gpu; the first two are not separate tasks and the third is closed by `SDLGPU-80` |
| SDL GPU build | Stable `cmake-build-sdlgpu`, Debug, `CNA_GRAPHICS_RENDERER=SDL_GPU`, tests/examples and `CNA_SDL_GPU_COMPILED_EFFECTS` ON |
| EasyGL oracle build | Stable `cmake-build-debug`, Debug, `CNA_GRAPHICS_RENDERER=OPENGL33`, tests/examples ON |
| Runtime driver | SDL 3.5.0 SDL_gpu Vulkan on AMD Radeon 780M / Mesa RADV 25.0.7; Khronos validation layer 1.4.309 present |
| Display constraint | Sandboxed tests do not access the host display. All 193 SDL GPU registrations explicitly use `SDL_VIDEODRIVER=offscreen`, `DISPLAY=` and `WAYLAND_DISPLAY=` while still creating the real Vulkan device/swapchain; EasyGL alone uses isolated Xvfb `:179`. Native driver defaults are platform-specific rather than hard-coded to X11. |
| Final SDL GPU verification | **191/191** registered SDL integration CTests pass from the last full stable sweep; the 192nd sampler-cache registration passes **5/5** on Vulkan and headless D3D12, and the new 193rd sampler-publication registration passes **9/9** on both. `SDLGPU-130` restores the strict texture-filter oracle from **69/70** to **70/70** on Vulkan and passes the same **70/70** fixture on headless D3D12; the native-bias-rejection LOD and all-shader constructor/reflection targets pass beside it (**3/3 CTests**), followed by the complete native sampler/mipmap slice at **14/14**. The preceding focused SDL GPU renderer aggregate is **51/51**. `SDLGPU-131` expands the compiled-effect subset to **45/45** on native Vulkan, focused ASan Vulkan and headless D3D12; the new case has a pre-fix ASan trace proving the renderer-context lifetime defect. SDLGPU-122's native-bias rejection gate makes its authored-mip result a shader-emulation proof, and the shared SPIR-V corpus passes 2/2 over all 27 effect passes. `SDLGPU-121` additionally passes the exact stock LOD-bias oracle 7/7 on Vulkan and D3D12, the SpriteBatch/constructor/stock-family companion slice 7/7 on Vulkan, and ShaderCross reflection of all 26 updated stock shaders. `SDLGPU-123` adds a pre-fix-failing cube-face coordinate discriminator that passes 25/25 on Vulkan/D3D12 plus an 11/11 native companion slice. `SDLGPU-124` adds eight destroy/recreate program-identity generations and distinct pass pixels on both drivers; `SDLGPU-125` proves the same loop holds one cache entry while live and returns to zero after every destruction, including deferred ordinary and SpriteBatch replay. `SDLGPU-126` bounds immutable sampler retention at 256 entries and recreates evicted states correctly on both available drivers. `SDLGPU-127` similarly bounds every stock/per-program immutable pipeline cache; its depth-bias discriminator passes **71/71** on Vulkan and **64/64** applicable checks on D3D12, the affected native seven-registration slice passes **7/7**, and the D3D12 compiled-effect corpus remains **45/45**. The integration total contains all 32 shared parity fixtures and now 114 parity-labelled registrations. `SDLGPU-87`'s capability/lifetime regression remains **295/295**; `SDLGPU-89` adds a 21/21 multistream result and isolated slot-15 parity pixels on both renderers. |
| Validation | SDL GPU debug mode is enabled. All 193 integration registrations now make `Validation Error`, `Validation Warning`, bare `VUID-`, `D3D12 ERROR:` or `D3D12 WARNING:` output fatal; the new publication and focused sampler runs emitted none. |
| Final EasyGL/parity oracle | **33/33** registered EasyGL oracle CTests pass under Xvfb/llvmpipe. The direct corpus executes all 32 shared sources under both renderers: 27 frames satisfy the strict byte policy, two use renderer-local discriminating invariants, and three stay within fixed measured line/edge coverage budgets. |
| Exact remaining EasyGL differences | Exact half-rate `PresentInterval::Two` (`⛔`) and `OcclusionQuery` (`SDLGPU-80`, `⛔`). EasyGL's non-default `MultiSampleMask` is also unimplemented, so it is an SDL_gpu limitation but not an EasyGL difference. SDL GPU intentionally differs from the nine XNA/FNA-invalid findings in `EASYGL-PARITY-1`–`9`; findings 4 and 7 are two EasyGL examples of the same bound-FBO backbuffer-readback defect. |

The first attempted baseline used the suite's historical hard-coded `SDL_VIDEODRIVER=x11` and
could not reach the inaccessible host display: early programs skipped and 47 tests failed for the
same environment reason. That result is retained as environment evidence, not misreported as a
renderer failure. The renderer-local `CNA_SDLGPU_TEST_VIDEO_DRIVER` cache setting makes the same
suite reproducible on this host without changing its `x11` default elsewhere.

The real offscreen/Vulkan baseline is **77/85 passed**. The eight failures are evidence and are not
hidden:

1. `SdlGpu_ConstructorExceptionSafety`: 234/264; its failure-injection oracle still hard-codes 23
   construction shaders while the live construction enum creates 25 (`SDLGPU-56`). Every observed
   failure remained transactionally balanced.
2. `SdlGpu_SkinnedEffect_Fog`: classic `SkinnedEffect` legs pass; only CNAEXT `PbrEffect` and
   `SkinnedPbrEffect` half-fog expectations fail. This is recorded as an existing out-of-scope
   regression and must not be worsened.
3. `SdlGpu_DepthBias`: the initial 56/57 SpriteBatch positive-bias failure is resolved by
   `SDLGPU-65`; the expanded test is now 67/67.
4. `SdlGpu_StockEffectSamplerContract` and `SdlGpu_DualTextureSlotSamplerContract`: the baseline
   refusal of their valid stride-28 independent-UV declaration and the exception-unwinding abort
   are resolved by `SDLGPU-59`; both complete with their validation-fatal oracles.
5. `SdlGpu_DescriptorCapacityContract`: the apparent 12/27 and 114/256 "exactness" failure was a
   baseline-reading defect, resolved by `SDLGPU-63`. Those are the deliberately Point-like cases;
   the other 15/27 and 142/256 deliberately Linear/Anisotropic cases must interpolate. All 29
   adversarial capacity/lifetime checks pass with zero wrong samples.
6. `SdlGpu_PointSamplingContract`: the initial 145/146 result is resolved by `SDLGPU-62`; the
  3D non-integer 3×3→10×10 PointClamp mapping now matches all 100 EasyGL/XNA texels.
7. `SdlGpu_GraphicsDevice_OrderedClear`: the initial 45/46 result was an oracle defect, resolved by
   `SDLGPU-66`: the supposedly untouched face had undefined new-resource contents. After seeding it
   with a known sentinel, all 46 checks pass and prove that two later face cycles preserve it.

### Definition and boundary

Parity means observable equivalence for the ordinary XNA 4.0 graphics surface that EasyGL
supports, using identical public inputs and exact or narrowly-toleranced readback. An internal
method named `*EXT` is in scope when an ordinary public constructor/property reaches it. Public
CNAEXT graphics (compute/storage/image bindings, indirect draws, GPU timers, modern render passes,
PBR/glTF additions, modern post-processing/shadows/IBL/particles) is `out`; existing support may be
preserved or fixed incidentally but is not expanded here. FNA under
`/rv/data/library/github.com/FNA-XNA/FNA` and existing shared fidelity tests arbitrate cases where
EasyGL itself appears wrong.

Legend: `=` equivalent with discriminating evidence; `~` implementation exists but evidence is
weaker or semantics remain uncertain; `≠` observable difference; `∅` absent; `⛔` demonstrated
underlying limitation with no correct reasonable emulation; `out` excluded modern CNAEXT.

### Feature-family matrix (initial live-source pass)

| Family | EasyGL reference | SDL GPU current state | State / owner |
|---|---|---|---|
| Clear color/depth/stencil overloads | Direct GL clears; ordered-clear corpus | Deferred clear commands and all combinations | `=` — ordered clear/draw/cube-target tests and state suites pass; `SDLGPU-58/66` |
| Present, interval and modes | Runtime swap interval; resize examples | SDL claim/present modes and proxy copy | `=` for Immediate/One/default forwarding, reset, resize and minimize retry; exact half-rate `PresentInterval::Two` is `⛔` because SDL exposes only VSYNC/IMMEDIATE/MAILBOX and no vblank timing primitive; `SDLGPU-68` |
| Backbuffer size/readback/channel order | Direct readback, RGBA contract | Real `backbufferProxy_` download, contrary to old SDLGPU-39 text | `=` — dimension/first-read/range plus reset/resize/minimize-retained readback pass; `SDLGPU-68` |
| Backbuffer MSAA | Construction-time multisample colour/depth FBO, resolved before present | Device-clamped multisample colour/depth attachments resolve into the swapchain/readback proxy; public reports and Reset track the applied count | `=` — identical public source proves 4x applied state, intermediate edge coverage, opaque interior and segment preservation on both renderers; SDL-only A→B→A reset adds the XNA/FNA behavior EasyGL lacks; `SDLGPU-85` |
| Logical resolution/transforms/letterbox | Explicit default viewport and transforms | Explicit physical/logical transforms | `=` — physical rectangle, all modes, HiDPI transforms, zero-size safety, SpriteBatch projection and real resize pass; `SDLGPU-68` |
| Blend factors/functions/BlendFactor | Full separate RGB/A state | Complete immutable pipeline state and per-draw dynamic factor | `=` — all 13 factors in all four roles, all five equations for RGB/A, separate fields and A→B→A are pixel-verified; `SDLGPU-58` |
| ColorWriteChannels | Four slot-aligned masks | Four slot-aligned immutable pipeline masks | `=` — every slot-0 mask value plus distinct four-MRT-slot masks and cache restoration pass; `SDLGPU-58` |
| MultiSampleMask | EasyGL also documents non-default masks as unimplemented | SDL receives the value but cannot apply it | `⛔` — SDL 3.5 requires `sample_mask=0` and `enable_mask=false`; arbitrary-shader emulation cannot preserve sample coverage/depth/stencil side effects; `SDLGPU-58` |
| Depth compare/write | All comparisons | Complete immutable depth state | `=` — shared 8×3 signature matrix, exact EasyGL frame, direct EasyGL source and write-enable oracle pass; `SDLGPU-58` |
| Stencil masks/ops/two-sided/reference | Full front/back state | Complete immutable descriptor and per-draw dynamic reference | `=` — all eight compares and operations, masks, two-sided state and reference pass, including A→B→A; `SDLGPU-58` |
| Cull/scissor/viewport/depth range | Full GL state | Cull/bias immutable; scissor/viewport/range captured per draw | `=` — exact EasyGL sources, exhaustive deferred A→B→A draws, cube/RT2D/backbuffer switches and resize all pass; presentation-mode lifecycle remains separately `SDLGPU-68` |
| FillMode.WireFrame | Renderer-side triangle-edge expansion | Native `SDL_GPU_FILLMODE_LINE` in every ordinary pipeline | `=` — shared asymmetric three-edge fixture passes; `SDLGPU-61` |
| Constant/slope depth bias | GL polygon offset with XNA normalization | Per-format normalized-to-native conversion in every immutable pipeline; SpriteBatch uses projected layer depth; each complete-key pipeline cache has a 256-entry LRU | `=` — expanded 71/71 pixel/cache/retention oracle passes on Vulkan and its 64 applicable checks pass on D3D12; `SDLGPU-65/127` |
| Sampler filter/address/anisotropy | All stock/effect slots; fixed mutable sampler ownership per slot | Every stock, compiled-effect and SpriteBatch command captures the complete state; immutable native cache retains a bounded 256-entry LRU | `=` — shared pixel oracles, 29/29 descriptor-capacity checks and the pre-fix-failing 288-state retention/recreation oracle on Vulkan/D3D12; `SDLGPU-64/126` |
| MaxMipLevel/LOD bias/AddressW | Applied on ordinary draws | MaxMipLevel/bias pixel-verified on stock 3D, SpriteBatch and compiled effects; every bias path is shader-side on every driver; W reaches the native descriptor | `=` for implemented semantics (`SDLGPU-64/79/121/122`); compiled bias's Metal hardware execution remains part of the platform evidence gate `SDLGPU-95`, not an open implementation gap |
| Texture2D Color/mips/partial/NPOT/readback | Real upload + CPU/GPU read paths | Format-sized upload, shared CPU shadow and authored mips | `=` including 3D PointClamp, authored storage replacement and queued-source retention; `SDLGPU-69/81` |
| Texture2D ordinary non-Color formats | Packed, DXT native-or-decode, SNORM | Exact packed/BC/SNORM storage where available; lossless packed and DXT decode fallbacks; NormalizedByte2 semantic expansion | `=` for EasyGL's exact nine-format set, including typed transfers, sampling, NPOT/partial DXT blocks and authored mips; remaining classic formats are truthfully refused by both public paths; `SDLGPU-69` |
| Texture3D Color/mips/boxes/readback | Native RGBA8 volume path | Native RGBA8 volume path | `=` — exact EasyGL sources prove slices, asymmetric boxes/readback and authored mips; expanded SDL test proves NPOT, cumulative writes, A→B→A and exact format forwarding; `SDLGPU-71` |
| Texture3D surface format | Public seam permits Color only; concrete EasyGL resource always allocates RGBA8 and ignores its internal argument | Resource-specific classifier supports Color, refuses the other 19 classic formats, and the concrete resource records/rejects its forwarded value | `=` — truthful Color-only reference surface plus ordinary compiled-effect volume sampling/lifetime; `SDLGPU-71/79` |
| TextureCube faces/mips/partial/readback | Color and DXT cubes; packed formats are falsely accepted (defect ledger) | Color plus native-BC-or-decoded DXT1/3/5; explicit refusal for every other classic format | `=` for the truthful public surface: six faces, partial rectangles/blocks, authored mips, DDS/content readback and DXT sampling; `SDLGPU-70`, `EASYGL-PARITY-2` |
| RenderTarget2D Color/depth/MSAA/readback/mips | Full classic path | Exact per-target D16/D24-or-D32/D24S8-or-D32S8 storage, real SetData/resolve/readback/mips | `=` — zero and real nonzero MSAA, full/partial authored levels, first-use readback, preserve/discard including depth/stencil, generated mip readback/sampling and mixed A→B→A transitions pass; `SDLGPU-73/74` |
| RenderTarget2D float/half/Rgba64 | Requested storage, runtime probed | Exact native storage, format-aware pipelines/transfers and XNA channel expansion | `=` — all nine EasyGL formats agree at query/construction, render, typed readback and sampling; 2/8/16-byte SetData is exact; `SDLGPU-72/74` |
| RenderTargetCube Color/faces/depth/MSAA/mips | Full classic path | Exact color/depth storage, six-face uploads/resolves/readback/sampling and mips | `=` — six-face SetData/preserve/discard/PlatformContents, independent MSAA face stores, authored/generated mips, readback/sampling and 2D/cube/backbuffer transitions pass; `SDLGPU-73/74` |
| RenderTargetCube non-Color | Requested storage, runtime probed | Exact native cube storage and cube-specific runtime probe | `=` — all nine EasyGL formats retain six faces; float sampling preserves >1; `SDLGPU-73` |
| MRT binding/clear | Independent fragment outputs verified for `RenderTarget2D` attachments; cube faces in an MRT are explicitly refused | Up to four simultaneous independently writable, mixed-format `RenderTarget2D` attachments; cube faces are refused at the same seam | `=` — ordinary compiled Effect oC0/oC1 outputs, mixed Color/RGBA16F targets, per-slot masks/blending and count/format/state cache transitions pass; the shared cube-face boundary is source-verified in both renderers; `SDLGPU-75/90` |
| Vertex buffers/declarations/dynamic options | declaration semantic+offset driven | classic inputs use semantic/index/format/offset and declaration-aware pipeline keys; queued draws copy the issue-time bytes | `=` — partial/raw/typed uploads, zero capacity, CPU readback and twelve-frame None/Discard/NoOverwrite replacement pass; `SDLGPU-59/78` |
| Index buffers 16/32-bit/dynamic | Both sizes/options | Both widths, CPU shadows and SDL cycle hints | `=` — 16/32-bit transfer/readback, partial/zero uploads and dynamic replacement pass; `SDLGPU-78` |
| Draw primitive/indexed/user ranges/order | Full public family | every ordinary variant and chronological queue | `=` — triangle list/strip and line list/strip, 16/32-bit indices, declaration-driven user data, nonzero vertex/start/base/index offsets, invalid ranges and ordering are discriminating-pixel verified; `SDLGPU-66/76/78` |
| Instancing/multiple vertex streams | Real per-instance divisors and streams | semantic multi-stream pipeline layouts; native instance rate with frequency materialization | `=` — core and ordinary compiled Effect routes cover per-vertex/per-instance streams, offsets, frequencies and A→B→A; explicitly CNAEXT `ShaderEffect` instancing is `out`; `SDLGPU-60/79` |
| SpriteBatch geometry/sort/state | Broad 2D corpus | Real deferred/immediate sprite renderer with target-local state snapshots | `=` — identical shared geometry/state/font frames, disposed-source policy, exact source/flip matrix, compiled Effect texture/pass behavior, 2D/3D/backbuffer ordering and cube-face target-local coordinates under mismatched presentation scaling pass; `SDLGPU-76/79/123` |
| BasicEffect | broad lighting/fog/option corpus | real per-vertex/per-pixel shader paths with complete light/material snapshots | `=` — shared term/transform/fog fixtures and the identical EasyGL per-pixel source distinguish every classic option family; `SDLGPU-77` |
| AlphaTest/DualTexture effects | broad compare/source/UV corpus | real shaders; independent TEXCOORD0/1 and null-texture white fallbacks | `=` — compare/source/fog/UV/transform/material fixtures pass byte-identically or within their narrow pixel tolerance; `SDLGPU-59/77` |
| EnvironmentMap/Skinned effects | broad light/specular/fresnel/bones corpus | real shaders; semantic skinned-stream normalization accepts Byte4 and Vector4 indices; full bone palette uses a cross-backend vertex sampler | `=` — shared term/fresnel/transform/bone fixtures plus identical EasyGL per-pixel and weights sources pass; the complete corpus found and corrected EnvironmentMapEffect's per-fragment Fresnel divergence so its fanned-normal frame is now byte-identical; D3D12 also passes the full shared skinned-term oracle after its storage-buffer PSO defect was removed; `SDLGPU-77/81/103` |
| Ordinary compiled effects | real compiled runtime; explicitly refuses vertex-stage texture sampling renderer-wide | real deferred MojoShader runtime and draw routes; explicitly refuses the same vertex-stage sampling boundary | `=` — 45/45 reflection/state/pixel/resource tests cover techniques, passes, clone values, multi-stream/instancing, SpriteBatch, RT/cube/volume pixel samplers, LOD bias, deferred lifetime including runtime-after-device destruction, destroy/recreate pipeline identity, program-lifetime eviction and bounded per-program state-cache retention on Vulkan/D3D12; the shared vertex-stage boundary is source-verified; `SDLGPU-79/90/118/119/122/124/125/127/131` |
| Models where renderer participates | hierarchy/multi-mesh/skinning corpus | stock draw paths and ordinary content pipeline | `=` — five exact EasyGL sources prove Model.Draw, 32-bit indices, child transforms, per-mesh effects and skinned playback; `SDLGPU-79` |
| Deferred resource/state lifetime | immediate GL plus registries | shared-state command snapshots, retained queued resources and bounded immutable sampler/pipeline caches | `=` — source textures/volumes/cubes/RTs, bound RT2D/cube/MRT, vertex/index data, full render state, samplers, viewports/scissors, stock/compiled/custom effects and render ordering have destruction or A→B→A issue-time tests; cache eviction/recreation remains pixel- and validation-clean; `SDLGPU-81/118/119/125/126/127` |
| OcclusionQuery | native GL query (`any` on GLES, count on desktop) | false capability and deterministic public construction refusal | `⛔` — vendored SDL_gpu 3.5.0 exposes completion fences but no occlusion/query-pool command, writable fragment-stage storage or native-device interop; exact renderer emulation fails arbitrary compiled shaders, four-target MRT, discard/depth/stencil/MSAA cases; `SDLGPU-80` |
| ShaderEffect/PBR/glTF/compute/storage/indirect/timers/modern passes | EasyGL CNAEXT families | some existing SDL GPU implementations | `out` — preserve, do not expand |

### Ordinary SurfaceFormat matrix (current)

The first twenty enum entries are the XNA-facing formats. `SDL native` means the vendored
`SDL_gpu.h` exposes an exact or semantics-compatible storage format; runtime
`SDL_GPUTextureSupportsFormat` still has to be asked for the intended usage. `EasyGL RT` is
driver-probed where noted. Since `SDLGPU-69`, Texture2D has an explicit renderer answer for all
twenty classic entries: it implements the same nine-format public set as EasyGL and explicitly
refuses the other eleven. `SDLGPU-70` closes the cube column with Color plus DXT1/3/5 and truthful
refusal of the other sixteen. `SDLGPU-71` closes the volume column at the reference renderer's
actual Color-only boundary: both implementations allocate RGBA8 and the public constructor now
obtains a resource-specific verdict instead of letting SDL silently discard its argument.
`SDLGPU-72/73` close both render-target format columns with the same nine-format set EasyGL
actually creates. `SDLGPU-73` additionally gives each target its own exact-or-more-precise
depth/stencil storage instead of substituting the swapchain's combined format.

| SurfaceFormat | EasyGL Texture2D/Cube | EasyGL render target | SDL native candidate | SDL current / task |
|---|---|---|---|---|
| Color | yes | yes | R8G8B8A8_UNORM | Texture2D/cube/volume/RT2D/RTCube `=` |
| Bgr565 | yes (desktop/ES3); cube classifier is a false positive | no | B5G6R5_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Bgra5551 | yes (desktop/ES3); cube classifier is a false positive | no | B5G5R5A1_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Bgra4444 | yes (desktop/ES3); cube classifier is a false positive | no | B4G4R4A4_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Dxt1 | native or renderer decode | no | BC1_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| Dxt3 | native or renderer decode | no | BC2_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| Dxt5 | native or renderer decode | no | BC3_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| NormalizedByte2 | yes (desktop/ES3); cube prohibited by public rule | no | R8G8_SNORM | Texture2D `=` via semantic RGBA8_SNORM expansion; cube/volume `=` refusal |
| NormalizedByte4 | yes (desktop/ES3); cube prohibited by public rule | no | R8G8B8A8_SNORM | Texture2D `=` native; cube/volume `=` refusal |
| Rgba1010102 | public EasyGL path defers/refuses | no | A2R10G10B10/A2B10G10R10_UNORM | Texture2D/cube/volume `=` refusal |
| Rg32 | public EasyGL path defers/refuses | no | R16G16_UNORM | Texture2D/cube/volume `=` refusal |
| Rgba64 | public texture path defers/refuses | desktop RT yes | R16G16B16A16_UNORM | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Alpha8 | public EasyGL path defers/refuses | no | A8_UNORM | Texture2D/cube/volume `=` refusal |
| Single | public texture path defers/refuses | driver-probed R32F | R32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Vector2 | public texture path defers/refuses | driver-probed RG32F | R32G32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Vector4 | public texture path defers/refuses | driver-probed RGBA32F | R32G32B32A32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfSingle | public texture path defers/refuses | driver-probed R16F | R16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfVector2 | public texture path defers/refuses | driver-probed RG16F | R16G16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfVector4 | public texture path defers/refuses | driver-probed RGBA16F | R16G16B16A16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HdrBlendable | public texture path defers/refuses | driver-probed RGBA16F | R16G16B16A16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |

The seven `*EXT` enum entries after these twenty belong to modern/CNA-specific planning unless an
ordinary call path proves otherwise. They are not silently counted toward this parity verdict.

### Mechanical renderer-contract audit

`plans/sdlgpu_renderer_contract_audit.csv` is the exact, one-row-per-hook inventory generated by
`tools/check_sdlgpu_renderer_contract_audit.py`. The checker asks Clang to parse the real EasyGL and
SDL GPU translation units using the two stable build trees' compile databases; it does not infer
overrides from grep or trigger a rebuild. After merge `b4508d38d` it finds **266** virtual hooks:
**149** are on a classic-observable call path (**144 `=`**, **5 `⛔` occlusion-query hooks**), and
**117** are renderer-specific or CNAEXT/modern `out`. There are **zero** unresolved `~`, `≠`, or
`∅` rows.

For the classic rows, EasyGL has 128 concrete override instances and 35 inherited instances; SDL
GPU has 134 concrete override instances, 23 explicitly allowlisted inherited instances, the five
unavailable query hooks, and one deliberately absent lease object because its factory's reviewed
null default means this non-GL renderer needs no movable context. Exact signatures make every
allowance fail closed when the interface changes. The inherited cases are limited to: framework-
owned CPU/mip shadows; a compressed upload unreachable on render-target cubes; the float-coordinate
SpriteBatch overload that delegates to the rectangle overload; null/no-op context and invalidation
hooks appropriate to SDL_gpu; identity presentation/profile facts shared with EasyGL; cube-face
binding delegated to the concrete target; and healthy 3D/device/default-limit answers. GL handle/
binding hooks and unused resource-dimension helpers are separately classified `out`, never treated
as missing SDL behavior.

The grouped reading aid below is retained, but the CSV and checker are authoritative when it and
the live interface disagree. “SDL explicit” means a concrete override exists; it does not by itself
mean parity.

| Contract hooks | EasyGL | SDL GPU | Public/scope result |
|---|---|---|---|
| `IVertexBufferRenderer::{SetData,SetDataWithOptions,SetVertexDeclaration,GetVertexCount}` | explicit | explicit | classic; semantic declarations, typed/raw/partial/empty transfers, CPU round-trip and every option are verified (`SDLGPU-59/78`) |
| `IIndexBufferRenderer::{SetData16,SetData32,*WithOptions,GetIndexCount,IsThirtyTwoBit}` | all explicit | all explicit | classic; both widths, partial/empty transfers, CPU round-trip and every option are verified (`SDLGPU-78`) |
| `IOcclusionQueryRenderer::{Begin,End,IsComplete,PixelCount,PixelCountIsPreciseEXT}` and factory | explicit | factory explicitly throws the exact underlying SDL_gpu limitation; capability/profile false | classic `⛔`; silent inherited-null object removed and no false result is fabricated (`SDLGPU-80`) |
| `ITextureRenderer::{GetWidth,GetHeight,UpdatePixels,UpdatePixelsLevel,HasDefinedMipLevel,GetSurfaceFormatEXT,ShareCpuPixels,GetData}` | explicit except default mip query behavior is implemented through EasyGL state | width/height/update/format/GetData explicit; mip query and CPU-share defaults remain appropriate because the XNA layer owns those shadows | classic format behavior `=` through `SDLGPU-69`; deferred lifetime remains `SDLGPU-81` |
| `ITexture3DRenderer::{SetData,GetData,BindGL,GetDimensionsEXT,GetSurfaceFormatEXT}` | upload/readback/bind explicit; concrete storage is RGBA8 | upload/readback explicit; concrete resource records and guards its Color format; GL/default dimensions and format metadata are irrelevant | classic Color storage/transfers `=` (`SDLGPU-71`); renderer metadata getters are `out` because the public wrapper owns them |
| `ITextureCubeRenderer::{SetData,SetCompressedDataEXT,GetData,BindGL,ShareCpuPixels,GetSizeEXT,GetSurfaceFormatEXT}` | all storage paths explicit | RGBA and DXT Set/Get explicit; exact DXT block shadow; size/format/cpu-share defaults are unused by ordinary SDL sampling | classic cube storage `=` through `SDLGPU-70`; `BindGL` and unused renderer metadata are `out` |
| `IRenderTargetRenderer::{Bind/Unbind,GetData,GetMultiSampleCount,GetAppliedDepthStencilFormatEXT,HasRealDepthBuffer,DepthBufferBitsEXT,HasRealStencilBuffer}` | explicit where native facts differ | bind/read/MSAA and all applied depth/stencil facts explicit from per-target state | classic format/property/lifecycle behavior `=` (`SDLGPU-72–74`) |
| `IRenderTargetCubeRenderer::{GetSize,BindFace,Unbind,GetData,GetMultiSampleCount,depth/stencil facts}` | explicit where native facts differ | size/bind/read/MSAA and all applied depth/stencil facts explicit from per-target state | classic format/property/lifecycle behavior `=` (`SDLGPU-66/73/74`) |
| `IEffectRenderer` compile/bind/unbind, uniform scalar/vector/matrix/arrays, 2D/cube/3D texture binds | all explicit | compile and scalar/vector/matrix explicit; bind/unbind no-op by design; array and texture defaults are covered by renderer-owned compiled path only | ShaderEffect is CNAEXT; ordinary compiled effect separately in scope (`SDLGPU-79`) |
| `ISpriteBatchRenderer::{Begin,End,SetTransformMatrix,SetCustomEffect,SetSampler*,SetImmediateMode,Draw overloads}` | explicit except default immediate hook | explicit for all public stock and compiled-Effect paths, including complete sampler state | classic geometry/state/font/compiled-Effect behavior `=` (`SDLGPU-64/65/76/79`) |
| `AcquireThreadContextLeaseEXT`, surface changed/invalidated, context-loss hooks | context/registry explicit | surface change explicit; invalidation/recovery defaults retained | surface change is ordinary lifecycle and verified by `SDLGPU-68`; lease and simulated recovery are EasyGL-specific/CNAEXT, while resource lifetime remains `SDLGPU-81` |
| `Clear`, all six depth/stencil variants, legacy depth/blend/write toggles | explicit | explicit | classic; discriminating sweep (`SDLGPU-58/66`) |
| `Present`, viewport/default viewport, virtual resolution, presentation mode, swap interval, MSAA/application-format facts | explicit including default viewport and runtime facts | present/size/default-physical-viewport/virtual/mode/requested interval explicit; SDL-local applied-interval fact distinguishes fallbacks; applied-format hooks remain inherited | classic presentation behavior verified by `SDLGPU-68`; format facts remain `SDLGPU-69–74` |
| format classifiers, compressed transfer policy, half-float filtering | explicit, runtime/profile aware; volume inherits Color-only framework fallback | Texture2D, TextureCube, Texture3D, RT2D and cube-target classification explicit; half filtering still inherited | classic textures/volume/targets `=` through `SDLGPU-69–73`; remaining filtering evidence is owned by its sampler/effect tasks |
| coordinate transforms and `ReadBackbuffer` | explicit | explicit | classic; readback baseline passes (`SDLGPU-68`) |
| texture/sprite/RT2D/RTCube/Texture3D/TextureCube factories, including format-bearing `*EXT` RT factories | explicit | Every texture and target factory preserves its classified format; cube and 2D targets retain per-resource depth and usage facts | classic format/factory/lifecycle surface `=` (`SDLGPU-69–74`) |
| `SetRenderTarget2D`, `SetRenderTargetCubeFace`, `SetRenderTargets` | all explicit | 2D and plural explicit; single cube-face hook inherited and delegates to plural | classic; cube ordering and independent MRT output semantics verified (`SDLGPU-75`) |
| blend/depth/raster/sampler applications; BlendFactor/reference/scissor/viewport | all explicit | all explicit | classic; immutable-key and propagation verification (`SDLGPU-58/63/65`) |
| buffer factories and colored/extended primitive/indexed draw hooks | explicit | explicit | classic; semantic dispatch, native topology, all ordinary draw families and ranges are verified by `SDLGPU-59/78` |
| `DrawInstancedPrimitivesEx`, `GetMaxVertexStreams`, multistream capability | explicit | explicit stock/compiled-effect draws, full sixteen-stream public ceiling and truthful capabilities | classic XNA 4.0 stock and ordinary compiled Effect semantics `=` (`SDLGPU-60/79/89`); CNAEXT `ShaderEffect` instancing is `out` |
| `SupportsDepth*`, `Ensure3DSupported`, unsupported-call behavior, `CanBeginDrawEXT` | runtime-aware | all three default-framebuffer depth/stencil answers and the legacy capability switch use the same queried native-format fact; other defaults retained where semantically applicable | classic capability/profile truthfulness (`SDLGPU-57/87`) |
| `SupportsCapability`, numeric texture/cube/volume/RT limits, limitations text | explicit runtime answers | explicit exhaustive capability switch, sixteen XNA vertex streams and limitations text; format/device limits remain owned by their dedicated tasks | publicly observable; false promises removed by `SDLGPU-57`, stream claims verified by `SDLGPU-60/89` |
| compiled-effect factory/runtime/support | explicit | explicit | ordinary Effect bytecode path `=` across reflection, state and all shared draw contracts (`SDLGPU-79`) |
| ShaderEffect dialect/source execution | EasyGL explicit | runtime compile path exists, while `GetShaderDialectEXT` inherits the truthful `Unknown` default | existing CNAEXT, `out`; no classic XNA behavior depends on the source-dialect query |
| compute/storage/image, indirect draw, barriers, GPU timer, shadow/IBL, display-color-space extensions | EasyGL implements a subset | SDL mostly inherits safe false/no-op defaults | modern CNAEXT, `out` |

### EasyGL example-corpus classification

`plans/sdlgpu_easygl_example_classification.csv` contains exactly one sorted row for every live
EasyGL example source. `tools/check_sdlgpu_easygl_example_classification.py` compares it to the
filesystem, rejects duplicates/unknown categories/missing evidence, and currently reports:

| Category | Count |
|---|---:|
| `classic-xna-covered-by-existing-sdlgpu-test` | 128 |
| `classic-xna-direct-parity` | 52 |
| `classic-xna-new-sdlgpu-test-needed` | 0 |
| `classic-xna-feature-missing` | 2 |
| `modern-cnaext-out` | 53 |
| `easygl-specific` | 9 |
| `duplicate` | 0 |
| `easygl-defect` | 3 |
| `unclassified` | **0** |

The three `easygl-defect` rows are two historical examples which read the current FBO instead of
the XNA backbuffer (`EASYGL-PARITY-4/7`) and the MSAA-change example which explicitly asserts
EasyGL's inability to apply the reset request (`EASYGL-PARITY-6`). The other defects below were
exposed by shared renderer-neutral evidence and mechanical audits, so they remain ledger entries
without falsely reclassifying an unrelated EasyGL example source.

Classification is a triage statement, not proof of parity. The 128 “covered” programs map to
explicit, filename-enumerated SDL GPU family tests or shared graphics regressions; an unknown new
classic filename is `unclassified` and fails the checker instead of inheriting a generic covered
fallback. All 32 renderer-neutral parity sources are registered, and `SDLGPU-81` closes the
exhaustive deferred-lifetime sweep.
The 52 direct rows now
include exact EasyGL presentation/lifecycle sources compiled under SDL GPU by `SDLGPU-68`, the
packed/DXT/format-refusal sources from `SDLGPU-69`, cube and volume sources from `SDLGPU-70/71`,
the exact RenderTarget2D properties source from `SDLGPU-72`, and both cube property/depth sources
from `SDLGPU-73`, plus the effect, buffer/draw and five model sources added by `SDLGPU-77/78/79`.
The only two feature-missing sources are EasyGL's occlusion-query programs, retained as explicit
`⛔` reference differences rather than falsely called covered (`SDLGPU-80`); the
old instanced-model source is explicitly CNAEXT `ShaderEffect` and is classified `out`, while
ordinary compiled Effect instancing is now verified. A row moves
whenever implementation evidence disproves its classification.

### EasyGL defects discovered by the SDL GPU parity oracle

| ID | Evidence and disposition |
|---|---|
| `EASYGL-PARITY-1` | `EasyGLRenderer::ApplyBlendState` disables blending whenever both RGB and alpha use One/Zero factors, without also requiring both equations to be Add. The SDLGPU-58 equation oracle demonstrated that ReverseSubtract then copies the source under EasyGL instead of evaluating `0 - source`; SDL GPU now evaluates the public XNA equation correctly. This EasyGL defect is not copied and is not an SDL parity blocker. A future EasyGL-local task should add the same two regression legs and include the blend functions in its opaque shortcut. |
| `EASYGL-PARITY-2` | EasyGL's default cube classifier inherits the Texture2D verdict and therefore reports Bgr565/Bgra5551/Bgra4444 Supported, but `EasyGLTextureCubeRenderer::CreateResources` allocates RGBA8 for every non-DXT cube and the public `TextureCube` exposes no packed-element transfer overload. Construction thus promises a packed cube that cannot be faithfully written or read. SDL GPU deliberately refuses these three formats; copying the false acceptance would reproduce a reference defect, not parity. |
| `EASYGL-PARITY-3` | `EasyGLRenderTargetRenderer::GetData` calls `glReadPixels` without setting `GL_PACK_ALIGNMENT=1`, then treats the destination as tightly packed. An exact 5×3 `HalfSingle` readback therefore asks GL to write 12 physical bytes per 10-byte logical row into the public 30-byte buffer; the first row comparison fails and the overwrite can corrupt the following test. The same oracle passes at a 6-pixel alignment-safe width, while SDL GPU also passes the original 5×3 transfer exactly. SDL retains tight format-sized staging rather than copying this EasyGL defect; a future EasyGL-local fix must save, set and restore pack alignment. |
| `EASYGL-PARITY-4` | `easygl_render_target_usage_test.cpp` calls public `GraphicsDevice::GetBackBufferData` while a `RenderTarget2D` is bound and expects the target's pixel. Current EasyGL obliges because `ReadBackbuffer` leaves the bound FBO as its read source; the source passes 3/3 and returns its green target. FNA `GraphicsDevice.GetBackBufferData` unconditionally calls `FNA3D_ReadBackbuffer`, whose OpenGL backend binds the actual backbuffer (or its resolve) before `glReadPixels`, and whose SDL_gpu backend downloads `fauxBackbufferColorTexture`. SDL GPU correctly keeps backbuffer and target readback separate. The valid usage oracle is the renderer-neutral `RenderTargetSemantics` pair using `RenderTarget2D::GetData`; a future EasyGL-local fix should make `ReadBackbuffer` select the real backbuffer and update/remove the defective example. |
| `EASYGL-PARITY-5` | Public `RenderTarget2D` inherits all `Texture2D::SetData` overloads, exactly as FNA does: FNA forwards them to `FNA3D_SetTextureData2D` even when the texture is a render target. CNA's shared target path likewise calls its existing `ITextureRenderer`, but `EasyGLRenderTargetRenderer` inherits the empty `UpdatePixels`/`UpdatePixelsLevel` defaults, so every accepted target upload is silently discarded. SDL GPU had the same 2D defect and fixed it in `SDLGPU-74`; its byte-exact regression covers full/partial level zero, authored/generated mip preservation and 2/4/8/16-byte target texels. This is intentionally not hidden to imitate EasyGL: a future EasyGL-local task must implement format-aware uploads into `colorTex_` without replacing the FBO-owned renderer. |
| `EASYGL-PARITY-6` | `EasyGLRenderer` consumes and clamps construction-time backbuffer MSAA correctly, but inherits `ApplyMultiSampleCount`, so `GraphicsDevice::Reset`/`GraphicsDeviceManager.PreferMultiSampling` cannot change it after construction. `easygl_msaa_change_test.cpp` explicitly treats that inability as its expected result. FNA resets and writes back the device-clamped count; SDL GPU now implements that XNA/FNA contract in `SDLGPU-85`, so the EasyGL-specific expectation is classified as a defect rather than compiled under SDL GPU. The shared standalone fixture still uses the exact same ordinary construction source for both renderers. |
| `EASYGL-PARITY-7` | `easygl_rt_roundtrip_test.cpp` repeats the bound-FBO assumption from `EASYGL-PARITY-4`: it calls `GraphicsDevice::GetBackBufferData` while each render target is active and labels the returned target pixel an RT readback. FNA defines this as backbuffer readback regardless of the active target; `RenderTarget2D::GetData` is the target API. The example is now explicitly classified as the same EasyGL defect, while SDL GPU's target/backbuffer transition and independent readback contracts remain the valid oracle. |
| `EASYGL-PARITY-8` | EasyGL's stock SpriteBatch decides a viewport is custom whenever the current GL rectangle differs from the complete physical target. A default Letterbox viewport necessarily does differ: the reproduced 800x480/240x240 case is the correct physical `(160,0,480x480)` presentation rectangle. `EasyGLSpriteBatchRenderer::FlushBatch` therefore projects the 240x240 logical sprite over a 480x480 *physical* extent and covers only half of the presentation rectangle. Its compiled-effect branch has the opposite default-framebuffer error: it resets the viewport to the entire 800x480 drawable and therefore includes the bars. SDL GPU recovers the logical projection extent from the mapped physical viewport and passes the exact shared public test; copying either EasyGL route would regress `SDLGPU-68`. |
| `EASYGL-PARITY-9` | `EasyGLRenderer::ReadBackbuffer` flips top-left XNA coordinates with `GetViewportSize`, which is the logical virtual height under Letterbox, instead of the physical default-framebuffer height. In the reproduced 800x480/240x240 case every request at public `y >= 240` produces a negative GL Y; the zero-filled failed reads made the shared test's old coarse scanner report `(0,245)-(792,476)` as if ink had landed there. That rectangle is a readback artifact, not rendered ink. `GraphicsDevice::GetBackBufferData` addresses the actual backbuffer and SDL GPU's proxy uses its physical extent, so SDL remains correct and the EasyGL bug is not copied. |

## Executable EasyGL-parity backlog

Each task below is one commit. Every implementation task includes its test/oracle and plan evidence
in the same commit. `⬜` open, `🟨` in progress or evidenced but not accepted, `✅` accepted, `⛔`
underlying API limitation proven after reasonable emulation analysis.

### SDLGPU-55 — establish a reproducible authoritative baseline ✅

- **Problem/public behavior:** the historical plan cannot reproduce current renderer tests on a
  machine without accessible X11 and contains obsolete capability claims.
- **Evidence:** EasyGL=246 sources, SDL GPU=38 sources/85 CTests; old x11 run is environmental while
  SDL offscreen/Vulkan gives the real 77/85 result above.
- **Location:** this dated plan section, classification CSV/checker, renderer-local example CMake.
- **Acceptance/test:** CMake defaults remain x11; the stable build can select offscreen; checker sees
  246/246 and no unclassified rows; record exact builds, drivers, failures and live source paths.
- **Result (2026-09-09):** accepted. Stable SDL GPU targeted build succeeded; real offscreen/Vulkan
  suite completed 77/85 with all eight failures classified above; focused EasyGL oracle completed
  27/27; the classification checker reports 246 rows and zero unclassified; `git diff --check`
  passes. No clean rebuild was used.

### SDLGPU-56 — repair the constructor exception-safety oracle ✅

- **Problem/public behavior:** the test stops after 23 shaders although construction now owns 25,
  leaving two allocation failure points untested; production transactionality is public lifecycle
  behavior.
- **Evidence:** EasyGL resource-registry tests cover construction/destruction; SDL baseline is
  234/264 with every failure balanced and both newer PBR vertex shaders absent from the oracle.
- **Location:** SDL GPU construction failure enum/test and renderer construction shader list.
- **Acceptance/test:** derive or explicitly share the authoritative count, inject every acquisition,
  prove zero leaked handles and successful recovery, with validation fatal.
- **Result (2026-09-09):** accepted. Added distinct failure points for the PBR vertex-color and
  skinned-PBR vertex-color shader acquisitions (they previously reused earlier points and were
  unreachable by injection), tied production `ConstructionShader::Count` to the public-internal
  contiguous failure-point count with `static_assert`, and made the test derive both its case count
  and success expectation from that value. Incremental target build succeeded;
  `SdlGpu_ConstructorExceptionSafety` passes in 14.93 s on offscreen/Vulkan with validation fatal.

### SDLGPU-57 — make capability and numeric-limit reporting truthful ✅

- **Problem/public behavior:** SDL GPU inherits broad `true`/unbounded defaults for unsupported
  wireframe, instancing, occlusion and device-dependent limits.
- **Evidence:** EasyGL switches every capability from live GL facts; SDL has no override while its
  corresponding factories/draw hooks are null/throw/default.
- **Location:** `SdlGpuRenderer::{SupportsCapability,GetMax*}` and capability tests.
- **Acceptance/test:** every public capability agrees with a successful discriminating operation or
  a truthful refusal; A→feature invocation produces no false-supported state.
- **Result (2026-09-09):** accepted. SDL GPU now enumerates every `GraphicsCapability` instead of
  inheriting catch-all true, runtime-probes 2× color+depth MSAA, reports the current one-stream
  encoder limit, and contributes exact qualitative limitations. MRT reports false until distinct
  outputs exist, while its already-supported multi-attachment bind/clear path remains callable.
  Compiled effects follow the optional build's real `SupportsCompiledEffects()` result. Incremental
  build succeeded; `SdlGpu_Smoke` passes 28/28 capability/lifecycle checks and the relinked
  `SdlGpu_MRT` compatibility test also passes; the non-production SDL ratchet remains at/below its
  existing budget.

### SDLGPU-58 — exhaustively verify immutable blend/depth/stencil/raster keys ✅

- **Problem/public behavior:** partial tests cannot prove every XNA state or cache identity.
- **Evidence:** EasyGL has factor/function/compare/mask/op/two-sided suites and shared equation
  fixtures; SDL descriptors look complete but are not fully discriminated.
- **Location:** pipeline key/creation and shared parity fixtures.
- **Acceptance/test:** all blend factors/functions/separate alpha/BlendFactor/write masks/sample
  mask, all depth compares, stencil masks/ops/two-sided/reference, cull/scissor/bias pass pixel
  oracles including A→B→A, with no validation output.
- **Result (2026-09-10):** accepted, with the one rigorously bounded SDL_gpu limitation called out
  below. Mechanical inspection confirms that every pipeline-static field used by the SDL
  descriptors is also represented in `PipelineCacheKey`: topology; depth enable/write/function;
  active color count/formats; sample count; depth/stencil format; blend enable and all six separate
  factor/equation fields; every active write mask; cull/fill; normalized constant/slope bias; and
  stencil enable, masks, front operations/function, two-sided enable and back operations/function.
  The dynamic blend constant, stencil reference, viewport and scissor are captured per queued draw
  and replayed through SDL_gpu's dynamic commands instead of being incorrectly baked into or read
  late from that key.

  The shared blend oracle now exhausts all 13 `Blend` values independently as color source,
  color destination, alpha source and alpha destination; all five `BlendFunction` values for both
  color and alpha; all 16 slot-0 channel masks; independent alpha/color fields; a nontrivial
  `BlendFactor`; and function A→B→A. This exposed and fixed one real SDL bug: One/Zero factors only
  permit the disabled-blend copy shortcut when both equations are Add. Two SDL-specific pixel legs
  retain ReverseSubtract One/Zero for color and alpha, because the old shortcut would respectively
  return the source color/source alpha instead of zero. The same experiment exposed
  `EASYGL-PARITY-1` above; the EasyGL bug is documented rather than copied.

  The depth fixture distinguishes all eight compares with the unique nearer/equal/farther
  signatures. The new stencil fixture does the corresponding reference-less/equal/greater sweep
  for all eight stencil compares and includes Always→Never→Always restoration. Existing shared and
  verbatim EasyGL sources additionally discriminate all eight stencil operations (including
  wrapping versus saturation at zero), read/write masks, fail/depth-fail/pass operations,
  two-sided front/back state, dynamic reference, depth-write enable, culling, scissor, viewport and
  bias. Four complete shared output frames—blend, depth, stencil operation and stencil compare—are
  byte-identical between EasyGL and SDL GPU (**maximum channel difference 0**).

  `BlendState.MultiSampleMask` is the sole unavoidable field in this family. Vendored SDL 3.5's
  `SDL_gpu.h` lines 1862–1863 mark `SDL_GPUMultisampleState::sample_mask` and `enable_mask` reserved
  for future use and require `0`/`false`. Rewriting arbitrary compiled/stock fragment shaders to
  discard by sample ID would not reproduce coverage before depth/stencil or all sample side
  effects, so there is no correct general renderer-side emulation. EasyGL also explicitly leaves
  non-default masks unimplemented. The all-ones default remains equivalent; non-default masks are
  classified `⛔`, never falsely claimed as working.

  Targeted incremental builds only were used. The SDL state selection passes **14/14 CTests** on
  offscreen Vulkan: five shared state fixtures, eight verbatim EasyGL sources, and the expanded
  20/20 SDL render-state regression. All have validation-error/warning fatal patterns and emitted
  none. `SdlGpu_MRT`, `SdlGpu_ColorWriteChannels` and the 67/67 `SdlGpu_DepthBias` control also pass,
  making the final focused SDL run **17/17**. The five shared controls pass **5/5** on EasyGL/Xvfb.
  The audit classifier remains complete at 246/246 with zero unclassified rows; eight now-direct
  state sources moved to direct parity. This periodic re-audit also moved the completed SDLGPU-59
  declaration and SDLGPU-64 sampler leads, plus the depth-bias source, out of stale gap buckets;
  custom ShaderEffect instancing remains correctly owned by `SDLGPU-79`.

### SDLGPU-59 — resolve stock vertex inputs from VertexDeclaration semantics ✅

- **Problem/public behavior:** valid declarations are selected by byte stride/fixed layouts and
  stride-28 DualTexture TEXCOORD0/1 is rejected.
- **Evidence:** EasyGL binds `(VertexElementUsage,index,offset)`; two SDL baseline tests skip then
  abort on precisely this declaration; shared `StockVertexSemantics.hpp` already encodes fidelity.
- **Location:** SDL stock program/pipeline vertex-input resolution and affected fixtures.
- **Acceptance/test:** reordered/padded/position-normal/lit-color/independent-UV declarations render
  the same pixels as EasyGL; missing required semantics throw precise errors; fixture exceptions
  safely unbind targets.
- **Result (2026-09-10):** accepted. Every classic single-stream stock family now resolves native
  vertex attributes by `(usage, usageIndex)` with the declaration's own format, offset and stride.
  The resolved layout—including optional inputs supplied by the reference-compatible neutral
  `(0,0,0,1)` record—is part of immutable pipeline identity. Program selection likewise uses
  Normal/Color/TexCoord semantics rather than ambiguous record sizes, and DualTexture consumes
  TEXCOORD0 and TEXCOORD1 independently. Lit vertex colour is carried to the shader and gated by
  `VertexColorEnabled`; absent ordinary stock textures reach the existing neutral-white binding.
  A declaration lacking mandatory POSITION0 throws a precise `NotSupportedException`. The two
  previously aborting fixtures now restore their render target and device state even if a future
  draw diagnostic throws.

  Verification uses identical renderer-neutral sources on EasyGL and SDL GPU. Reordered/padded
  vertex semantics, lit vertex colour, position+colour program selection, and independent dual UV
  frames are byte-identical (**maximum channel difference 0** for all four). Those four tests,
  `SdlGpu_3D` (including the missing-POSITION0 rejection), and both previously blocked sampler
  contracts pass **7/7 CTests** on Vulkan with validation diagnostics fatal. Seven broader classic
  effect regressions (Effects, EnvironmentMap, EnvironmentMapEmissive, Skinned,
  SkinnedEffectVertexColor, SkinnedEffect_WorldNormal and Effects_Fog) also pass. The separate
  CNAEXT PBR fixtures remain outside this parity task; their existing current-tree failures were
  reproduced and traced through their unchanged stride-derived queue/issue path rather than hidden
  or misreported as classic evidence. Multi-stream input and ordinary BasicEffect instancing are
  now closed by `SDLGPU-60`, while deeper Skinned declaration coverage remains with `SDLGPU-77`.

### SDLGPU-60 — implement ordinary XNA instancing and multi-stream input ✅

- **Problem/public behavior:** `DrawInstancedPrimitives` and multiple `VertexBufferBinding`s are
  ordinary XNA 4.0 but SDL inherits a throwing draw and false max-stream default.
- **Evidence:** EasyGL uses stream offsets/frequencies/divisors; SDL_gpu exposes multiple vertex
  bindings plus `SDL_GPU_VERTEXINPUTRATE_INSTANCE`.
- **Location:** buffer bindings, SDL vertex input/pipeline keys, deferred draw command.
- **Acceptance/test:** shared split-stream and instanced fixtures pass, including nonzero vertex/
  base/start/instance offsets, per-instance signatures, frequency and lifetime snapshots.
- **Result (2026-09-10):** accepted. The ordinary stock-effect dispatcher now collects every
  bound per-vertex stream, selects the shader family across the complete declaration set and
  resolves each consumed semantic to its declaring stream's own format, offset and stride.
  Pipeline identity includes the resolved stream table and input rate; replay binds the densely
  resolved buffers plus the optional neutral record. Each queued command copies every stream at
  the public draw call, so later rebinding, updates, wrapper destruction and allocator address
  reuse cannot alter an issued draw.

  `DrawInstancedPrimitivesEx` now resolves POSITION0/COLOR0 semantically and the four world-matrix
  columns positionally across any per-instance declarations, matching EasyGL's declaration-order
  rule. SDL_gpu's native `SDL_GPU_VERTEXINPUTRATE_INSTANCE` supplies the step mode. Because its
  descriptor has no divisor field, XNA `InstanceFrequency > 1` is emulated exactly by materializing
  source record `VertexOffset + instance / frequency` once per submitted instance. The native
  indexed draw preserves 16/32-bit indices, `startIndex`, signed `baseVertex` and each stream's own
  element offset. Missing matrix columns, incompatible formats and the still-unimplemented
  compiled/custom-effect instanced combinations raise named exceptions rather than drawing from a
  subset. Capabilities now report multi-stream input and instancing. `SDLGPU-89` later removes the
  historical eight-stream resolver ceiling and verifies the full 16-slot XNA/SDL_gpu width. This closes the core
  geometry path; texture, lighting, AlphaTest, DualTexture, EnvironmentMap, Skinned and compiled
  effect permutations remain explicitly tracked by `SDLGPU-77/79` and therefore keep the
  feature-family matrix at `~` rather than overclaiming whole-effect parity.

  Verification is deliberately redundant: `OrdinaryDrawMultiStreamTest` passes **20/20** and
  `InstancedDrawMultiStreamTest` passes **28/28**, covering two per-vertex plus two per-instance
  streams, non-contiguous/dropped bindings, independent offsets, `vertexStart`, `startIndex`,
  `baseVertex`, 16/32-bit indices, frequencies 1/2/3, A→B→A pipeline/binding sequences, dynamic
  replacement, deferred snapshots and wrapper death. The newly enabled diffuse/vertex-color
  instancing suites pass another **21/21** checks, including non-neutral RGB/alpha multiplication,
  enabled/disabled transitions, packed stride 24, a nonidentity effect World composed after the
  instance World, and queued state/data lifetime. The shared
  `multi_stream_split` and `instanced_draw` sources pass under both renderers and their complete
  RGBA8 dumps are byte-identical (SHA-256 respectively
  `bef17637de93bbc0ce6bfd85f342dafb322968cd1f9ea494d5b29e708b6957d4` and
  `cd2d27772b4d72f808185524fbc87fd7f976924911e306bc9da32c4ad4ae2842`). Both SDL CTests make
  validation diagnostics fatal. The construction failure matrix also passes after adding the new
  instanced shader acquisition, proving transactional release at every stage.

### SDLGPU-61 — verify and advertise FillMode.WireFrame ✅

- **Problem/public behavior:** the parity audit initially classified XNA wireframe as absent and
  the capability profile reported false, despite the live renderer already baking the requested
  fill mode into its immutable pipelines.
- **Evidence:** `FillRasterizerState` maps the queued state snapshot to
  `SDL_GPU_FILLMODE_LINE`; SDL 3.5 exposes that native polygon-line mode. EasyGL's implementation
  is a renderer-side edge expansion, so output—not implementation technique—is the contract.
- **Location:** capability profile/report and the shared EasyGL/WebGPU wireframe fixture.
- **Acceptance/test:** the same asymmetric triangle under Solid and WireFrame has a filled versus
  empty interior, all three wire edges have positive coverage, the images are distinct, the
  capability is true, and validation diagnostics are fatal.
- **Result (2026-09-09):** the unchanged native line-fill implementation passes all ten shared
  discriminators on Vulkan: Solid fills 169/169 interior pixels, WireFrame fills 0/169, all three
  wire-edge probes contain exactly nine lit pixels, and the two interiors differ by 246. The
  renderer now reports `WireFrame=true` and no longer lists it as a limitation; the expanded
  30/30 smoke profile agrees. Both tests treat validation diagnostics as fatal.

### SDLGPU-62 — match PointClamp texel-center sampling for 3D draws ✅

- **Problem/public behavior:** a 3×3 texture scaled to 10×10 changes texels at different pixels.
- **Evidence:** shared `SdlGpu_PointSamplingContract` passes 145/146; only non-integer 3D mapping
  differs from its EasyGL/XNA oracle.
- **Location:** stock textured vertex shader UV/pixel-center transform and sampling fixture.
- **Acceptance/test:** exact 100-pixel signature matches the EasyGL control without regressing the
  already exact SpriteBatch and integer-scale cases.
- **Result (2026-09-09):** every queued stock 3D family now post-multiplies its WVP by EasyGL's
  measured 63/64-of-a-pixel Direct3D 9 correction in X and Y, using the exact viewport captured at
  draw time. Multisampled destinations deliberately retain the unshifted matrix, matching EasyGL's
  established coverage rule. `SdlGpu_PointSamplingContract` passes 146/146: the non-integer U2 leg
  moved from 19 mismatches to zero while all SpriteBatch, integer-scale, custom-viewport, address,
  minification and linear-filter controls remain exact. The 3D, Effects, EnvMap, EnvMapEmissive,
  Skinned, BasicEffect fog and DepthBias regressions pass. The MSAA target control exposed an
  unrelated invalid negative-Z depth fixture left latent by `SDLGPU-65`; it is isolated as
  `SDLGPU-84`. Native validation diagnostics are fatal.

### SDLGPU-63 — verify sampler identity at descriptor capacity ✅

- **Problem/public behavior:** determine whether rotating 27 sampler states or 256 live textures
  aliases native sampler/texture bindings or exhausts a hidden descriptor pool.
- **Evidence:** the initial audit incorrectly read 12/27 and 114/256 exact samples as failures. The
  fixture intentionally rotates all nine `TextureFilter` values: five magnify linearly, so 15/27
  sampler-only draws and 142/256 non-uniform texture draws must interpolate; the remaining Point-like
  cases must reproduce byte-encoded identities exactly.
- **Location:** sampler cache key, binding tables, descriptor-capacity rollover and resource cycling.
- **Acceptance/test:** every adversarial sample remains exact across capacity/rollover and A→B→A;
  bounded cache/resource behavior and validation cleanliness are proved.
- **Result (2026-09-09):** all **29/29** checks pass on the real Vulkan SDL_gpu path. The test proves
  1/2/31/32/63/64/65/66/96/128/256 simultaneously-live textures; all 27 filter/address
  combinations; 256 live texture+sampler pairs; Point-vs-Linear native distinctness; repeated
  identical state; 256 create/draw/read/destroy cycles; 65 live render targets; 96 DualTexture,
  70 AlphaTest and 70 EnvironmentMap effect resources; 96 SpriteBatch flushes and one 96-sprite
  batch; indexed/non-indexed/user draws; 320 released-resource cycles; recreation/interleaving; and
  70 live cubes. Every wrong/unreadable counter is zero. `SamplerCacheKeyEXT` compares and hashes
  filter, U/V/W address modes, clamped anisotropy, MaxMipLevel and the exact LOD-bias bit pattern.
  Vulkan validation errors and warnings are now fatal for this contract. No production defect was
  present and no renderer code was changed.

### SDLGPU-64 — propagate every ordinary sampler field on every stock path ✅

- **Problem/public behavior:** MaxMipLevel, MipMapLevelOfDetailBias and volume AddressW are accepted
  but not captured by all stock/SpriteBatch commands.
- **Evidence:** EasyGL effect corpus varies these; SDL header explicitly says only compiled effects
  receive extended mip state while stock commands snapshot filter/address/anisotropy.
- **Location:** sampler snapshots in all stock effect and sprite command records, volume sampling.
- **Acceptance/test:** shared filter/max-mip/LOD-bias/sprite-state fixtures plus AddressU/V and
  anisotropy slot tests yield deliberately different mip/edge colors; AddressW reaches the native
  descriptor without aliasing. The three-colour volume observation is retained by the dependent
  volume-sampling task `SDLGPU-79`, because this task does not invent a new public shader API.
- **Result (2026-09-09):** every SDL GPU stock command now snapshots filter, U/V/W address modes,
  anisotropy, MaxMipLevel and LOD bias per texture slot and passes the complete description to the
  already lossless native sampler cache. This includes both `DualTextureEffect` slots, the base and
  cube samplers of `EnvironmentMapEffect`, every built-in textured family, PBR preservation paths,
  and SpriteBatch. `SDL_GPUSamplerCreateInfo::address_mode_w` now uses the captured W mode rather
  than a hard-coded clamp. The shared SpriteBatch contract was also corrected: `Begin` assigns the
  resolved sampler to `GraphicsDevice.SamplerStates[0]` as FNA does, and a new complete common hook
  carries all seven properties instead of silently dropping four. EasyGL and WebGPU adopted the
  hook in the same task to preserve their ordinary-XNA behavior; WebGPU's sprite shader uses its
  existing `textureSampleBias` emulation.

  Verification is discriminating, not construction-only. On the real SDL_gpu Vulkan path the four
  shared sampler fixtures pass, including authored mip colours under natural, +1 and -2 bias and a
  MaxMipLevel clamp; the revised SpriteBatch fixture also compares all seven retained device
  properties. Existing `SdlGpu_SamplerState`, both environment-map cube sampler contracts and the
  29-check descriptor-capacity/lifetime stress all pass (**8/8 CTests total**) with validation
  diagnostics fatal. The same four fixtures pass on EasyGL. WebGPU shader validation and the
  corrected fixture pass on the host GPU, and direct EasyGL↔WebGPU frame comparison is byte-exact
  (max difference 0). `SDLGPU-59` subsequently removed the stock/dual contracts' stride-28
  declaration blocker; both now finish successfully, preserving their sampler evidence.
  `SDLGPU-79` supplies the final end-to-end AddressW proof: a compiled volume sample at W=1.25
  with Wrap returns the red first slice, where a lost/hard-coded Clamp would return the blue second
  slice, and the same draw remains correct after the public volume is destroyed before replay.

### SDLGPU-65 — honor SpriteBatch depth and slope bias ✅

- **Problem/public behavior:** SpriteBatch under a positive depth bias fails to move behind the
  reference geometry.
- **Evidence:** EasyGL depth-bias oracle passes; SDL baseline is 56/57 while 3D bias and A→B→A pass.
- **Location:** sprite pipeline key/descriptor depth-bias fields.
- **Acceptance/test:** positive, negative, slope and combined sprite cases pass against coplanar and
  sloped geometry, target-format A→B→A, validation clean.
- **Result (2026-09-09):** accepted for every currently supported depth attachment. FNA3D's own
  SDL_gpu driver showed the missing semantic conversion: XNA supplies normalized depth, while
  SDL_gpu consumes native r-units (`D16: 2^16−1`, `D24: 2^24−1`, `D32F: 2^23−1`). Every SDL GPU
  pipeline now converts against its actual depth format, includes the converted value in immutable
  cache identity, and explicitly enables XNA depth clipping. Sprite vertices now carry their
  projected `layerDepth` (`0.5 - depth/2`) instead of the old hard-coded near-plane Z=0. The
  discriminators use a normalized `0.0005` constant that would be ineffective if passed through,
  and cover positive/negative/combined bias, flat slope, A→B→A, deferred state lifetime and
  nonzero layer depth and positive/negative/combined bias on a public transform-created sloped
  sprite. Incremental build succeeded; `SdlGpu_DepthBias` passes 67/67 on Vulkan/D32F
  with validation enabled. Focused `SdlGpu_Smoke`, `SdlGpu_2D`, `SdlGpu_3D`, and
  `SdlGpu_ShaderEffect` regressions also pass. Cross-format A→B→A remains assigned to the format
  tasks because this baseline exposes only D32F/S8 and SDL GPU does not yet create other requested
  depth formats.

### SDLGPU-66 — isolate ordered clears and draws per cube face ✅

- **Problem/public behavior:** the ordered-clear baseline appeared to show work queued for one cube
  face retroactively affecting a supposedly untouched face.
- **Evidence:** baseline T4 was 45/46 because an unwritten face read as the same yellow used by a
  later face clear. EasyGL returned transparent black, but new render-target contents are undefined;
  neither value proves or disproves isolation. Existing SDL tests already paint all six faces with
  distinct values and preserve face 0 after five later writes.
- **Location:** shared ordered-clear oracle; no renderer behavior was changed.
- **Acceptance/test:** seed the control face with a distinct known value, then issue draw/clear
  sequences against two other faces; all three exact readbacks, six-face tests, repeated switches,
  pass-boundary tests and validation remain clean.
- **Result (2026-09-09):** T4 now initializes PositiveY to cyan before the NegativeX and PositiveZ
  subject cycles and requires byte-exact cyan afterward. SDL GPU passes all 46 ordered-clear checks;
  EasyGL passes all 49 of its enabled checks. The existing SDL cube target suite also proves six
  distinct faces, and the depthless-cube suite covers all faces plus producer/consumer switching.
  Its depth discriminator was also corrected from invalid identity-projection Z=−0.5 (outside
  XNA/DirectX's 0..W clip volume) to valid near/far depths 0.25/0.75; it passes 7/7 without undoing
  `SDLGPU-65`'s XNA depth clipping. The SDL ordered-clear CTest now makes native validation
  diagnostics fatal.

### SDLGPU-67 — verify viewport/scissor/depth-range capture across target changes ✅

- **Problem/public behavior:** deferred draws must retain the exact viewport, MinDepth/MaxDepth and
  scissor active at issue time and restore full target/backbuffer viewports on switches.
- **Evidence:** many SDL targeted tests pass; EasyGL has subregion/state/RT switch controls but the
  complete resize/logical-resolution sequence is not shared.
- **Location:** draw snapshots, target-pass setup and viewport conversion.
- **Acceptance/test:** adversarial A→B→A viewport/scissor/depth-range sequences across differently
  sized RT2D/cube/backbuffer and resize match EasyGL invariants.
- **Result (2026-09-10):** accepted without a production change: the live draw queue already
  snapshots viewport bounds, MinDepth/MaxDepth, scissor enable and rectangle per public draw. The
  stale SDL contracts in both exhaustive deferred fixtures were corrected to exercise the real
  proxy-backed backbuffer readback; they now complete **39/39** viewport and **47/47** scissor
  checks instead of skipping the backbuffer half. Six missing shared/oracle programs are now
  registered under SDL GPU, including the exact EasyGL `viewport_state`, `viewport_subregion` and
  `scissor` sources. The existing renderer-neutral cube fixture now dirties state and proves the
  exact cube 32×32 → RT2D 19×23 → backbuffer viewport, depth-range and scissor resets. The focused
  SDL GPU suite passes **13/13** with `VUID`/validation diagnostics fatal; the corresponding seven
  EasyGL oracles pass **7/7** on Xvfb. The corpus checker remains 246/246 with zero unclassified and
  promotes all three reused EasyGL sources to direct parity.

### SDLGPU-68 — close presentation, resize, reset and minimize lifecycle parity ✅

- **Problem/public behavior:** GDM/PresentationParameters changes, zero-size windows, VSync modes,
  backbuffer dimensions/readback and logical transforms must remain coherent.
- **Evidence:** before the fix SDL inherited `GetDefaultViewportRect()`, so the common discriminator
  passed only 2/6: Letterbox returned logical `(0,0,400×160)` instead of centred physical
  `(0,80,800×320)`, Overscan returned the same logical rectangle instead of
  `(-200,0,1200×480)`, and Stretch/FixedHeight returned 400×160 instead of 800×480. SDL also
  inherited `OnSurfaceChanged()`, ignored display scale in both coordinate transforms and exposed
  no requested interval fact. `SetVirtualResolution()` additionally overwrote physical drawable
  dimensions while Native mode was active, conflating two separate facts. The live proxy path did
  already provide real readback; this row tested rather than replacing it.
- **Location:** presentation setters, swapchain claim/reclaim, proxy recreation, viewport facts.
- **Acceptance/test:** same public reset/resize/minimize/restore sequences match dimensions,
  transforms, viewport and readback; unsupported present modes report the applied value truthfully.
- **Result (2026-09-10):** SDL GPU now consumes `RendererSurfaceInfo` updates with immutable window
  identity, physical drawable dimensions and normalized display scale; returns the exact physical
  presentation rectangle; keeps virtual and physical dimensions independent; and transforms
  between SDL client coordinates and XNA logical coordinates with HiDPI scale. SpriteBatch now
  snapshots its logical projection extent separately from the mapped physical viewport, so the
  fixed default rectangle does not turn a letterboxed default viewport into a custom sub-viewport
  or let a later resize alter a queued sprite. The common physical-presentation suite moves from
  **2/6 to 6/6**, including pixel-readback coverage of the default letterboxed SpriteBatch viewport.

  The documented successful-null swapchain acquisition path now has a forced real-command-buffer
  oracle: it submits without error, retains the queued clear, renders it on retry and stays usable
  (**3/3**). A deterministic surface/present-mode executable passes **13/13** for Letterbox,
  distinct logical/physical sizes, scaled transforms, bar rejection, zero-size safety, window
  identity and requested-versus-applied intervals. The five pre-existing swapchain/present/resize/
  backbuffer-readback tests remain **5/5**. Six exact EasyGL sources are now compiled under SDL GPU
  and pass **6/6 CTests**: backbuffer resize, reset events, GDM VSync forwarding, MSAA changes,
  PresentationParameters and real OS-window resize. Their EasyGL originals pass **6/6** under
  Xvfb after the stale real-resize fixture explicitly selected the FixedHeightDynamicWidth mode its
  assertions describe. All new SDL tests make `VUID`/validation warnings fatal and emitted none.

  **Exact unavoidable interval difference:** vendored SDL 3.5's `SDL_GPUPresentMode` contains only
  `VSYNC`, `IMMEDIATE` and `MAILBOX`; both synchronized modes present at each vblank, and the API
  exposes neither a half-rate mode nor vblank/presentation-timing control from which the renderer
  could correctly emulate XNA `PresentInterval::Two`. SDL therefore records request `2` while
  truthfully recording applied interval `1`. An Immediate request is applied natively where
  supported and otherwise records its synchronized fallback. No fake timing claim is made.
  Corpus classification remains machine-checked at **246/246** with zero unclassified; all six
  reused lifecycle sources are promoted to direct parity. Registered SDL GPU CTests: **123**.

### SDLGPU-69 — implement ordinary Texture2D surface formats ✅

- **Problem/public behavior:** every non-Color XNA texture format is currently rejected or would be
  stored as RGBA8 despite SDL_gpu exposing native formats.
- **Evidence:** EasyGL packed/DXT/SNORM examples; SDL classifier defaults and texture allocation/
  upload assume four bytes per texel.
- **Location:** format map/classifiers, `SdlGpuTextureRenderer`, transfer/readback conversions and
  compressed-content policy.
- **Acceptance/test:** every ordinary format gets truthful runtime classification; native storage
  is used where exact, renderer conversion where reasonable, and byte/typed SetData/GetData,
  partial rectangles, authored mips, NPOT, compressed blocks and sampling are discriminated.
- **Result (2026-09-10):** accepted. SDL GPU now explicitly classifies all twenty classic
  `SurfaceFormat` values for Texture2D: the exact nine-format EasyGL set (`Color`, the three packed
  16-bit formats, DXT1/3/5 and both signed-normalized formats) is supported, while the other eleven
  are explicitly refused instead of reaching a hard-coded RGBA8 allocation. Packed formats use
  their exact SDL_gpu storage when the device supports it and a lossless logical-to-RGBA8 expansion
  otherwise. DXT uses BC1/2/3 where all required device formats exist and the shared CNA decoder
  otherwise; the content loader now retains compressed assets only when that native-storage fact is
  true. `NormalizedByte4` uses native RGBA8 SNORM. `NormalizedByte2` expands to RGBA8 SNORM with
  missing B/A channels set to signed one, preserving D3D9/XNA sampling semantics rather than SDL's
  native two-channel zero/one defaults.

  Format-sized transfers no longer assume four bytes per texel. Exact DXT block shadows preserve
  caller-authored data across byte readback, block-aligned partial updates, NPOT edge blocks and
  authored sub-4x4 mip levels; GPU uploads use format-aware SDL sizes and tightly packed rows.
  Three exact EasyGL sources now compile unchanged under SDL GPU and pass: DXT sampling (3/3),
  packed sampling (3/3), and format construction/refusal (30/30). Forced packed and DXT fallback
  registrations pass the same pixel oracles. The SDL-specific native/fallback matrix passes 9/9 on
  both paths, including all classifier rows and the `NormalizedByte2` blue-versus-black missing-
  channel discriminator. The unchanged compressed-content loader fixture passes 3/3 both natively
  and under forced decode, covering DDS DXT1, XNB DXT5, four authored mips and SpriteBatch sampling.
  Shared graphics format/constructor tests pass 17/17. All nine registered format CTests make
  validation diagnostics fatal and emit none. Corpus classification remains 246/246 with zero
  unclassified; the three format sources move from feature-missing to direct parity. Registered SDL
  GPU CTests: **132**.

### SDLGPU-70 — implement ordinary TextureCube formats ✅

- **Problem/public behavior:** cube creation ignores `surfaceFormat`, transfers assume RGBA8 and
  compressed cube upload inherits false.
- **Evidence:** EasyGL six-face/mip/partial and DXT paths; SDL_gpu supports cube textures with the
  same native format enum.
- **Location:** cube classifier/resource format/upload/readback/sampling.
- **Acceptance/test:** all legal ordinary cube formats are truthful; six deliberately distinct
  faces, partials, authored mips and DXT blocks round-trip/sample; illegal SNORM cubes refuse.
- **Result (2026-09-10):** accepted. `CreateTextureCube` now preserves `surfaceFormat`; the cube
  classifier explicitly supports Color and DXT1/3/5, explicitly refuses the other 16 classic
  values, and the compressed-transfer predicate is no longer inherited false. DXT cubes allocate
  native BC1/2/3 when the concrete cube usage is supported and otherwise allocate RGBA8 backed by
  the shared CPU decoder; `CNA_SDLGPU_FORCE_DXT_FALLBACK=1` exercises the latter even on this BC
  device. Per-face/per-level exact block shadows reconstruct full logical levels for partial
  updates, preserving untouched NPOT edge blocks identically on both storage paths, while public
  `GetData(Color*)` decodes and crops the requested rectangle. Plain Color and compressed cube mips
  are now explicitly authored: the old full-level-0 `SDL_GenerateMipmapsForGPUTexture` call was an
  observable XNA/EasyGL mismatch because SDL regenerates all six faces and overwrote mips already
  authored on earlier faces. The unchanged EasyGL mip oracle exposed that defect and passes after
  its removal.

  Eight new validation-fatal registrations pass: four exact EasyGL sources (six faces 96/96,
  partial/startIndex 114/114, authored mips 126/126, DDS content load 7/7), the shared compressed
  cube sample/readback fixture on native and forced-decode paths, and the SDL cube format matrix on
  both paths (11/11 each: exact classifier boundary, DXT1/3/5, six distinct faces, NPOT partial
  blocks and a sub-4x4 authored DXT5 mip). The shared sample fixture now supplies its required
  neutral white EnvironmentMapEffect base texture, preventing two blank frames from satisfying its
  agreement check; the rebuilt shared source also passes 4/4 under EasyGL on temporary Xvfb, so
  this harness correction preserves the oracle renderer. The existing SDL Color cube regression
  passes 5/5 with its former implicit-mip
  assertion replaced by the discriminating authored +X mip → later -X level-0 preservation
  sequence. Graphics tests pass 59/59 across every `*TextureCube*` suite, including construction/use
  agreement for each DXT format and the named surface-refusal matrix. The updated Texture2D matrix
  also passes 9/9 twice and proves the process-wide compressed-content policy agrees with physical
  BC support for both 2D and cube resource kinds. All 11 focused CTests emitted no Vulkan validation
  diagnostics. Corpus classification is 246/246 with zero unclassified; the four cube sources move
  from covered to direct parity. Registered SDL GPU CTests: **140**.

### SDLGPU-71 — implement ordinary Texture3D format semantics and repair the public seam ✅

- **Problem/public behavior:** public `Texture3D` unconditionally calls the Color-only framework
  validator and SDL then ignores the forwarded format.
- **Evidence:** EasyGL has a format-bearing volume factory; SDL_gpu exposes 3D textures in the same
  format system, so the public seam—not an `EXT` suffix—makes this in scope.
- **Location:** shared Texture3D classifier call path (minimal common correction) and SDL volume
  storage/transfers.
- **Acceptance/test:** profile+renderer classification mirrors Texture2D; every format in the
  reference volume-storage surface handles dimensions/mips/partial boxes/readback with its exact
  forwarded identity or is truthfully refused; EasyGL plus SDL are regression tested after the
  shared change. End-to-end ordinary compiled-effect volume sampling remains part of the compiled
  effect contract in `SDLGPU-79`, rather than making this format task depend on a separate shader
  execution path.
- **Result (2026-09-10):** accepted at the live reference surface's actual boundary. The deeper
  audit found that neither renderer supports a non-Color ordinary volume today: CNA's public
  `Texture3D` transfer overloads are Color-shaped, the old shared validator permits Color only,
  and `EasyGLTexture3DRenderer` ignores its internal `surfaceFormat` argument while allocating
  every level as RGBA8. Expanding SDL_gpu to packed, BC or float volumes would therefore create a
  new unrepresented public transfer contract rather than EasyGL parity. Instead, the common
  renderer contract now has a resource-specific `ClassifyTexture3DFormatEXT` hook whose safe
  default preserves all other renderers' Color-only behavior. The public constructor applies the
  graphics-profile gate and then that verdict. SDL GPU explicitly supports Color, refuses every
  other one of the 20 classic values with `NotSupportedException`, forwards the accepted ordinal
  into its concrete resource, records it for diagnostics, and defensively rejects any non-Color
  direct factory use. The format argument is no longer accepted and ignored.

  The SDL regression checks the exact 1-supported/19-refused classifier boundary, public named
  refusal for all 19, concrete format forwarding, NPOT dimensions, cumulative independent volume
  writes, authored mips, partial boxes/readback and A→B→A resource sequences. Four new
  validation-fatal CTests compile the verbatim EasyGL slice, asymmetric upload, asymmetric
  readback/startIndex and authored-mip sources under SDL GPU; the focused volume slice passes
  **6/6 CTests** with no captured validation diagnostic. The relevant graphics tests pass **44/44**
  under both SDL GPU and EasyGL (the unsupported-renderer control skips once under each), and the
  EasyGL format contract remains 30/30. The four standalone EasyGL oracles also pass directly on a
  temporary Xvfb display. Volume sampling is not exercised by any classic stock XNA effect; its
  existing compiled-effect resource path remains explicitly owned by `SDLGPU-79`, while the
  CNAEXT ShaderEffect volume program remains out of this task. Corpus classification is 246/246
  with zero unclassified; four volume sources move from covered to direct parity. Registered SDL
  GPU CTests: **144**.

### SDLGPU-72 — preserve requested RenderTarget2D color formats ✅

- **Problem/public behavior:** ordinary `RenderTarget2D(..., SurfaceFormat, ...)` reaches the base
  `CreateRenderTarget2DEXT`, which silently substitutes Color.
- **Evidence:** EasyGL creates/probes Rgba64 and float/half targets; SDL_gpu exposes corresponding
  render-target formats and a usage-support query.
- **Location:** SDL RT format classifier/factory/state/pipeline target-info/readback.
- **Acceptance/test:** each ordinary format is either exact and pixel/byte verified (including >1
  float witnesses) or truthfully refused from a failed runtime probe; pipeline cache separates
  target formats.
- **Result (2026-09-11):** accepted. The renderer now maps the exact nine-format set exposed by
  EasyGL (`Color`, `Rgba64`, `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`,
  `HalfVector4`, `HdrBlendable`) to native SDL_gpu storage and runtime-probes each format for the
  combined color-target and sampler usage required by XNA. The other eleven classic formats are
  explicitly refused. The ordinary format-bearing factory preserves the request; allocation,
  multisample attachment, immutable pipeline target description and typed readback all consume
  the same stored native format/texel size instead of the historical RGBA8/4-byte constants.
  SpriteBatch snapshots the sampled target's public format and applies D3D9/XNA absent-channel
  expansion for one- and two-channel float targets; four-channel textures remain unchanged.

  On the current RADV device all nine exact formats are available. The shared agreement suite
  proves matching query/construction, bind/clear and stock-3D rendering for all nine; typed
  byte-exact readback covers 2-, 4-, 8- and 16-byte texels with deliberately above-one float
  values, and the existing NPOT RGBA32F test remains exact for 13x7, 17x3, 5x11 and 64x1.
  The shared HDR fixture proves above-one half storage, sampling, depth, MSAA and generated mips.
  The channel oracle distinguishes `(R,1,1,1)`, `(R,G,1,1)` and untouched RGBA sampling. The exact
  EasyGL RenderTarget2D properties source now compiles unchanged under SDL GPU and passes all
  18 checks, including Rgba64 typed readback. Four focused SDL CTests pass 4/4 with validation
  warnings/errors fatal; the task-only graphics filter passes with its one correctly skipped MRT
  leg. The broader capability filter retains one pre-existing assertion that expects MRT=true;
  this is explicitly owned by `SDLGPU-75`, not hidden as a target-format failure.

  The new exact numeric source passes under EasyGL at alignment-safe dimensions. Its adversarial
  5x3 HalfSingle predecessor exposed `EASYGL-PARITY-3`; SDL passed that same tight transfer and does
  not reproduce the OpenGL pack-alignment overwrite. Corpus classification remains 246/246 with
  zero unclassified, promotes the exact properties source to direct parity, and registered SDL GPU
  CTests increase to **146**. Only targeted incremental builds/relinks were used.

### SDLGPU-73 — preserve requested RenderTargetCube and depth/stencil formats ✅

- **Problem/public behavior:** cube target color format is dropped and depth/stencil facts partly
  come from generic defaults.
- **Evidence:** EasyGL creates requested cube storage and tests depth formats; SDL uses a combined
  depth/stencil choice without fully reporting the applied public result.
- **Location:** cube EXT factory/state, depth format mapping/facts, pipeline keys.
- **Acceptance/test:** six faces of every supported color format plus Depth16/24/24Stencil8,
  zero/nonzero MSAA, mips/readback/sampling/switching match EasyGL or truthfully refuse.
- **Result (2026-09-11):** accepted. A new cube-specific format classifier now probes the actual
  SDL `TEXTURETYPE_CUBE` color-target-plus-sampler usage instead of assuming a successful 2D probe
  also proves cube support. The format-bearing factory preserves the ordinal, and each cube state
  owns its exact native format and texel size. Allocation, per-face MSAA resolve, mip generation,
  immutable pipeline target descriptions and renderer-local native readback all consume those
  facts. The common Color-shaped `TextureCube::GetData` contract remains RGBA8-only and explicitly
  refuses a typed float cube rather than leaking native bytes through the wrong element type.

  Depth storage is likewise per target for both cube and 2D resources. `None` allocates nothing;
  `Depth16` uses D16; `Depth24` prefers D24 and reasonably falls back to D32 float; and
  `Depth24Stencil8` prefers D24S8 and falls back to D32S8. The applied public XNA semantic remains
  the requested enum, while the actual fixed-point precision or float mantissa (16, 24 or 23 bits)
  drives DepthBias normalization. The live RADV device applies 0/16/23/23 effective bits for
  None/D16/D24/D24S8; its lack of D24 and D24S8 is no longer confused with lack of the ordinary XNA
  capability. Depth-only targets report no stencil plane, and multisample clamping now requires
  both the selected color and depth formats to support the count. Pipeline identity already
  includes the native depth format and now receives each target's real format rather than the
  swapchain's global combined choice.

  The SDL physical matrix passes **40/40**: all 20 classic cube classifier answers, the exact nine
  supported formats, all six faces with distinct format-sized values, half-float MSAA and mip
  values above one, and the four depth facts for both cube and 2D targets. The strengthened native
  cube regression passes **7/7**, including a D16→D24→D24S8→D16 pipeline sequence whose nearer
  geometry wins each time. The ordinary public discriminator fills every half-float face with
  `(4,2,1)`, samples it through `EnvironmentMapEffect` into an RGBA32F target and reads back the
  exact values; the historical RGBA8 substitution would return `(1,1,1)`. It passes identically on
  SDL GPU and EasyGL. The two unchanged EasyGL cube sources compile under SDL GPU and pass 16/16
  constructor/property checks plus the 2/2 depth-versus-no-depth pixel oracle; both also pass
  directly under EasyGL/Xvfb.

  The final focused SDL run passes **9/9 CTests**, including RT2D, cube MSAA+mips, plural binding,
  depthless SpriteBatch compatibility and the 67/67 depth-bias control, with validation diagnostics
  fatal and none emitted. Of 23 selected shared HDR/target/default-contract tests, 22 pass and the
  sole MRT case is expectedly skipped. The classifier remains 246/246 with zero unclassified and promotes both exact EasyGL
  cube sources to direct parity. Registered SDL GPU CTests: **149**. Only targeted incremental
  builds and relinks were used.

### SDLGPU-74 — finish render-target MSAA, mip, preserve/discard and transition semantics ✅

- **Problem/public behavior:** broad Color-path tests exist, but zero MSAA, preserve/discard,
  consecutive passes, read-before-first-present and mixed target transitions need one oracle.
- **Evidence:** EasyGL and shared RT suites cover these independently; SDL has resolve/proxy/pass
  machinery with historical fixes but no consolidated parity run.
- **Location:** RT state/resolve/mip/pass finalization for 2D and cube.
- **Acceptance/test:** zero stays zero, supported nonzero counts report applied samples, preserve/
  discard obey XNA, authored/rendered mips read and sample correctly across repeated mixed passes.
- **Result (2026-09-11):** accepted after the consolidated audit found one real production gap in
  otherwise-correct live lifecycle machinery. The modular renderer already contained the
  post-historical segmented-pass, per-face MSAA, first-use, resolve and generated-mip paths. The
  new SDL registration builds the exact shared `parity_render_target_mip.cpp` source which EasyGL
  already runs. Both legs pass every programmatic assertion; their complete 128×64 RGBA frames are
  byte-identical (`max diff 0`, tolerance 0). The fixture distinguishes level 0 from a generated
  1×1 mip through both typed `GetData` and `BasicEffect` sampling; both renderers produce
  `(137,90,80)` for the coarsest level against the permitted `(138,90,80)` rounded mean.

  The gap was public authored content. `RenderTarget2D` inherits ordinary `Texture2D::SetData`, but
  its SDL renderer inherited empty `UpdatePixels`/`UpdatePixelsLevel` defaults and accepted every
  upload while storing nothing. `RenderTargetCube` likewise inherits `TextureCube::SetData`; SDL
  inherited the common renderer's explicit refusal even though EasyGL has a real RGBA8 cube-face
  upload. FNA's public `Texture2D.SetData<T>` has no render-target exclusion and always reaches
  `FNA3D_SetTextureData2D`, so the 2D no-op is an EasyGL defect too (`EASYGL-PARITY-5`), not
  behavior to copy.

  SDL targets now upload through tightly packed, native-format-sized transfer buffers after
  flushing older deferred work. The 2D state records exactly which levels contain authored or
  rendered bytes so a later partial mip upload seeds untouched texels from the live chain rather
  than transparent zeroes; a render pass marks level zero and every generated descendant defined.
  A first `PreserveContents` bind loads authored level-zero bytes instead of applying the
  uninitialized-resource safety clear. The cube route accumulates RGBA8 regions across faces and
  levels on the same resolved texture and follows the same render-before-upload chronology. Its
  classic Color-shaped API truthfully refuses non-Color target storage rather than reinterpreting
  four-byte `Color` values as half/float/Rgba64 texels. The 2D factory now also retains its
  `preserveContents` argument: a preserving MSAA target uses `RESOLVE_AND_STORE` even for its last
  pass, matching the already-correct cube path, instead of leaving the samples undefined before a
  later `LOAD`.

  Authored single-sample bytes cannot be copied directly into multisample storage. The first
  attempted SDL blit produced Vulkan `VUID-vkCmdBlitImage-dstImage-00234` twice (2D and cube): a
  `vkCmdBlitImage` destination must have one sample. That attempt was rejected, not suppressed.
  The final renderer-side emulation draws the resolved texture over the complete multisample
  attachment with the native sample count and stores the samples before the public partial pass;
  cube faces first copy into a temporary 2D sampleable texture because the stock seed shader has a
  `sampler2D` binding. The final validation-fatal run is clean.

  The new public regression passes **21/21** discriminating checks: byte-exact complete and partial
  level-zero writes, authored child mips, partial authored and renderer-generated mip preservation,
  render→upload and upload→render ordering, two independently retained cube faces, a first real
  cube render pass preserving 60 untouched authored texels, face-mip regeneration, and exact
  2/4/8/16-byte target uploads (`HalfSingle`, `Color`, `Rgba64`, `Vector4`). Its final six checks
  require real 4x 2D/cube targets, seed authored pixels into their multisample attachments, apply
  two partial `PreserveContents` passes separated by a completed readback, and retain exactly
  60 then 56 untouched authored texels. The focused SDL run
  passes **19/19 CTests**: requested-zero and device-supported real MSAA, first readback before
  present, MSAA+depth, MSAA+mips, invalid levels, 2D producer/consumer, backbuffer interleaving,
  cube face preservation and MSAA isolation, depth/stencil preservation, ordered clears, repeated
  pass boundaries and the new upload matrix. The shared public `RenderTargetSemantics` suite also
  passes **6/6**, including the paired PreserveContents/DiscardContents discriminator and a
  4-sample→0-sample→4-sample A→B→A sequence. Every relevant CTest is validation-fatal; none emitted
  `VUID`, `Validation Error` or `Validation Warning`. Registered SDL GPU CTests: **151**; shared
  fixtures: **19/31**.

  The one historical EasyGL source still tagged as needing reuse was not copied: it asks
  `GetBackBufferData` for a bound render target and passes 3/3 only because EasyGL reads the current
  FBO. FNA's OpenGL and SDL_gpu drivers explicitly select the actual backbuffer for that public
  call. This is `EASYGL-PARITY-4`; the renderer-neutral usage pair correctly uses target `GetData`
  and proves SDL behavior. Corpus classification remains 246/246 with zero unclassified, moving
  that file to `easygl-defect`. Only targeted incremental objects and relinks were built.

### SDLGPU-75 — implement independently observable MRT outputs ✅

- **Problem/public behavior:** binding/clearing multiple attachments is not MRT parity if shaders
  cannot write distinct values to each target.
- **Evidence:** EasyGL `easygl_mrt_test` uses distinct fragment locations; SDL stock comments state
  only attachment zero is written although multiple targets bind.
- **Location:** ordinary compiled-effect pipeline/output reflection and MRT pipeline target info.
- **Acceptance/test:** one ordinary Effect pass writes unique signatures to at least two targets,
  both read back correctly, masks/blend states per slot apply, and A→different format/count→A is
  cache-safe. CNAEXT ShaderEffect alone is not sufficient proof.
- **Result (2026-09-10):** the immutable pipeline target tuple now records and hashes the exact
  format of every active attachment instead of cloning slot zero. This applies to every stock,
  ShaderEffect and ordinary compiled-Effect pipeline, so mixed-format MRTs cannot reuse an
  incompatible pipeline. `MultipleRenderTargets` now reports true and the limitations report
  truthfully describes four independently writable mixed-format targets. The public XNA `Effect`
  bytecode fixture writes `Tint` to `oC0` and `Tint.yzxw` to `oC1`; `SdlGpu_MRT` reads distinct
  Color outputs, then repeats through mixed `{Color, HdrBlendable/RGBA16F}` storage with exact
  half-float readback, nontrivial Additive blending, independent Red/Green slot masks and
  A→format/state→A restoration. The existing count-specific shaders separately cover 1→3→1→3,
  all four counts, alternate target objects, depth and MSAA. Targeted incremental build passed;
  the validation-fatal offscreen/Vulkan run passes **55/55** without `VUID`, `Validation Error` or
  `Validation Warning`. The smoke capability branch also passes; that binary remains 29/30 only
  because its pre-existing live-MSAA capability expectation is false on the offscreen device.

### SDLGPU-76 — complete SpriteBatch observable parity ✅

- **Problem/public behavior:** one smoke path does not establish overload, sorting, transform,
  source/origin/rotation/scale/flip/layer/effect/state/RT/order semantics.
- **Evidence:** EasyGL has a broad sprite/font corpus; SDL implements the surface but has a live
  depth-bias failure and incomplete shared-fixture coverage.
- **Location:** SpriteBatch command capture/sort/geometry and renderer sprite pipelines.
- **Acceptance/test:** reuse shared sprite geometry/state/font/sampler sources, add immediate vs
  deferred and interleaved 3D checks, disposed-resource failures and nontrivial rectangles.
- **Result (2026-09-10):** registered the same `sprite_geometry`, `sprite_state` and `sprite_font`
  sources already used by EasyGL. SDL GPU and EasyGL produce the same discriminating results for
  partial/out-of-range source rectangles, nonzero rotation origins, nonuniform and fractional
  placement, every flip composition, Deferred/Immediate/FrontToBack/BackToFront ordering,
  layerDepth, transform matrices, Additive→Opaque restoration, opposite-corner target-local
  coordinates, glyph selection/spacing/newlines/default substitution and atlas orientation. The
  existing exact 89-check source/target/backbuffer orientation matrix and 146-check Point sampler
  matrix remain green. A renderer-neutral disposed-guard fixture is now reused under SDL GPU and
  passes 7/7, including the required disposed-Texture Draw exception and post-failure device use.
  Removing two obsolete “SDL GPU cannot read the backbuffer” gates expanded the existing mixed
  2D/3D ordering suite from 65 to **83/83** checks and the backbuffer pass-order suite to **30/30**;
  this also directly re-proved clear-after-draw and depth-only clear behavior instead of preserving
  stale false declarations. The focused validation-fatal SDL run passes **8/8 CTests**, the three
  EasyGL shared oracles pass under temporary Xvfb/llvmpipe, and no relevant Vulkan validation
  diagnostic appeared. Only targeted objects and executables were rebuilt.

### SDLGPU-77 — exhaustively verify all five classic built-in effects ✅

- **Problem/public behavior:** smoke triangles do not prove all option combinations of Basic,
  AlphaTest, DualTexture, EnvironmentMap and Skinned effects.
- **Evidence:** 44 corpus rows identify weaker SDL evidence; shared effect fixtures isolate light
  terms, alpha comparisons, UV1, fresnel and bones.
- **Location:** stock shader variants/uniform snapshots/program selection.
- **Acceptance/test:** same sources verify texture/color/diffuse/emissive/alpha/fog, default and
  multiple lights, per-pixel/specular, transforms/nonuniform normals, bones/weights limits and
  environment amount/fresnel/specular with discriminating pixels.
- **Result (2026-09-10):** SDL GPU now registers all nine remaining renderer-neutral classic
  stock-effect fixtures plus the exact EasyGL BasicEffect/SkinnedEffect
  `PreferPerPixelLighting` and `WeightsPerVertex` sources. The audit exposed and fixed three real
  differences: DualTextureEffect skipped draws when either public texture property was null instead
  of independently substituting XNA's white fallback; the lit and skinned shaders ignored
  `PreferPerPixelLighting`; and SkinnedEffect rejected a valid semantic declaration carrying
  `BLENDINDICES0` as `Vector4`. The lighting shaders now carry both clamped Gouraud terms and
  fragment-stage inputs and select the public mode from the uniform snapshot. Skinned streams are
  normalized by semantic/index/format/offset, accepting Byte4 or Vector4 bone indices and Color or
  Vector4 vertex color without changing the public declaration. The exact EasyGL per-pixel sources
  distinguish approximately 128 from 155, and the weights oracle proves that the one-, two- and
  four-weight paths ignore disabled influences. All **12/12** new SDL GPU CTests pass with validation
  diagnostics fatal; the identical twelve EasyGL oracles pass under temporary Xvfb/llvmpipe. In the
  broader 21-executable SDL stock-effect run, 20 CTests pass outright and all seven classic legs in
  the remaining mixed test pass; only its two pre-existing CNAEXT PBR half-fog legs remain failing
  and out of this task's scope. No relevant Vulkan validation diagnostic appeared. The corpus now
  has no SDLGPU-77 rows awaiting tests, all **31/31** shared fixtures are registered for SDL GPU,
  and the SDL GPU integration label contains **167** tests.

### SDLGPU-78 — close buffer and draw-family edge cases ✅

- **Problem/public behavior:** all draw overloads must honor primitive count/topology, offsets,
  base/start values, 16/32-bit indices, dynamic options, zero sizes and repeated replacement.
- **Evidence:** EasyGL has dedicated validation/dynamic/range/GetData tests; SDL paths exist but are
  not collectively verified and deferred capture increases risk.
- **Location:** vertex/index backends, public draw parameter packing, queued commands.
- **Acceptance/test:** exact geometry signatures for every draw family/topology and adversarial
  ranges; Discard/NoOverwrite replacement and disposal/lifetime sequences; validation clean.
- **Result (2026-09-10):** thirteen EasyGL buffer/draw corpus sources are now compiled verbatim
  under SDL GPU and pass **13/13** with validation diagnostics fatal. They cover static/dynamic
  vertex and index buffers, 16/32-bit widths, typed/raw/partial uploads and CPU readback,
  `BufferUsage`, disposal and missing-resource guards, invalid public ranges, declaration-driven
  user data, nonzero user offsets, and a twelve-frame None→Discard→NoOverwrite replacement cycle
  with a distinct expected pixel every frame. The audit found the reference corpus's one weak
  spot: `LineList`/`LineStrip` previously only had a “does not throw” test. A new renderer-neutral
  public-API oracle now gives those topologies different pixel signatures across six routes:
  DrawUserPrimitives, both 16/32-bit DrawUserIndexedPrimitives paths, DrawPrimitives and both
  16/32-bit DrawIndexedPrimitives paths. Every route carries two decoy vertices/indices before the
  real range, so losing `vertexOffset`, `vertexStart`, `indexOffset`, `startIndex` or `baseVertex`
  produces red or no geometry. SDL produces 102 LineList / 95 LineStrip pixels and EasyGL 104/96,
  the expected narrow rasterizer-edge difference; both have zero decoy pixels, empty LineList
  centres and nine-pixel LineStrip centre crossings. The shared oracle also proves zero-sized
  static/dynamic vertex and 16/32-bit index resources accept empty uploads without native misuse.
  It passes on both renderers (**22/22 assertions** each). Existing SDL winding, TriangleStrip,
  pass-boundary and chronological-draw tests remain green. The focused SDL sweep is **18/18** for
  in-scope tests; `SdlGpu_Smoke` remains 29/30 solely because its previously documented live-MSAA
  expectation is false on this offscreen device. No relevant Vulkan validation diagnostic
  appeared. SDL GPU now registers **181** integration tests, and all thirteen corresponding corpus
  rows are direct parity evidence.

### SDLGPU-79 — verify Models and ordinary compiled Effect behavior ✅

- **Problem/public behavior:** Model hierarchy/mesh/effect application and compiled techniques,
  passes, parameters, state and texture lifetimes must not regress.
- **Evidence:** EasyGL model/Effect corpus and SDL compiled runtime both exist, but no complete oracle
  comparison has been recorded.
- **Location:** `SdlGpuCompiledEffect`, stock model draw routing and shared content fixtures.
- **Acceptance/test:** identical compiled fixtures and model sources prove technique/pass changes,
  clone independence, 32-bit meshes, child transforms, multi-mesh effects and skinned playback.
- **Result (2026-09-10):** the first complete run exposed real hidden gaps rather than merely
  confirming the existing runtime: **29/33** tests passed, the shared multi-stream and SpriteBatch
  render-target-source contracts failed, and compiled instancing plus cube/volume sampling skipped
  behind explicit refusals. The renderer now resolves a compiled shader's semantic inputs across
  all public vertex bindings, builds dense native stream layouts, captures every used stream at
  draw time, materializes instance frequencies above one, submits the requested instance count and
  includes all buffer/input fields in the immutable pipeline key. Ordinary compiled-effect
  instancing therefore follows the same XNA path as stock effects; explicitly CNAEXT
  `ShaderEffect` instancing remains outside this parity boundary.

  Compiled sampler capture now accepts exact 2D, render-target, cube and volume texture kinds and
  retains their native sampled state through deferred replay. SpriteBatch supplies its public
  source texture when an ordinary Effect sampler does not explicitly bind slot 0, while an
  explicitly assigned effect texture still wins. `Texture3D` uses the same shared sampled-resource
  lifetime model as 2D/cube resources. The new destruction oracle queues a volume draw, destroys
  the public texture before the target flushes, and still samples correctly; its W=1.25 Wrap value
  returns the red first slice rather than the blue Clamp slice, discriminating AddressW as well as
  lifetime. The compiled suite now passes **34/34** with no captured `VUID`, `Validation Error` or
  `Validation Warning`; it covers reflection, technique/pass selection, state publication,
  parameter bounds, clone independence, uniform snapshots, multi-stream/instanced indexed and
  non-indexed draws, SpriteBatch passes/slots, RT sources, cube faces, volume slices, resource
  lifetime, many-draw and truncation behavior.

  Five EasyGL model sources are compiled verbatim under SDL GPU and pass **5/5** with validation
  diagnostics fatal: Model.Draw, a JSON/content model with 32-bit indices, a child mesh's absolute
  parent-bone transform, independent effects on two meshes and skinned animation playback. Their
  EasyGL originals pass **5/5** under Xvfb/llvmpipe; the 32-bit source produces the identical 2/2
  centre/outside pixel result on both. Focused compiled/instancing/MRT/model regression is green,
  SDL GPU now registers **186** integration CTests, and the machine-checked 246-row corpus has no
  new-test-needed entries. The two remaining feature-missing rows are both occlusion queries owned
  by `SDLGPU-80`.

### SDLGPU-80 — prove or close the occlusion-query limitation ✅ (`⛔`)

- **Problem/public behavior:** ordinary `OcclusionQuery` works on EasyGL; SDL correctly reports the
  capability false but inherits a null factory, so public construction succeeds and Begin/End
  silently no-op instead of refusing the unavailable operation.
- **Evidence:** vendored SDL 3.5.0 `SDL_gpu.h` contains fence queries only and no occlusion/query-pool
  primitive. CPU bounds or delayed fake values cannot reproduce samples-after-depth/stencil.
- **Location:** vendored-header evidence, capability reporting and limitation text.
- **Acceptance/test:** re-audit the exact current header/API; investigate a correct render/readback
  emulation and reject it only with measured correctness/cost reasoning. If no reasonable path
  exists, capability false + constructor refusal + exact limitation is `⛔`; never fake results.
- **Result (2026-09-10):** confirmed as an underlying API limitation, not an unfinished CNA draw
  path. The exact in-tree dependency is SDL **3.5.0** (`third_party/SDL` commit
  `cbe3fbe9f367340dcd924de29c225c9f4ffea1f5`). A complete case-insensitive query/occlusion/
  timestamp/fence scan of its 4,500-line public `SDL_gpu.h` finds only command-completion fences:
  `SDL_SubmitGPUCommandBufferAndAcquireFence`, `SDL_WaitForGPUFences`, `SDL_QueryGPUFence` and
  `SDL_ReleaseGPUFence` (lines 4376–4493). There is no occlusion-query handle, query-pool type,
  begin/end command, result resolve or sample-count result. `SDL_GPUDevice`, command buffers and
  render passes are opaque, and the device-properties surface publishes names/versions rather
  than native Vulkan/Metal/D3D12 handles. This is independently corroborated by the exact pinned
  FNA3D `3240147` SDL_gpu driver: its `CreateQuery`, dispose, Begin, End, Complete and PixelCount
  functions at lines 4014–4056 all log “not supported by SDL_GPU”, with creation returning null.

  Renderer-side alternatives were traced against the actual XNA semantics and fail correctness
  before performance becomes the deciding factor. A query counts every sample surviving shader
  discard/clip and rasterizer depth/stencil/MSAA tests even when color writes are disabled,
  blended to the same value or overwritten later, so color/depth readback cannot reconstruct the
  history. An auxiliary additive color target would require rewriting every stock and arbitrary
  compiled pixel shader, consumes an unavailable fifth attachment during an ordinary four-target
  MRT draw, and an MSAA resolve loses the per-sample tally. Atomic instrumentation is unavailable:
  SDL_gpu 3.5 marks graphics-stage storage textures and buffers read-only (header lines 906–912,
  986–991 and 3417–3535); writable storage exists only for compute. CPU replay cannot reproduce
  arbitrary compiled shader sampling, discard, interpolation, depth/stencil and multisample rules.
  Reaching behind SDL_gpu to one native backend is also impossible through its public opaque
  handles and would cease to be an SDL_gpu renderer on Metal/D3D12. No timing benchmark can make
  any of these semantically incorrect candidates reasonable.

  SDL GPU already reported the legacy capability and detailed profile as false, but inherited
  `CreateOcclusionQuery()` returned null, allowing the public constructor to succeed and silently
  turn Begin/End into no-ops with a fabricated zero. The renderer now explicitly throws
  `System::NotSupportedException` with the concrete API reason. The new validation-fatal
  `SdlGpu_OcclusionQuery_Limitation` passes **5/5**: both capability surfaces are false, the public
  constructor has the exact refusal, limitations distinguish fences from rasterizer queries, and
  a subsequent real target clear/readback proves the device remains usable. The updated historical
  smoke reaches and passes both query checks; its total remains the pre-existing **29/30** because
  this offscreen device lacks the smoke's requested nonzero MSAA capability. EasyGL's cycle,
  fully-visible-positive and depth-occluded-zero controls pass **3/3** under Xvfb, proving the
  reference behavior being classified rather than weakening it. SDL GPU now registers **187**
  integration CTests, and no relevant Vulkan validation diagnostic appeared.

### SDLGPU-81 — register and pass the shared EasyGL parity corpus; audit deferred lifetime ✅

- **Problem/public behavior:** EasyGL/WebGPU build the same shared sources; SDL GPU did not obtain
  them from the canonical registry, and queued work must snapshot/retain all referenced
  state/resources at issue time.
- **Evidence:** existing SDL lifetime tests cover targets and some source textures, not every
  texture/cube/volume/buffer/effect/sampler/viewport/RT mutation sequence.
- **Location:** SDL example CMake and deferred command ownership/snapshots.
- **Acceptance/test:** register `cna_register_parity_fixtures` against SDL GPU, all in-scope fixtures
  pass with validation fatal, direct EasyGL-vs-SDL raw frames agree within each fixture's declared
  tolerance, and destruction/mutation after enqueue cannot retroactively change earlier draws.
- **Result (2026-09-10):** SDL GPU now obtains all **31/31** renderer-neutral sources from
  `CNA_PARITY_FIXTURES` in one call. The helper's optional `TARGET_PREFIX` preserves 30 established
  executable names (only the former one-off target-mip name changed), while its multi-valued fatal
  expression makes both `Validation (Error|Warning)` and bare `VUID-` fail every fixture. The
  central CTest slice passes **31/31** on offscreen Vulkan with that generated property verified.

  The direct `scripts/run-parity-corpus.sh cmake-build-debug cmake-build-sdlgpu sdlgpu` run executes
  the same 31 binaries under EasyGL/Xvfb and SDL GPU. Both legs pass every internal assertion; 27
  frames meet the default strict byte/tolerance policy, `compressed_cube` and `sampler_filters`
  deliberately use their stronger renderer-local A/B invariants, `sprite_geometry` differs at 32
  of its 64 permitted half-pixel edge pixels, and `fill_mode_wireframe` differs at 367 of 512
  permitted line-coverage pixels. No broad renderer-wide tolerance or WebGPU exception is inherited.

  That comparison exposed a real EnvironmentMapEffect error which isolated tests had missed. In
  the asymmetric fanned-normal cell EasyGL averaged RGB `(77,124,72)` while SDL GPU averaged
  `(84,96,54)`, producing 738 out-of-policy pixels: SDL recomputed Fresnel per fragment from the
  interpolated normal/eye vector, whereas the XNA/FNA shader computes and saturates it per vertex
  before interpolation through `COLOR1`. Moving that calculation into `env_map3d.vert.glsl` makes
  the complete `env_map_terms` frame byte-identical to EasyGL; the checked-in SPIR-V was regenerated
  from the corrected sources.

  The lifetime audit also found a reproducible pre-fix `SIGSEGV`: a function-local custom
  `ShaderEffect` destroyed after `SpriteBatch::End()` left `SpriteCommand` holding a raw dead
  `SdlGpuEffectRenderer*` until whole-frame replay. Queueing now resolves and captures the exact
  target-compatible pipeline plus uniform bytes at public draw time. Effect destruction/recompile
  defers pipeline release until the queued frame has submitted. The expanded ShaderEffect test
  destroys a local `(0.8,0.4,0.2,1)` effect before replay and reads exact `(204,102,51)`, passing
  **4/4** instead of crashing.

  Existing discriminating tests complete the resource/state inventory: deferred source lifetime
  covers ordinary textures, authored mips, cube/volume samplers and rendered RT2D/cube sources;
  bound-target lifetime covers RT2D, every cube face and MRT; dynamic buffer tests mutate/dispose
  vertex and 16/32-bit index data; viewport/scissor, blend/depth/stencil/rasterizer and sampler tests
  use issue-time A→B→A sequences; stock and compiled effects copy uniform values and retain sampled
  resources; pass-boundary, SpriteBatch/3D and ordered-clear tests retain target selection and
  chronological order. The focused validation-fatal lifetime/state regression slice passes
  **11/11**, including the new ShaderEffect check.

### SDLGPU-82 — final adversarial sweep and exact verdict ✅

- **Problem/public behavior:** known tasks turning green does not prove parity.
- **Evidence:** repeat mechanical searches for inherited defaults, ignored parameters, hard-coded
  RGBA8/state, silent fallbacks, missing keys, throws/nulls/TODOs and unclassified examples.
- **Location:** entire public graphics seam, both modular renderers, tests and this plan.
- **Acceptance/test:** recompute the 246-row manifest with zero unexplained relevant rows; rerun all
  SDL tests, all shared parity/oracle tests and targeted EasyGL controls; record final commits/counts,
  validation status, unavoidable limits and every remaining task. Only then issue verdict A or B.
- **Result (2026-09-10):** the live modular interface, implementations and public XNA seams were
  swept again for inherited defaults, ignored arguments, hard-coded RGBA8/state, incomplete cache
  keys, silent fallbacks, throws/nulls and TODO/FIXME markers. Every hit is accounted for in the
  contract/format matrices: remaining inherited hooks are either semantically correct shared
  behavior, EasyGL-specific, or modern CNAEXT; format fallbacks are guarded by explicit public
  classifiers; named exceptions are validation or the proven query/compiled-vertex-texture
  boundaries; the latter is a shared ordinary-Effect contract limitation rather than an SDL/EasyGL
  difference. Pipeline and sampler identities retain every immutable field, and the A→B→A corpus
  remains green.

  The classifier was strengthened so a new unknown classic filename becomes a fatal
  `unclassified` row rather than being silently called covered. Its exact regeneration reports
  **246/246** sources: 128 covered, 52 direct, 2 query-limit, 52 modern-out, 9 EasyGL-specific,
  3 EasyGL-defect, 0 new-test-needed, 0 duplicate and **0 unclassified**. This sweep identified a
  second EasyGL example of the bound-FBO backbuffer-readback defect (`EASYGL-PARITY-7`) and did not
  copy it.

  Final gates from the stable incremental builds are **188/188** SDL integration CTests,
  **42/42** SDL renderer unit tests, **33/33** registered EasyGL oracle CTests and every one of the
  32 shared sources passing under both renderers. The direct frame policy yields 27 strict frames,
  two renderer-local semantic invariants and three fixed, measured rasterization-edge budgets;
  none was widened during closure. SDL_gpu validation diagnostics are fatal throughout the shared
  and high-risk suites and none was reported. Tasks `SDLGPU-55`–`86` are all closed. The only
  remaining EasyGL capability difference is the rigorously proven SDL_gpu occlusion-query API
  limitation (`SDLGPU-80`, `⛔`), so verdict **A** is warranted.

### SDLGPU-83 — make the requested native viewport authoritative before first acquisition ✅

- **Problem/public behavior:** a first draw at a non-default `NativeBackBuffer` size captured the
  renderer's constructor-time 800×480 viewport even though the public presentation request was
  256×128; lazy swapchain acquisition learned the real size only after the draw had been queued.
- **Evidence:** the shared wireframe fixture rendered its solid triangle in the 800×480-scaled
  upper-left of a 256×128 readback on both offscreen RADV and X11/lavapipe. Readback tracing showed
  `backbuffer=256x128 viewport=800x480` immediately before acquisition; subsequent frames reported
  256×128. EasyGL derives the logical size from the requested virtual dimensions.
- **Location:** `SdlGpuRenderer::SetVirtualResolution`, first-frame smoke regression.
- **Acceptance/test:** the renderer and public `GraphicsDevice.Viewport` both report the exact
  requested 320×240 size on frame one, before any present/readback/acquisition; ordinary rendering
  regressions remain green and native validation output is fatal.
- **Result (2026-09-09):** `SetVirtualResolution` now treats a positive native request as the
  pending swapchain extent immediately; the first real acquisition remains authoritative and
  replaces it if the platform clamps the size. The expanded smoke test passes 30/30 on Vulkan,
  including both first-frame exact-size checks, with validation diagnostics configured as fatal.

### SDLGPU-84 — keep render-target depth oracles inside the XNA clip volume ✅

- **Problem/public behavior:** the SDL-only 2D and multisampled 2D render-target tests call an
  identity-projection vertex at Z=−0.5 “near.” After `SDLGPU-65` correctly enabled the XNA/D3D
  0..W depth clip, that quad is clipped and both tests misleadingly report a depth failure.
- **Evidence:** the equivalent cube fixture had the same issue and passes at valid 0.25/0.75
  depths; FNA3D's SDL_gpu driver explicitly enables depth clipping.
- **Location:** SDL GPU RenderTarget2D and RenderTarget2DMSAA test geometry only.
- **Acceptance/test:** use valid, separated 0.25/0.75 clip depths; nearer green wins under both
  single-sample and MSAA targets, all other assertions remain green, validation clean.
- **Result (2026-09-09):** both fixtures now use near Z=0.25 and far Z=0.75, entirely within the
  XNA/Direct3D clip volume while retaining a large discriminating separation. The ordinary target
  passes 7/7 and the 4x-MSAA resolve target passes 9/9; both CTests now make Vulkan validation
  diagnostics fatal.

### SDLGPU-85 — implement ordinary backbuffer MSAA and truthful applied-count reporting ✅

- **Problem/public behavior:** the ordinary `GraphicsDevice` construction path forwards
  `PresentationParameters.MultiSampleCount` in `GraphicsRendererCreateArgs`, but the SDL GPU
  factory drops that argument. The swapchain pass, its depth attachment and every selected
  pipeline are consequently forced to one sample while the inherited applied-count mapper echoes
  the unfulfilled request.
- **EasyGL evidence:** `EasyGLRenderer` consumes the construction argument, clamps it to
  `GL_MAX_SAMPLES`, renders into multisampled colour/depth renderbuffers, resolves before present
  and reports the applied count. Its inability to change the count after construction is a
  separate reference defect rather than justification for discarding the initial request.
- **SDL GPU evidence:** the factory's five-argument construction omits `args.multiSampleCount`;
  `EnsureDepthStencilTexture` and `RenderToSwapchain` hard-code `SDL_GPU_SAMPLECOUNT_1`; the main
  renderer inherits `GetMultiSampleCount()==0` and identity `GetAppliedMultiSampleCountEXT`.
- **Likely location:** `SdlGpuRenderer` construction/factory, backbuffer colour/depth resource
  lifecycle, swapchain pass resolve metadata, pixel-centre selection and the shared parity corpus.
- **Acceptance/test:** clamp against the actual swapchain and depth formats; allocate matching
  multisample colour/depth attachments; resolve each backbuffer segment to either the acquired
  swapchain or the readback proxy while preserving A→B→A segment contents; use the applied sample
  count in every pipeline and public report; support `GraphicsDevice::Reset` without stale
  attachments; and prove through ordinary public calls that the applied count is honest and
  sloped opaque geometry contains resolved partial-coverage pixels. The same source must exercise
  EasyGL and SDL GPU, with validation diagnostics fatal.
- **Result (2026-09-10):** the SDL GPU factory now forwards the public construction request and
  clamps it against the actual swapchain colour format and selected combined depth/stencil format.
  A lazily sized multisample colour texture and same-count depth texture back every backbuffer
  pass; `SDL_GPU_STOREOP_RESOLVE_AND_STORE` resolves to either the acquired swapchain or the
  existing readable proxy while retaining samples for a later A→RT→A backbuffer segment. Pipeline
  selection receives the real count, and the XNA half-pixel correction is disabled for the
  multisampled destination exactly as it already was for multisampled render targets.

  `GetMultiSampleCount` and the construction-time applied mapper now expose the actual clamped
  value. `ApplyMultiSampleCount` flushes pending work transactionally, releases only the stale
  backbuffer colour/depth attachments, and recreates them lazily; sample count already participates
  in every immutable pipeline key, so no unrelated pipeline rebuild is needed. The capability
  query now uses the same clamping routine instead of falsely requiring exactly 2x support; this
  device supports the exercised 4x colour/depth pair and the smoke profile consequently improves
  from its recorded 29/30 to **30/30**.

  The new renderer-neutral `parity_backbuffer_msaa.cpp` uses only ordinary public XNA calls and
  passes on both EasyGL/llvmpipe and SDL GPU/RADV: request 4 reports 4, the identical opaque
  diagonal contains one genuinely intermediate resolved pixel, an interior pixel is opaque white,
  and an untouched pixel retains the first backbuffer segment's black clear across an intervening
  target pass. The SDL-only second invocation runs 4x→0x→4x through `GraphicsDevice::Reset` and
  passes all **8/8** checks, including hard versus blended edge signatures. Both SDL CTests make
  validation diagnostics fatal and emit none. The full direct corpus now passes **32/32** on both
  renderers: the MSAA frame differs at 506 pixels, all confined to the three implementation-defined
  coverage boundaries and within the fixed 520-pixel policy; its programmatic coverage/interior
  invariants are the semantic oracle. The complete SDL GPU compiled-effect/sampler unit slice also
  passes **42/42** after the constructor-contract change.

### SDLGPU-86 — refresh parity-obsoleted SDL regression oracles ✅

- **Problem/public behavior:** the first freshly rebuilt 188-test sweep after the parity changes
  passes 180 tests but leaves eight deterministic failures. Source and output triage identifies
  stale test assumptions rather than eight renderer regressions: two PBR scenes put Z=-0.5 outside
  the XNA 0..W clip volume; one pass-boundary depth fixture does the same; DualTexture supplies no
  `TEXCOORD1` while attempting to test slot 1; two contract fixtures still call the now-exact
  RenderTargetCube SetData path unsupported; and the render-target/effect-source fixture expects
  semantically incomplete PositionTexture declarations to be accepted by EnvironmentMap and
  skinned families. The remaining PBR fog oracle compares an arithmetic midpoint of stored sRGB
  bytes even though the shader correctly performs fog in linear space before encoding.
- **EasyGL/XNA evidence:** FNA/XNA depth clipping is 0..W; effect vertex declarations are matched
  by semantic/index rather than stride; `DualTextureEffect` owns independent texture-coordinate
  semantics; and the shared cube/volume contract now proves exact RenderTargetCube uploads. The
  PBR-only color-space controls are CNAEXT diagnostics and must state whether their byte oracle is
  linear or encoded.
- **Location:** the eight failing SDL GPU regression fixtures only; no renderer implementation or
  public API change.
- **Acceptance/test:** preserve or strengthen every discriminator while correcting its inputs and
  capability table; rebuild only the eight affected executables; make those eight CTests pass
  serially with validation diagnostics fatal; then require the complete 188-test SDL GPU sweep to
  pass before `SDLGPU-82` may close.
- **Result (2026-09-10):** all eight affected executables were rebuilt incrementally and the exact
  eight-test serial slice now passes **8/8**. The corrected PBR projections preserve the intended
  world-space BRDF inputs while moving only clip Z; the pass-boundary test uses separated valid
  depths 0.25/0.75; the DualTexture sampler check declares and feeds both independent UV channels;
  both cube contracts now exercise exact uploads; incomplete effect declarations are truthfully
  rejected; and the PBR fog diagnostic explicitly requests linear storage before comparing a
  linear arithmetic mix. No renderer source or public API changed. The complete 188-test sweep
  remains the closing gate owned by `SDLGPU-82`.

### SDLGPU-87 — make default-framebuffer depth/stencil capability answers truthful ✅

- **Problem/public behavior:** `SdlGpuRenderer::QueryDepthStencilFormat` explicitly allows
  construction to continue when the device exposes neither supported combined depth/stencil
  format, and the legacy `SupportsCapability` switch correctly reports that condition. The three
  common contract hooks used by ordinary `GraphicsDevice::Clear`, however, were inherited as
  unconditional `true`; a device without the format would therefore advertise real depth and
  stencil planes to the public clear path while silently rendering without either attachment.
- **EasyGL/SDL evidence:** EasyGL's default is valid for its mandatory GL framebuffer path. SDL
  GPU's own `depthStencilFormat_` is the authoritative result of runtime
  `SDL_GPUTextureSupportsFormat` probes, and already controls attachment creation, pipeline target
  state and legacy capability reporting.
- **Location:** `SdlGpuRenderer` capability overrides and its constructor exception/capability
  regression fixture.
- **Acceptance/test:** override all three answers from the queried native format; force the
  no-format path without replacing SDL or mocking resource ownership; prove the aggregate, depth
  and stencil contract answers and both legacy capability fields are false, then issue a combined
  clear/present successfully with validation diagnostics fatal.
- **Result (2026-09-10):** `SupportsDepthStencil`, `SupportsDepthBuffer` and
  `SupportsStencilBuffer` now all derive from the same `depthStencilFormat_ != INVALID` fact as
  native attachment/pipeline creation. A scoped test-only constructor hook forces the otherwise
  hardware-dependent unavailable branch after the real device query. The expanded transactional
  constructor fixture proves all five public/internal capability views agree, color presentation
  remains usable without an attachment, and every native resource remains balanced. Its stable
  incremental target rebuild succeeds and the validation-fatal CTest passes **295/295** checks.

### SDLGPU-88 — make the renderer-contract audit exact and fail closed ✅

- **Problem/public behavior:** the prose contract matrix grouped related hooks and stated that
  overloads/defaults had been inspected, but it was neither an exact inventory nor mechanically
  tied to the current modular declarations. A newly inherited default could therefore appear after
  the parity verdict without making any audit gate fail.
- **EasyGL/SDL evidence:** the live `IGraphicsRenderer.hpp` contains fifteen related renderer
  interfaces, overloaded hooks, optional renderer-specific helpers and modern extensions. EasyGL
  and SDL GPU each split their concrete implementations across multiple classes, so name-only grep
  counts cannot reliably distinguish an override from a default.
- **Location:** `tools/check_sdlgpu_renderer_contract_audit.py`, generated
  `plans/sdlgpu_renderer_contract_audit.csv`, and this plan's mechanical-audit section.
- **Acceptance/test:** parse the real interface and both concrete renderer translation units with
  their stable compile commands; emit exactly one classified row per virtual hook with signature,
  line, base default, scope, both implementations, state and evidence; reject drift, duplicate
  identities, stale allowances, unreviewed classic inherited defaults and any unresolved state.
  Re-run the independent 246-source EasyGL corpus classifier too.
- **Result (2026-09-10):** Clang AST enumeration finds **226/226** hooks across all fifteen
  interfaces. The manifest classifies 146 classic-observable rows as 141 equivalent and five
  unavoidable occlusion-query hooks, plus 80 renderer-specific/CNAEXT/modern rows as `out`; there
  are **zero** unresolved rows. Exact-signature allowances document the 20 concrete SDL inherited
  instances, and any changed/removed allowance is fatal. The checker reruns cleanly from both
  stable compile databases without a rebuild; the independent example classifier still reports
  **246/246**, zero unclassified and zero unowned remediation categories.

### SDLGPU-89 — accept the full XNA sixteen-stream binding width ✅

- **Problem/public behavior:** XNA 4.0 permits sixteen `VertexBufferBinding` slots and EasyGL
  exposes that full ceiling, but SDL GPU reported eight and rejected an ordinary draw with more
  than eight live, non-duplicate per-vertex streams before semantic resolution. A stock shader may
  consume fewer than eight inputs while a required semantic legally resides in any public slot.
- **EasyGL/SDL evidence:** EasyGL inherits `GetMaxVertexStreams()==16` and searches all captured
  streams by semantic. Vendored SDL 3.5 defines `MAX_VERTEX_BUFFERS` and
  `MAX_VERTEX_ATTRIBUTES` as 16 and validates that exact ceiling in `SDL_gpu.c`; SDL GPU's eight
  came only from conflating the maximum stock-shader input count with the offered-stream count.
- **Location:** shared stock semantic resolver capacity, SDL GPU capability/limitation reporting,
  common ordinary multi-stream oracle and renderer smoke.
- **Acceptance/test:** report sixteen; retain all sixteen public streams until semantic resolution;
  bind only the streams the shader consumes; pixel-prove a required `COLOR0` in slot 15 behind
  fourteen live unique decoy streams under both EasyGL and SDL GPU; keep validation clean.
- **Result (2026-09-10):** the offered-stream capacity is now the XNA/SDL_gpu ceiling of sixteen,
  independent of the eight inputs the largest stock shader can consume. The resolver searches all
  offered bindings and still densifies only consumed streams, so the new adversarial draw creates
  two native bindings rather than sixteen. `GetMaxVertexStreams()` and generated limitation text
  now tell the same truth. The renderer-neutral pixel oracle puts `POSITION0` in slot 0 and
  `COLOR0` in slot 15, with fourteen uniquely declared `TEXCOORD1..14` buffers between them; the
  old limit deterministically refused it, while a truncating implementation would render white.
  The targeted SDL GPU multistream suite passes **21/21**, including the new slot-15 pixel case,
  and the validation-fatal renderer smoke passes **1/1** with its updated sixteen-slot capability
  assertion. The same new pixel oracle passes in an isolated EasyGL/Xvfb process. A first
  monolithic EasyGL 21-test attempt exited 139 while entering the second pre-existing case, before
  the new case started; an exact two-case reproduction then passed 2/2, so the transient failure is
  retained as evidence but not misattributed to this change. No Vulkan validation diagnostic
  appeared. Only the affected stable-build
  targets were rebuilt, with `-j2`. A WebGPU compile check exposed the stale non-production-SDL
  ratchet count left by `SDLGPU-87`'s one new constructor-test hook; its exact file budget is now
  38 instead of 37, restoring the configuration audit without broadening any production allowance.
  The reconfigured `cna_renderer_webgpu` target then builds successfully, proving the shared
  resolver-capacity change preserves the other stream-aware renderer. Committed as
  `9285b36e39106aecd85402c6f14688a4fa2f0fcb`.

### SDLGPU-90 — reconcile the final adversarial boundary audit ✅

- **Problem/public behavior:** the post-implementation sweep found no additional SDL-only feature
  gap, but it did find three authoritative-record defects and one stale public diagnostic: the
  summary called only occlusion queries an EasyGL difference even though EasyGL can forward an
  exact swap interval of two; the MRT row did not say that both renderers refuse cube faces in a
  multi-target set; the compiled-effect row did not record that both renderers refuse vertex-stage
  texture sampling; and an internal guard still said "more than eight vertex streams" after
  `SDLGPU-89` raised the real ceiling to sixteen.
- **EasyGL/SDL evidence:** `EasyGLRenderer::SetSwapInterval` forwards the caller's integer directly,
  while SDL_gpu exposes only VSYNC/IMMEDIATE/MAILBOX present modes. EasyGL and SDL GPU each throw
  at `SetRenderTargets` when any member of a multi-target set is a cube face, and each throws when
  an ordinary compiled effect's vertex shader has a sampler. The public
  `GraphicsDevice::SetVertexBuffers` seam and vendored SDL_gpu both cap bindings at sixteen.
- **Location:** final status/matrix/task ledger in this plan and the SDL GPU refusal diagnostics in
  `SdlGpuRenderer.cpp`; no graphics behavior or public API change.
- **Acceptance/test:** make every remaining difference statement internally consistent; state the
  two shared renderer boundaries without calling them SDL parity gaps; make the stream diagnostic
  agree with the implemented public/native ceiling; rerun both fail-closed audit checkers, the
  existing sixteen-stream public/pixel checks and validation-fatal smoke from stable incremental
  builds; finish with zero unclassified examples, zero unresolved contract hooks and a clean diff.
- **Result (2026-09-10):** the final status now names exact interval Two and occlusion queries as
  the two unavoidable EasyGL capability differences and distinguishes the shared unimplemented
  multisample mask. The matrix explicitly limits the equivalent MRT claim to ordinary
  `RenderTarget2D` attachments and records cube-face MRT plus compiled-effect vertex-stage
  sampling as identical EasyGL/SDL refusal boundaries. The only stale stream diagnostic now says
  sixteen, matching the public and native limits; the compiled-effect diagnostic no longer calls
  its deliberate shared boundary a provisional route-specific omission.

  The stable SDL build performed five incremental steps: one renderer translation unit, its static
  archive and the two affected executable links. The complete ordinary multistream slice passes
  **21/21** under `SDL_VIDEODRIVER=offscreen`, including the slot-15 pixel oracle and the public
  17-binding refusal; validation-fatal `SdlGpu_Smoke` passes **1/1** with no diagnostic. No runtime
  test used the host display. The exact contract checker remains **226/226** (141 `=`, 80 `out`,
  five `⛔` occlusion hooks), and the example classifier remains **246/246** with zero unclassified
  and zero unowned classic remediation categories; its only two feature-missing rows are the two
  `SDLGPU-80` occlusion-query examples. The adversarial source scan finds no SDL GPU `TODO`/`FIXME`,
  no surviving eight-stream diagnostic and no ignored texture/cube/volume/render-target format
  argument. `git diff --check` is clean.

---

## Historical SDL GPU implementation plan (preserved evidence; paths/status may be stale)

# SDL GPU Graphics Backend — Implementation Plan

> **Correction (2026-07-15, later the same day): the banner below's "every phase ... is fully
> implemented" claim was wrong when written** — `SDLGPU-17`–`20` (the genuine keyed pipeline cache,
> plus dynamic `BlendState`/`DepthStencilState`/`RasterizerState`) were still `⬜`/🟨 at that point;
> every 3D/sprite pipeline was still hardcoded Opaque/no-cull/Solid/no-stencil regardless of what
> `GraphicsDevice.BlendState`/`DepthStencilState`/`RasterizerState` were assigned. Found on a
> later re-check the same day and closed for real — see execution-order item 10 and each row's own
> Notes column. A genuine bug (`SDL_SetGPUStencilReference()` never called anywhere, so
> `ReferenceStencil` was silently ignored) was found and fixed along the way.
>
> **Status (2026-07-15): every phase currently in this plan (SDLGPU-1 through SDLGPU-10) is fully
> implemented and verified** — `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
> `SkinnedEffect` are all real and verified (`EnvironmentMapEffect` landed once Phase `SDLGPU-9`'s
> cube-texture backends existed; `SkinnedEffect` landed after a real empirical spike found and
> worked around a genuine `SDL_gpu` push-uniform size cap — see that row for the full finding), and
> custom `ShaderEffect` support (Phase `SDLGPU-10`) landed last, via a real runtime GLSL→SPIR-V
> compile (`libshaderc` linked into the backend binary itself, not just used at build time) — see
> `SDLGPU-42`'s row for a genuine architectural finding this decision uncovered (Vulkan's own
> `ShaderEffect` support doesn't actually runtime-compile GLSL text at all, despite the class's own
> documented contract).
> `CNA_GRAPHICS_BACKEND=SDL_GPU` configures, builds (`cna_backend_graphics_sdl_gpu`, zero new
> third-party dependencies as predicted), and a real window + real `SDL_GPUDevice` (Vulkan driver)
> clears color+depth+stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint,
> alpha, rotation, both flips, Point/Linear + Wrap/Clamp sampling), draws real
> `colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d` 3D geometry (with a real
> depth-test occlusion proof), and draws real `AlphaTestEffect`/`DualTextureEffect` geometry (real
> per-pixel discard; a real two-sampler multiply with a colour-shift result that could only come
> from two textures actually being sampled) — all via the public `BasicEffect`-family/
> `VertexBuffer`/`GraphicsDevice.DrawPrimitives` API. Verified by `SdlGpu_Smoke` (6/6), `SdlGpu_2D`
> (3/3), `SdlGpu_3D` (6/6), and `SdlGpu_Effects` (3/3 checks, `ctest -R SdlGpu`), all on real GPU
> via Vulkan on this Linux dev machine, each backed by a real screenshot, not just "didn't throw".
> Those screenshots caught and led to fixing a real bug — see `SDLGPU-14`'s row below — and then,
> for the 3D path, *confirmed the fix's own theory* (3D shaders using a real XNA projection matrix
> need no Y-flip, unlike the hand-computed 2D sprite NDC math). `GraphicsBackendCompileDefinitionTests.cpp`'s
> `ExactlyOneGraphicsBackendIsSelected` was updated for the new backend and the full `CnaTests`
> suite (118 tests) still passes. `EnvironmentMapEffect`/`SkinnedEffect` are still ⬜. **Phase
> `SDLGPU-8` (render targets) is now fully done**: `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
> (`RenderTargetCube`, real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38` (`RenderTarget2D`
> MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` legs are all done and verified,
> 2026-07-15 — see their own rows for the real multi-pass `EnsureFrameRendered` refactor and
> `DrawTarget` generalization this required. `SDLGPU-39`'s remaining swapchain leg stays 🟨 — hit a
> real, unresolved segfault (see that row); plain `Texture2D::GetData()` is separately out of scope
> (already-accurate CPU shadow, no GPU path needed). Plain `TextureCube::GetData()`'s leg is now
> **also done for real** — `SDLGPU-51` (below) landed a full `SdlGpuTextureCubeBackend` with a real
> transfer-buffer+fence `GetData()`, not a stub, so `SDLGPU-39` is now only blocked on the swapchain
> segfault.
>
> **Real cross-backend interface change made 2026-07-15 to close `SDLGPU-39`'s `RenderTarget2D`
> leg:** `ITextureBackend` (in the common `IGraphicsBackend.hpp` every backend implements) gained a
> new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default
> (mirrors `ITextureCubeBackend::GetData`'s own convention) — purely additive, no other backend's
> code needed to change. `Texture2D::GetData()`'s two entry points now fall back to this only when
> the CPU-side shadow is empty (i.e. only for a `RenderTarget2D`); plain, `SetData()`-populated
> textures are completely unaffected (confirmed via 123/123 passing `Texture2DTest`/
> `TextureCubeTest`/`Texture3DTest`). See `SDLGPU-39`'s own row for the full detail.
>
> **Phase `SDLGPU-9` (`Texture3D` + plain `TextureCube`) fully closed 2026-07-15**: `SDLGPU-40`
> (`Texture3D`) and `SDLGPU-41` (mips) are done and verified — see `SDLGPU-40`'s own row for a real
> bug this task's own byte-exact round-trip test caught and fixed (`SDL_UploadToGPUTexture`'s
> `cycle=true` silently orphaned earlier partial sub-volume writes onto an abandoned GPU resource;
> fixed with `cycle=false` for `Texture3D` specifically). `SDLGPU-51` (plain `TextureCube`) is also
> done and verified — `SdlGpuTextureCubeBackend` reused the exact same per-face `region.layer`
> indexing `SdlGpuRenderTargetCubeBackend::GetData()` already established, and its `SetData` uses
> `cycle=false` from the start (the `SDLGPU-40` bug was checked for and confirmed absent via a
> deliberate "write all 6 faces, then re-read face 0 last" regression check). With both cube-texture
> sources now real, `SDLGPU-33` (`EnvironmentMapEffect`) was implemented and verified the same day —
> see its own row for the new `env_map3d.glsl` shader pair and the dual-backend cube resolve this
> required. Only `SDLGPU-34` (`SkinnedEffect`'s push-uniform-size spike) remains deferred from
> Phase 7.
>
> **Real architectural gotcha found while writing `SDLGPU-37`'s test — FIXED 2026-07-15 (see
> execution-order item 12 below), no longer applies:** a `RenderTarget2D`/`RenderTargetCube`
> destroyed before this backend's deferred `Present()`-time render pass actually executes used to
> be a genuine use-after-free. `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState` (new,
> shared_ptr-owned structs holding the actual GPU state, independent of the
> `SdlGpuRenderTargetBackend`/`SdlGpuRenderTargetCubeBackend` wrapper's own C++ lifetime) now keep a
> render target's state alive exactly as long as anything still references it — a short-lived local
> render target destroyed mid-`Draw()` now renders its own pending content correctly AND is safe to
> sample from elsewhere the same frame. Confirmed via a real segfault when `sdlgpu_mrt_test.cpp`'s
> first draft used `Draw()`-local `RenderTarget2D` instances (now fixed, still worth keeping
> targets as member fields by convention, but no longer a crash risk either way).
>
> **Full `CnaTests` suite is NOT currently stable end-to-end under `CNA_GRAPHICS_BACKEND=SDL_GPU`
> in this sandboxed dev environment** (found 2026-07-15 while trying to verify `SDLGPU-35`
> broadly): running the entire ~4300-test suite segfaults partway through
> `ContentManagerSkinnedModelTest` (confirmed reproducible on the unmodified pre-`SDLGPU-35`
> baseline too, and the exact test it crashes on shifts when the first one is filtered out —
> consistent with real-window/GPU-device resource churn across thousands of tests in this sandbox,
> not a deterministic logic bug in one test). This is a pre-existing condition, not something
> `SDLGPU-35` introduced or fixed. This backend's own dedicated CTest suite
> (`SdlGpu_Smoke`/`SdlGpu_2D`/`SdlGpu_3D`/`SdlGpu_Effects`/`SdlGpu_RenderTarget2D`/
> `SdlGpu_RenderTargetCube`/`SdlGpu_MRT`/`SdlGpu_RenderTarget2DMSAA`/`SdlGpu_Texture3D`/
> `SdlGpu_TextureCube`/`SdlGpu_EnvMap`/`SdlGpu_Skinned`/`SdlGpu_ShaderEffect`, `ctest -R SdlGpu`) is the actual validated methodology
> for real-window backends in this project (mirrors
> how Vulkan/D3D11/D3D12 are validated) — don't treat a full unfiltered `CnaTests` run under this
> backend as a meaningful signal without first re-reading this note.
>
> **Known partial gaps in what's landed so far:** `SDL_CreateGPUDevice`'s `debug_mode` is
> hardcoded `false`, not yet wired to a CNA-side debug/validation toggle (minor, deferred).
> "Recoverable handling of a failed/occluded swapchain acquisition" (SDLGPU-11) only covers the
> one case `SDL_gpu` itself documents (a null texture on a minimized window) — there is no
> WebGPU-style surface-loss/outdated-recovery case to handle because `SDL_gpu` does not expose
> one at this API level.
>
> **Real, cross-backend bug found while verifying this phase — fixed elsewhere, noted here for
> history:** `GraphicsDeviceManager::SynchronizeWithVerticalRetrace` +
> `ApplyChanges()`/`applyToExistingBackend()` used to never reach
> `IGraphicsBackend::SetSwapInterval()` for *any* backend (`GraphicsDevice::Reset()` never forwarded
> the presentation interval to the backend). This has since been fixed in `GraphicsDevice::Reset()`
> (merged into `develop` from the D3D9 branch). Fixing it surfaced a real consequence for every
> SDL_GPU test/example in this file: each one's constructor called
> `backend.SetSwapInterval(0)` directly to avoid this environment's ~1s/frame VSync wait on its
> virtual/headless display, but that call ran *before* `Game::DoInitialize()`'s `CreateDevice()` —
> which, now that forwarding works, applies `GraphicsDeviceManager.SynchronizeWithVerticalRetrace`'s
> default (`true`) afterward and silently overwrote the workaround, making every 120-frame test take
> ~2 minutes and time out under CTest's 60s default. Fixed by replacing the direct-backend-poke
> workaround in all 19 SDL_GPU example/test constructors with
> `gdm_->setSynchronizeWithVerticalRetraceProperty(false);`, called before `CreateDevice()` ever
> runs — the correct, property-level way to request Immediate presentation, and no longer a
> workaround now that the forwarding gap itself is closed.
>
> **Real, pre-existing SDL_GPU bugs found while merging into `develop` (2026-07-16), fixed as part
> of that merge:** `SdlGpuVertexBufferBackend::SetDataWithOptions` and
> `SdlGpuIndexBufferBackend::Upload` created the backing `SDL_GPUBuffer` with `size` equal to the
> caller's raw vertex/index byte count, with no floor — a degenerate `vertexCount`/`indexCount` of
> `0` (e.g. an empty part of a skinned model) asked `SDL_CreateGPUBuffer` for a 0-byte buffer, which
> SDL_gpu's Vulkan driver asserts on and crashes (reproduced identically on the pristine,
> pre-merge `feature/sdlgpu` branch, so this was not introduced by the merge — it just had never
> been exercised by a real content-loading test under this backend before). This crashed
> `ContentManagerSkinnedModelTest.PartNameWithUnbalancedEmbeddedBraceParsesCorrectly` and two
> `TextureLoads*` cases with a SEGFAULT. Fixed by clamping the backing allocation to a 4-byte floor
> and skipping the transfer-buffer upload entirely when the logical size is 0 (the public
> `vertexCount_`/`indexCount_`/`shadowData_` still report the real, possibly-zero size).
>
> **Known gap surfaced by the same merge, left open:** `CnjEffectTest.LoadsRealCnjFixture` (a
> `develop`-only test — it doesn't exist on `feature/sdlgpu`, so this is the first time it has run
> under `CNA_GRAPHICS_BACKEND=SDL_GPU`) fails because its fixture's custom `ShaderEffect` GLSL is
> not written in the SPIR-V-compatible dialect SDL_GPU's shader compiler requires (needs an
> explicit `#version 310 es`-or-higher, explicit `location` qualifiers on stage I/O, and uniforms in
> a block rather than loose/non-opaque). This is a real, separately-scoped gap in SDL_GPU's custom
> `ShaderEffect` support (either the fixture's GLSL or SDL_GPU's shader-compat layer needs work),
> not something fixable as a small merge-time patch — left open and tracked here rather than fixed
> under this merge.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Why an SDL GPU backend

- **Zero new third-party dependency.** SDL3 is already a vendored/git-cloned dependency of this
  project (`third_party/SDL`), and `SDL_gpu.c` is already compiled into every prebuilt SDL3
  package this repo produces (`.sdl-prebuilt*/`). Unlike WebGPU (`wgpu-native`, a separate
  library) or Vulkan (the Vulkan SDK/loader), adding this backend requires no new external
  package — only new CNA-side code.
- **One API, several native backends.** `SDL_gpu` itself dispatches to Vulkan, D3D12, or Metal
  depending on platform/availability. A working SDL GPU backend gets CNA a second real path to
  Direct3D 12 and Metal without maintaining separate `D3D12`-style or `Metal`-style CNA backend
  code for them — at the cost of losing direct control over backend-specific behavior that the
  dedicated `D3D11`/`D3D12`/`Vulkan` backends have.
- **Consistent with the existing multi-backend architecture.** CNA already supports 9
  `CNA_GRAPHICS_BACKEND` values (`SDL_RENDERER`, `EASYGL`, `BGFX`, `VULKAN`, `WEBGPU`, `HEADLESS`,
  `SOFTWARE`, `D3D11`, `D3D12`) selected at CMake configure time; this is simply the tenth,
  following the exact same `IGraphicsBackend` contract every other backend already implements
  (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend. See
`Internal (CNA) vs XNA Layer` in `CLAUDE.md` for the backend-selection architecture this plan
extends.

---

## Non-goals and known constraints (read before starting)

- **Not a replacement for any existing backend.** SDL GPU is an *additional* pluggable backend.
- **No occlusion-query support in `SDL_gpu` itself.** As of the vendored `third_party/SDL/include/SDL3/SDL_gpu.h`,
  there is no query-pool/occlusion-query type anywhere in the API (only `SDL_GPUFence` for
  CPU/GPU synchronization). `IOcclusionQueryBackend::CreateOcclusionQuery()` must return
  `nullptr` on this backend, exactly like the `Headless`/`Software` backends already do for
  their own reasons. This is a **permanent limitation of the underlying API**, not a task gap —
  do not open a task to "fix" it unless a future SDL3 version adds real query support.
- **Shaders must be precompiled bytecode with a mandatory resource-binding order.**
  `SDL_gpu` does not accept GLSL/HLSL source at runtime. `SDL_CreateGPUShader` takes
  `SDL_GPUShaderFormat`-tagged bytecode (SPIR-V for the Vulkan driver, DXBC/DXIL for the D3D12
  driver, MSL/metallib for the Metal driver), and — per `SDL_gpu.h` (~L2606–2744) — every
  shader stage's samplers/storage-textures/storage-buffers/uniform-buffers must be declared in a
  **fixed HLSL-style order** (`t[n]`/`s[n]` space0, `u[n]` space1, `b[n]` space2 for the vertex
  stage; an analogous space2/space3 split for the fragment stage). This is a different
  convention from the raw `layout(set=, binding=)` numbers the existing `VulkanGraphicsBackend`
  GLSL shaders use (`src/CNA/Internal/Backends/Vulkan/shaders/*.glsl`) — those shaders are a
  useful **algorithmic** reference (the lighting math, the alpha-test logic, the dual-texture
  blend) but their compiled SPIR-V is **not** directly reusable as-is; the binding layout has to
  be re-authored to satisfy `SDL_gpu`'s convention. See Phase `SDLGPU-3` for the decision this
  implies (hand-author SPIR-V for the Vulkan driver first vs. vendoring the official
  `SDL_shadercross` cross-compiler for all formats).
- **Verified target for the first milestone: Linux desktop via `SDL_gpu`'s Vulkan driver.**
  This matches the current dev environment (the only `SDL_gpu` driver compiled into this
  project's prebuilt SDL3 packages on Linux is `vulkan`, per
  `.sdl-prebuilt-Linux-x86_64/SDL/build/.../src/gpu/`). Windows (D3D12 driver) and macOS/iOS
  (Metal driver) remain **code paths only, not validation claims**, until run on that real
  hardware — the same phased-claim discipline already used for `D3D11`/`D3D12` (Wine/DXVK/
  vkd3d-proton verified, real-Windows-hardware tasks marked `needs_human`) and for WebGPU
  (Linux-desktop-verified, Windows/macOS unverified).
- **Pipelines are immutable objects**, exactly like Vulkan/D3D12/WebGPU in this codebase — a
  pipeline cache keyed by (shader pair, vertex format, blend/depth-stencil/rasterizer state,
  sample count, target formats) is required from the start, not an afterthought.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `SDL_GPU` |
| CMake option | `CNA_BACKEND_SDL_GPU` |
| Compile definition | `CNA_BACKEND_SDL_GPU` |
| Backend directory | `src/CNA/Internal/Backends/SdlGpu/`, `include/CNA/Internal/Backends/SdlGpu/` |
| CMake target | `cna_backend_graphics_sdl_gpu` |
| Main class | `CNA::Internal::Backends::SdlGpuGraphicsBackend` (deliberately distinct from the existing `SdlGraphicsBackend` in `SdlRenderer/`, which wraps the older `SDL_Renderer` 2D API — the two are unrelated APIs and must not share a class name) |
| Task prefix | `SDLGPU-` |
| CTest target | `SdlGpu_Smoke` (plus one CTest binary per capability, mirroring `WebGPU_Colored3D`, `D3D11_Smoke`, etc.) |

---

## Active execution order — do this one task at a time

1. ~~`SDLGPU-1`~~ – ~~`SDLGPU-12`~~ — Phases `SDLGPU-1`/`SDLGPU-2` (infrastructure, device/window/
   swapchain lifecycle, color+depth+stencil clear/present) done and verified 2026-07-15, all ✅
   except two 🟨 rows noted in each row's own Notes column (debug-mode toggle not yet wired;
   `IGraphicsBackend`-family concrete subclasses besides the main backend class don't exist yet).
2. ~~`SDLGPU-13`~~ – ~~`SDLGPU-25`~~ — Phases `SDLGPU-3`/`SDLGPU-4`/`SDLGPU-5` (shader authoring
   decision, sprite2d pipeline, `Texture2D`, vertex/index buffers, `SpriteBatch`) done and
   verified 2026-07-15 — see each row's own Notes column for the handful of 🟨/⬜ sub-items
   (BlendState/DepthStencilState/RasterizerState/ApplySamplerState dynamic mapping, streaming
   hints, a genuine multi-key pipeline cache) that are real 3D-draw-path or polish work, not
   blockers for the 2D milestone itself.
3. ~~`SDLGPU-26`~~ – ~~`SDLGPU-30`~~ — Phase `SDLGPU-6` (`colored3d`/`textured3d`/
   `colored_textured3d`/`lit_textured3d`, i.e. `BasicEffect`, plus real `DrawPrimitivesEx`/
   `DrawIndexedPrimitivesEx` stride dispatch and a real depth-test occlusion proof) done and
   verified 2026-07-15, all ✅. The pipeline-cache/state-mapping generality this phase actually
   needed (`SDLGPU-17`'s `GetOrCreate*` pattern extended to 4 shader families keyed by
   topology+depthTest+depthWrite+depthFunc) landed as part of it; `SDLGPU-18`–`21`'s full
   `BlendState`/`DepthStencilState`/`RasterizerState`/`ApplySamplerState` *dynamic* mapping did
   not (every 3D pipeline is still hardcoded opaque/no-cull/Linear+Clamp) — see those rows' own
   Notes for what's real vs. still open.
4. ~~`SDLGPU-31`~~/~~`SDLGPU-32`~~ — `AlphaTestEffect`/`DualTextureEffect` done and verified
   2026-07-15, all ✅ (real per-pixel discard proof; real two-sampler-multiply proof). `SDLGPU-33`
   (`EnvironmentMapEffect`) and `SDLGPU-34` (`SkinnedEffect`) deliberately deferred at this point —
   both landed later the same session once their own blockers (no `TextureCube` foundation yet; an
   unresolved 72-bone-palette push-size question plus a new stride-52 vertex format) were resolved,
   see items 7/8 below.
5. **Phase `SDLGPU-8` (render targets) is fully done.** `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
   (`RenderTargetCube`, including real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38`
   (`RenderTarget2D` MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` `GetData()` legs
   all done and verified 2026-07-15, all ✅ — see their own rows for the real multi-pass
   `EnsureFrameRendered` refactor, the `DrawTarget{rt,cube,face}` generalization, the
   `currentExtraMrtTargets_` MRT mechanism, the `ClampSampleCount`/automatic-resolve MSAA
   mechanism, and the new `ITextureBackend::GetData` cross-backend interface addition this
   required, plus `SDLGPU-37`'s own row for a real cross-cutting resource-lifetime gotcha found
   while testing it (see the status banner's own callout). `SDLGPU-39`'s remaining legs (swapchain/
   plain `Texture2D`/`TextureCube`) stay 🟨: the swapchain leg hit a real, unresolved segfault
   specific to the swapchain texture as a download/copy source (see that row) — do not re-attempt
   without reading it first; plain `Texture2D` needs no fix (already-accurate CPU shadow); plain
   `TextureCube`'s leg is now also done for real (see item 6 below) — so `SDLGPU-39` is now only
   blocked on the swapchain segfault.
6. **Phase `SDLGPU-9` (`Texture3D` + plain `TextureCube`) is fully done.** `SDLGPU-40` (`Texture3D`),
   `SDLGPU-41` (mips), and `SDLGPU-51` (plain `TextureCube`) are all done and verified 2026-07-15,
   all ✅ — see `SDLGPU-40`'s own row for a real `cycle=true` GPU-resource-orphaning bug this
   phase's own byte-exact round-trip tests caught and fixed (confirmed absent for `SDLGPU-51` too
   via its own dedicated regression check).
7. **`SDLGPU-33` (`EnvironmentMapEffect`) done and verified 2026-07-15**, once both cube-texture
   sources existed — see its own row for the new `env_map3d.glsl` shader pair and the dual-backend
   cube resolve this required.
8. **`SDLGPU-34` (`SkinnedEffect`) done and verified 2026-07-15 — Phase `SDLGPU-7` is now fully
   closed.** The bone-palette push-size question was resolved via a real empirical spike: this
   backend's push-uniform-data mechanism has a genuine, undocumented ~4096-byte cap per slot, so
   the 72-bone (4608-byte) palette is uploaded as a real storage buffer
   (`SDL_BindGPUVertexStorageBuffers`) instead of a uniform push — see its own row for the full
   binary-search finding and fix. All of Phases `SDLGPU-1`–`SDLGPU-9` are now done.
9. **`SDLGPU-42`/`SDLGPU-43` (custom `ShaderEffect`) done and verified 2026-07-15 — Phase
   `SDLGPU-10` is now fully closed.** User chose a
   real runtime `libshaderc` GLSL→SPIR-V compile over deferring — see `SDLGPU-42`'s own row for a
   genuine finding this decision uncovered along the way (Vulkan's own `ShaderEffect` support
   doesn't actually runtime-compile GLSL text at all, unlike what the class's own documented
   contract says). `SdlGpuEffectBackend` + full `SpriteBatch` custom-effect wiring
   (`SetCustomEffect`/per-`SpriteCommand` uniform snapshot at `Draw()`-call time/`RenderSprites()`
   pipeline selection) verified end-to-end via `SdlGpu_ShaderEffect`, 3/3 checks, through the real
   public `ShaderEffect`/`SpriteBatch.Begin(effect)` API. **Correction**: this item's own prior text
   claimed "every phase currently in this plan is done" — that was wrong; `SDLGPU-17`–`20`
   (dynamic `BlendState`/`DepthStencilState`/`RasterizerState`, and the genuine keyed pipeline
   cache their combination needs) were still `⬜`/🟨 at this point, found on a later re-check and
   closed as item 10 below. `GraphicsDeviceManager`'s vsync-forwarding gap (listed here as still
   open) was also fixed separately the same day on `fix/graphicsdevicemanager-vsync` (backend-
   independent, not SDL_GPU-specific — see that branch's own commit).
10. **`SDLGPU-17`–`20` (real dynamic `BlendState`/`DepthStencilState`/`RasterizerState`, plus the
    hash-keyed pipeline cache all three together need) done and verified 2026-07-15.** Every
    pipeline family (sprite + all 7 3D shader families) previously hardcoded Opaque blending,
    `CullMode::None`, `FillMode::Solid`, and no stencil test at all — `GraphicsDevice.BlendState`/
    `DepthStencilState`/`RasterizerState` assignments never reached this backend's own pipeline-
    baked state. A real bug was found and fixed along the way, not just new plumbing added: SDL_gpu
    exposes `ReferenceStencil` as a genuine per-draw *dynamic* value (`SDL_SetGPUStencilReference`),
    not pipeline-baked, and this call was missing from every draw path — a real per-pixel stencil
    test could never actually use a non-zero reference. New `SdlGpu_RenderState` test, 13/13 checks,
    all real `RenderTarget2D::GetData()` pixel assertions (not "didn't throw"): `BlendState.Additive`/
    `AlphaBlend`, a real stencil-mask-then-masked-draw proof (temporarily reverting the
    `SDL_SetGPUStencilReference()` fix reproduced the original bug exactly, confirming the test
    catches it), a `CullMode` differential (`CullCounterClockwise`/`CullClockwise`/`None`), a
    `FillMode` differential (`Solid`/`WireFrame`), and a `ScissorTestEnable`+`ScissorRectangle`
    clip proof. `RasterizerState.DepthBias`/`SlopeScaleDepthBias` are captured but deliberately not
    applied (no SDL_gpu per-draw dynamic-depth-bias equivalent exists at all, unlike Vulkan's
    `vkCmdSetDepthBias`); `TwoSidedStencilMode`'s CCW fields are wired but not separately empirically
    differentiated by a dedicated front/back test in this pass — both documented, deliberate scope
    boundaries, not silent gaps. Full `ctest -R "SdlGpu"` re-run: **14/14 passing** (stable across
    repeated runs; this real-display GPU suite is occasionally flaky run-to-run on an unrelated
    test — a transient segfault on a different test each time, gone on immediate rerun, reproduces
    on unmodified code too — not something this task introduced). Full `CnaTests` (4367 of 4375,
    excluding 8 pre-existing-crash `ContentManagerSkinnedModelTest` cases confirmed to segfault
    identically on unmodified `feature/sdlgpu` HEAD): 4364 passed, 2 pre-existing hardware-sensor
    skips, 1 pre-existing failure (`ContextRecoveryTest.GetDataThrowsAfterFullUploadWithRecoveryDisabled`,
    also confirmed to fail identically on unmodified `feature/sdlgpu` HEAD) — zero regressions from
    this task's own changes.
11. **`SDLGPU-21` (`ApplySamplerState` for direct 3D draws) done and verified 2026-07-15.** The
    sampler-object cache itself already existed and was already verified via `SpriteBatch`; this
    closed the missing piece — a new `samplerSlots_[16]` array set by `ApplySamplerState()` and
    read directly into each `DrawCommand`'s pre-existing `textureFilter`/`addressU`/`addressV`
    fields at `Queue*Draw()` time (slot 0 for every single-texture family; `DualTextureEffect`
    genuinely needed independent slots 0 and 1 for its two texture units, so its own
    `DualTextureDrawCommand` gained separate `texture0*`/`texture1*` fields and
    `RenderDualTextureDraws` now binds two distinct samplers instead of one shared one). Also fixed
    a latent, stale default: the 5 single-texture `DrawCommand` structs hardcoded `addressU=1`
    (Clamp) — corrected to `0` (Wrap), matching `SamplerState.LinearWrap`'s real `GraphicsDevice`
    default (`SpriteCommand`'s own separate Clamp default is deliberately unchanged — XNA's real
    `SpriteBatch` default genuinely is `SamplerState.LinearClamp`, not the same thing).
    New `SdlGpu_SamplerState` test, 7/7 real pixel checks (`TextureAddressMode` Wrap-vs-Clamp past
    `U=1`; `TextureFilter` Point-vs-Linear at a colour boundary, matching the hand-derived blend
    weight exactly; `DualTextureEffect`'s two texture units resolving independently) —
    temporarily reverting `ApplySamplerState()` to a no-op reproduced the exact predicted failure
    pattern on all 3 checks, confirming the test and fix both genuinely matter. Full
    `ctest -R "SdlGpu"`: **15/15 passing**. Full `CnaTests` (same exclusions as item 10 above):
    4364 passed, 2 pre-existing skips, 1 pre-existing failure — identical to item 10's own numbers,
    zero regressions.

12. **Render-target-destroyed-before-flush use-after-free fixed for real, 2026-07-15.** Asked the
    user which architectural approach to pursue (a simpler "defer the GPU texture-handle release"
    fix that only guarantees no crash, vs. a fuller redesign that also guarantees correct content)
    — user chose the fuller redesign. New `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState`
    structs hold the actual `SDL_GPUTexture*` handles + clear-pending state, owned via
    `std::shared_ptr` instead of directly by `SdlGpuRenderTargetBackend`/
    `SdlGpuRenderTargetCubeBackend`. `usedRenderTargetsThisFrame_`/
    `usedRenderTargetCubeFacesThisFrame_` (and every queued `DrawTarget`) hold/reference that
    shared_ptr, so a render target's own pending `Clear()`/draws still execute correctly and its GPU
    texture handles stay valid for anything still sampling them, even if the
    `SdlGpuRenderTargetBackend` wrapper itself is destroyed mid-`Draw()`, before `Present()` ever
    runs. `RenderToTarget`/`RenderToTargetCubeFace` now take the state struct directly (not the
    wrapper); every existing wrapper-level accessor (`ColorTexture()`, `GetWidth()`, etc.) still
    works unchanged for callers that resolve it while the wrapper is alive (e.g. `SpriteBatch::Draw`'s
    own dual-backend resolve), just delegating to the new struct internally — no public/external API
    changed. `SdlGpuRenderTargetBackend`'s own destructor is now nearly empty (just clears
    `currentRenderTarget_` if it was the active one); the actual GPU release is deferred inside
    `SdlGpuRenderTarget2DState`'s destructor via the existing `QueueTextureRelease` mechanism
    (`SDLGPU-19`'s row), which only runs once every reference (the wrapper's own, plus
    `usedRenderTargetsThisFrame_`'s, plus any queued `DrawCommand`'s) has released it.
    `currentExtraMrtTargets_` (MRT's own secondary-target tracking) was deliberately NOT converted —
    a real, separate, pre-existing dangling-pointer risk if an MRT secondary target is destroyed
    while still the active MRT binding, narrower and rarer than the sampled-as-texture-input case
    this task fixed; left as a documented, out-of-scope finding, not fixed here.
    New `SdlGpu_RenderTargetLifetime` test (3/3 checks): a `RenderTarget2D` created, drawn into,
    sampled by a still-pending `SpriteBatch` draw, and destroyed — all as a local variable inside
    one `Draw()` call, before `Present()` ever runs. Confirmed via `git stash`-style before/after
    testing: reverting just the deferred-release piece reproduces the original segfault reliably
    (5/5 runs); with the fix, the test not only avoids the crash but reads back the *correct*
    sampled content (proving the fuller redesign's real value over the simpler crash-only fix, which
    was verified along the way to leave the sampled content undefined/wrong even though it no longer
    crashes). **Also found and fixed a real, unrelated pre-existing regression while chasing this
    down**: `sdlgpu_envmap_test.cpp`'s own quad was wound CCW under the identity view/projection it
    uses — harmless while `SDLGPU-18/19/20`'s `RasterizerState.CullMode` was still hardcoded to
    `None`, but once real dynamic culling landed, the project's own default
    `RasterizerState.CullCounterClockwise` started genuinely culling it, silently regressing the
    test (confirmed via `git stash` to the `SDLGPU-21` commit, reproducing the failure
    deterministically 5/5 times — this had gone undetected in that task's own "14/14 passing"/
    "15/15 passing" claims, an honest correction, not a new gap). Fixed by flipping its winding to
    match this project's own established CW convention for identity-view/projection tests (same
    fix already applied to `sdlgpu_renderstate_test.cpp`/`sdlgpu_samplerstate_test.cpp` earlier the
    same day). Full `ctest -R "SdlGpu"`: **16/16 passing**, stable across 3 repeated runs. Full
    `CnaTests` (same exclusions as items 10/11): 4364/4367 passed, identical numbers to those
    items' own — zero regressions.

The documented open findings (swapchain-readback segfault, full-`CnaTests` in-process instability
under this backend in this environment, `currentExtraMrtTargets_`'s own narrower dangling-pointer
risk if an MRT secondary target is destroyed while still bound) remain the only unresolved items in
this plan.

---

## Phase SDLGPU-1 — Infrastructure and CMake integration

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-1 | Add `CNA_GRAPHICS_BACKEND=SDL_GPU` as a tenth valid value: extend the `CACHE STRING`/`set_property(... STRINGS ...)` list (`CMakeLists.txt` ~L94–95), the `option(CNA_BACKEND_SDL_GPU ...)` declaration and its inclusion in the mutual-exclusion `_cna_enabled_backends` list (~L97–147). | ✅ | Done 2026-07-15. Also updated `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` counter, which would otherwise fail under this backend. |
| SDLGPU-2 | Add the `elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")` branch (~L184–255) setting `BACKEND_DIR`, `BACKEND_TARGET`, `add_compile_definitions(CNA_BACKEND_SDL_GPU)`. | ✅ | Done 2026-07-15, mirroring `HEADLESS`/`SOFTWARE` — no `find_package` call needed. |
| SDLGPU-3 | Add the matching backend-library-target block (~L301+) and, if the backend needs test-only source files, its own `CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU"` guard (mirroring `HEADLESS`/`SOFTWARE`/`D3D11`/`D3D12` at ~L6497–6799). | ✅ | Link block done 2026-07-15 (`SDL3::SDL3` only). The `CNA_BUILD_TESTS` guard was added directly as `SdlGpu_Smoke`'s own registration (see SDLGPU-12), not as an empty placeholder. **Follow-up fix 2026-07-15 (adversarial-review finding):** the default `cmake --build cmake-build-sdlgpu` failed linking `cna_reference_dump`/`cna_demo_xact` with "undefined reference to `CNA::Logger::Warn`" — `SdlGpuGraphicsBackend.cpp` calls this CNA-defined symbol for its own non-fatal capability-gap warnings, hitting the exact same single-pass static-archive-scanning problem under GNU ld that D3D11/D3D12 already had a fix for (`target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)`, ~L420–432), but that fix's `if()` condition was never extended to cover `SDL_GPU`. Fixed by adding `SDL_GPU` to that condition. Verified: `cna_reference_dump` now links cleanly; `cna_demo_xact` still fails, but only on its unrelated post-build Content-directory copy step (missing `examples/demo_xact/Content`, a pre-existing XACT-audio-demo asset gap, confirmed unrelated to graphics/linking). |
| SDLGPU-4 | Create `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp` — class skeleton declaring every `IGraphicsBackend`-family interface (`IVertexBufferBackend`, `IIndexBufferBackend`, `ITextureBackend`, `ITextureCubeBackend`, `ITexture3DBackend`, `IRenderTargetBackend`, `IRenderTargetCubeBackend`, `IEffectBackend`, `ISpriteBatchBackend`, `IOcclusionQueryBackend`, `IGraphicsBackend`). | ✅ | **Row corrected 2026-07-16 — was stale.** This row was still marked 🟨 ("concrete subclasses don't exist yet") even though every one of these interfaces has had a real concrete implementation for a long time: `SdlGpuTextureBackend`, `SdlGpuTexture3DBackend`, `SdlGpuRenderTargetBackend`, `SdlGpuRenderTargetCubeBackend`, `SdlGpuTextureCubeBackend`, `SdlGpuVertexBufferBackend`, `SdlGpuIndexBufferBackend`, `SdlGpuEffectBackend`, `SdlGpuSpriteBatchBackend` all exist in this same header. Only `IOcclusionQueryBackend` has no concrete subclass — but that is `SDLGPU-44`'s own documented, permanent non-gap (no occlusion-query API exists in `SDL_gpu` at all), not something this row was ever waiting on. This row was simply never updated when those classes were built out phase by phase. |
| SDLGPU-5 | Create `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` — constructor/destructor scaffolding, explicit `ThrowUnsupported...`-style paths for every not-yet-implemented method, so the backend compiles and links from task 1. | ✅ | Done 2026-07-15 (`ThrowNotImplemented()`); `CreateTexture`/`CreateSpriteBatch`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` all throw, verified by `SdlGpu_Smoke`'s own throw checks. |

---

## Phase SDLGPU-2 — Device, window and swapchain lifecycle

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-6 | `SDL_CreateGPUDevice` — request `SDL_GPU_SHADERFORMAT_SPIRV` first (Linux/Vulkan driver target), debug-mode flag wired to a CNA-side validation/debug toggle. | ✅ | 2026-07-16. `debug_mode` now mirrors `D3D11GraphicsBackend`'s own `#ifndef NDEBUG` CNA-side toggle (design decision 12's "debug layer is a debug-build convenience, never a hard requirement" rationale) — a debug build requests `SDL_gpu`'s own validation layer, a release build does not. New `debugModeEnabled_` field + `IsDebugModeEnabledEXT()` accessor (mirrors `D3D11GraphicsBackend::IsDebugLayerEnabledEXT()`), logged at backend-init time. `SDL_GPUSupportsShaderFormats`-gated startup error not added — `SDL_CreateGPUDevice` returning null already throws with `SDL_GetError()`, judged sufficient for now. **Real, previously-silent bugs found and fixed via turning this on for real, not just a cosmetic wiring change:** enabling `debug_mode` for the first time surfaced 2 genuine, pre-existing `SDL_gpu` API-contract violations that had always been silently tolerated with `debug_mode` hardcoded `false` — both caused a genuine unkillable-by-SIGTERM hang under Vulkan validation, not just a warning. (1) `RenderTargetCube`'s own MSAA design (`SDLGPU-36`) used a 6-layer `SDL_GPU_TEXTURETYPE_2D_ARRAY` texture with `sample_count>1` — `SDL_gpu` explicitly forbids `sample_count>1` on any array texture ("For array textures: sample_count must be SDL_GPU_SAMPLECOUNT_1"). Fixed by switching to a single-layer `SDL_GPU_TEXTURETYPE_2D` texture shared across whichever face is currently the active render target (same convention `depthTexture` already uses), with `SDL_GPUColorTargetInfo.cycle=true` when writing into it (required per `SDL_gpu.h`'s own doc comment on reusing a bound resource across passes without cycling — a second, related hazard this fix also had to address) — `cubeTexture` itself (whether as the direct target or the resolve target) must never cycle, since that would wipe every other face already written this frame. (2) `Texture3D`/`TextureCube`'s mipmap generation (`SDLGPU-40`/`41`/`51`) called `SDL_GenerateMipmapsForGPUTexture` on textures created with `SAMPLER` usage only — `SDL_gpu` requires `SAMPLER | COLOR_TARGET` for that call. Fixed by widening `createInfo.usage` to include `COLOR_TARGET` when `mipMap` is requested (unchanged for the non-mipmap case). Verified: `SdlGpu_RenderTargetCube` (7/7), `SdlGpu_Texture3D` (6/6), `SdlGpu_TextureCube` (5/5) all pass cleanly with `debug_mode` genuinely on, no hang, full `ctest`/`CnaTests` sweep 4387/4387 zero regressions. **Two further, separate, non-blocking issues found in the same investigation, deliberately NOT fixed here (reported, not silently expanded into):** a Vulkan validation warning (`VUID-vkCmdDraw-renderPass-02684`, depth-stencil attachment incompatible between two render passes) appears during `SdlGpu_RenderTargetCube`'s depth-tested check but does not affect its correctness readback; and `Texture3D`'s own mip-chain auto-generation produces `vkCmdBlitImage`/`vkCreateImageView` validation errors specifically for the depth dimension (likely `SDL_gpu`'s blit-based generator assuming standard per-level depth-halving, which doesn't hold for this backend's own FNA-matching "depth does not participate in the mip-level count" convention) — `SdlGpu_Texture3D`'s existing mipmap check uses a uniform test color that cannot actually discriminate correct vs. corrupted output here, so this may be a real, still-undetected correctness gap in `Texture3D`'s generated-mipmap path specifically (authored, i.e. explicit per-level `SetData`, mipmaps are unaffected). |
| SDLGPU-7 | `SDL_ClaimWindowForGPUDevice` + present-mode negotiation (`SDL_WindowSupportsGPUPresentMode`, `SDL_SetGPUSwapchainParameters`) mapped from XNA's `PresentationParameters`/vsync equivalent. | ✅ | Done 2026-07-15. Verified both paths: VSync (default) and Immediate (every SDL_GPU example/test sets `gdm_->setSynchronizeWithVerticalRetraceProperty(false)` before first device creation — see this file's top-of-file note; this replaced an earlier direct-backend-poke workaround once `GraphicsDeviceManager`'s forwarding gap was fixed in `develop`). |
| SDLGPU-8 | Per-frame `SDL_AcquireGPUCommandBuffer` + `SDL_WaitAndAcquireGPUSwapchainTexture` (or `SDL_AcquireGPUSwapchainTexture` for the non-blocking variant), including the documented zero-size/minimized-window null-texture case. | ✅ | Done 2026-07-15. The null-texture case is handled (submit the empty command buffer, skip the frame) per `SDL_gpu.h`'s own documented contract, but not independently exercised by a real minimized-window test yet — code-path only for that specific branch. |
| SDLGPU-9 | Main render pass (`SDL_BeginGPURenderPass`/`SDL_EndGPURenderPass`) with color + depth + stencil `SDL_GPUColorTargetInfo`/`SDL_GPUDepthStencilTargetInfo` attachments. | ✅ | Done 2026-07-15, including real depth+stencil texture creation/recreation-on-resize (`EnsureDepthStencilTexture`, D24_UNORM_S8_UINT preferred, D32_FLOAT_S8_UINT fallback, per-device `SDL_GPUTextureSupportsFormat` query). Verified via `SdlGpu_Smoke`'s combined `Target\|DepthBuffer\|Stencil` clear, not yet by an actual depth-tested draw (no draw path exists yet). |
| SDLGPU-10 | Clear semantics — map XNA's `ClearOptions` combinations (`Target`/`DepthBuffer`/`Stencil` and their unions, i.e. `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) onto `SDL_GPULoadOp::CLEAR` vs `LOAD` per attachment. | ✅ | Done 2026-07-15, all 7 `IGraphicsBackend` clear methods implemented; `SdlGpu_Smoke` exercises the combined color+depth+stencil path every frame. |
| SDLGPU-11 | Present via `SDL_SubmitGPUCommandBuffer` (or `SDL_SubmitGPUCommandBufferAndAcquireFence` where a fence is needed for readback), with recoverable handling of a failed/occluded swapchain acquisition. | ✅ | Done 2026-07-15 for the one recoverable case `SDL_gpu` itself documents (null swapchain texture, e.g. minimized window — submit and skip, no throw). There is no WebGPU-style surface-loss/outdated/suboptimal recovery case to add here: `SDL_gpu`'s own API doesn't expose one at this level. **Hard-acquisition-failure recovery proven for real 2026-07-16**: a genuine `SDL_WaitAndAcquireGPUSwapchainTexture` failure (as opposed to the already-handled null-texture/minimized-window case) is hard to trigger via real hardware/driver-loss events, but `SDL_ReleaseWindowFromGPUDevice`/`SDL_ClaimWindowForGPUDevice` gives a safe, controlled way to force the exact same failure — un-claiming the window makes the next acquisition attempt fail for real, with a real `SDL_GetError()` message, through the identical code path a genuine device-loss event would take. New `SdlGpu_SwapchainRecovery` test, 5/5 real checks: normal frames render fine beforehand; releasing the window then forcing `Present()` throws as documented; re-claiming the window succeeds; a subsequent `Present()` succeeds with no exception (the backend recovers cleanly — `framePending_` correctly stays set across the failed attempt, so the same frame's queued `Clear()` isn't lost); many further frames afterward continue to render correctly, proving the backend stays fully usable, not just "didn't crash immediately". |
| SDLGPU-12 | First end-to-end proof: a minimal native window that initializes the backend, clears, presents at least 60 frames, then exits cleanly. | ✅ | Verified 2026-07-15: `examples/sdlgpu_smoke_test.cpp` + `SdlGpu_Smoke` CTest, real window + real `SDL_GPUDevice` (Vulkan driver) on this Linux dev machine, 60 frames of combined color+depth+stencil `Clear()`+`Present()`, plus `GetWindowInternal`/`GetRendererInternal`/`GetViewportSize`/`CreateVertexBuffer`/`CreateIndexBuffer16` checks — 6/6 checks, `ctest -R SdlGpu_Smoke` passes in ~2s (with VSync off; VSync itself was also confirmed working, at ~1s/frame on this environment's virtual display, before switching the smoke test to Immediate for speed). |

---

## Phase SDLGPU-3 — Shader authoring and resource-binding strategy

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-13 | **Decision task.** Choose the shader pipeline: (a) hand-author SPIR-V for the Vulkan driver by extending the existing `compile_shaders.py`/`libshaderc` runtime-compile pattern with `SDL_gpu`'s mandatory binding order, deferring DXBC/DXIL/MSL authoring; or (b) vendor the official [`SDL_shadercross`](https://github.com/libsdl-org/SDL_shadercross) tool to compile a single HLSL source to all four formats. | ✅ | Decided 2026-07-15: (a). `src/CNA/Internal/Backends/SdlGpu/shaders/compile_shaders.py` (adapted from the Vulkan backend's own script) compiles GLSL → SPIR-V at build time via `libshaderc`, following `SDL_gpu`'s *graphics-pipeline* binding convention specifically (vertex-stage textures=set0/UBOs=set1, fragment-stage textures=set2/UBOs=set3 — a different, narrower convention than the compute-pipeline one also documented in `SDL_gpu.h`; do not confuse the two when adding more shaders). `SDL_shadercross` remains unvendored; revisit (b) only when Windows/macOS driver work actually starts. |
| SDLGPU-14 | `sprite2d` SDL-GPU shader pair (vertex + fragment), compiled to SPIR-V, satisfying `SDL_GPUShaderCreateInfo`'s explicit `num_samplers`/`num_storage_textures`/`num_storage_buffers`/`num_uniform_buffers` counts. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + a real screenshot, see Phase `SDLGPU-5` below). **Found and fixed a real bug via the screenshot, not just the "no exception" CTest**: `SDL_gpu`'s Vulkan driver flips clip-space Y internally (for cross-backend NDC consistency) — the vertex shader's original `gl_Position = vec4(ndc, 0, 1)` silently rendered every sprite upside down (a texture's top row appeared at the bottom). Fixed by negating Y (`vec4(ndc.x, -ndc.y, 0, 1)`), confirmed by an isolated single-sprite diagnostic (`examples/sdlgpu_diag_single_sprite.cpp`, kept as a permanent manual diagnostic tool, same precedent as `cna_diag_d3d12_swapchain`) before and after the fix. |
| SDLGPU-15 | `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping for the four stock vertex strides: `VertexPositionColor` (16), `VertexPositionTexture` (20), `VertexPositionColorTexture` (24), `VertexPositionNormalTexture` (32). | ✅ | **Row corrected 2026-07-15 — was stale.** This row was still marked ⬜ ("not started") even though Phase `SDLGPU-6` (`SDLGPU-26`–`29`, all ✅, done and verified 2026-07-15) actually built exactly this: `colored3d` (stride 16), `textured3d` (stride 20), `colored_textured3d` (stride 24), and `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) each have their own real `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping, all runtime-verified via screenshots and CTest. This row was simply never updated when that phase closed. |
| SDLGPU-16 | Per-draw uniform delivery via `SDL_PushGPUVertexUniformData`/`SDL_PushGPUFragmentUniformData` (world/view/projection, `DiffuseColor`, alpha-test params, light params), respecting the documented std140 alignment rule (`vec3`/`vec4` fields 16-byte aligned). | ✅ | **Row corrected 2026-07-15 — was stale.** This row was still marked 🟨 ("3D-specific payloads not implemented") even though Phase `SDLGPU-6`/`SDLGPU-7` (all ✅, done and verified 2026-07-15) actually built exactly this: world/view/projection + `DiffuseColor` push in `textured3d`/`colored_textured3d` (`SDLGPU-27`/`28`), full `ComputeLights()` light params in `lit_textured3d` (`SDLGPU-29`), alpha-test params in `AlphaTestEffect` (`SDLGPU-31`), and per-bone skin-matrix delivery in `SkinnedEffect` (`SDLGPU-34`, which also empirically found and documented the real ~4096-byte `SDL_PushGPUVertexUniformData` per-slot cap this row asked to "measure empirically" — worked around via a storage buffer instead). This row was simply never updated when those phases closed. |

---

## Phase SDLGPU-4 — Pipelines and render state

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-17 | `SDL_GPUGraphicsPipelineCreateInfo` construction plus a pipeline cache keyed by (shader pair, vertex format, blend state, depth/stencil state, rasterizer state, sample count, color/depth target formats). | ✅ | 2026-07-15 (closed alongside `SDLGPU-18`–`20`). `PipelineCacheKey()` now folds topology/depthTest/depthWrite/depthFunc/colorFormat plus the *full* `RenderStateSnapshot` (blend, cull, fill mode, stencil) via a boost-style `HashCombine()` chain, replacing the old 5-field hand-packed `int` key — a hash was chosen specifically to avoid `VulkanGraphicsBackend`'s own documented bit-packing budget problem once every render-state dimension is folded in. Disabled dimensions (blend off, stencil off, two-sided off) always collapse their own sub-fields out of the hash regardless of value, mirroring Vulkan's own `PackBlendBits`'s "collapse to 0 when disabled" rule, so irrelevant values don't fragment the cache with duplicate pipelines. All 12 cache maps (sprite + 7 3D families, `AlphaTest3D`'s stride-24 map included) changed from `unordered_map<int, ...>` to `unordered_map<size_t, ...>`; the custom `ShaderEffect` pipeline cache (`SdlGpuEffectBackend::pipelines_`) deliberately kept as `int` since its own pipeline stays out of this task's scope (see `SDLGPU-19`/`20`'s own notes). |
| SDLGPU-18 | `BlendState` mapping — XNA `BlendState` → `SDL_GPUColorTargetBlendState` (`SDL_GPUBlendFactor`/`SDL_GPUBlendOp`). | ✅ | 2026-07-15. `ApplyBlendState()` overridden; `ToBlendFactor`/`ToBlendOp` mirror `VulkanGraphicsBackend`'s own XNA-ordinal tables exactly. A new shared `FillBlendState()` fills every pipeline family's (sprite + all 7 3D shader families) own `SDL_GPUColorTargetBlendState` from a captured `RenderStateSnapshot` instead of a hardcoded Opaque/no-op struct. Verified: `BlendState.Additive` over a Red background reads back `(255,128,0)` (real colour-add, not a stuck-Opaque overwrite — note XNA's own `Color.Green` is `(0,128,0)`, not lime); `BlendState.AlphaBlend` (`One`/`InverseSourceAlpha`) with a half-alpha source over White reads back mid-grey. |
| SDLGPU-19 | `DepthStencilState` mapping — `SDL_GPUDepthStencilState` (`SDL_GPUCompareOp`, `SDL_GPUStencilOpState`), covering `DepthBufferEnable`/`DepthBufferWriteEnable`/`StencilEnable` and all three XNA stencil-op fields (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`). | ✅ | 2026-07-15. `ApplyDepthStencilState()` overridden, forwarding into the same `depthTestEnabled_`/`depthWriteEnabled_`/`depthCompareFunction_` fields the pre-existing `SetDepthTestEnabled`-style shortcuts already used (whichever setter runs last wins, by design), plus a new `stencilParams_`/`stencilReadMask_`/`stencilWriteMask_`. A new shared `FillDepthStencilState()` bakes `StencilFail`/`StencilPass`/`StencilDepthBufferFail`/`StencilFunction` into every pipeline family's front/back `SDL_GPUStencilOpState` (`TwoSidedStencilMode=false` copies front into back, matching this project's own EasyGL/Vulkan fallback convention — **not** empirically differentiated front-vs-back in this pass; a dedicated two-sided CW/CCW differential test was not written, a documented scope boundary, not a silent gap). **Real bug found and fixed along the way:** `GraphicsDevice.ReferenceStencil`/`DepthStencilState.ReferenceStencil` were captured into `referenceStencil_` but never actually reached the GPU — SDL_gpu exposes stencil reference as a genuine *per-draw dynamic* value (`SDL_SetGPUStencilReference`), not a pipeline-baked one, and this call was missing entirely from every draw path. Fixed by adding `stencilReference` to `RenderStateSnapshot` (captured per-`DrawCommand`/`SpriteCommand` at queue time, like the rest of the snapshot) and calling `SDL_SetGPUStencilReference()` in all 8 `Render*Draws`/`RenderSprites` call sites. Verified via a real per-pixel stencil-mask proof (`StencilFunction=Always`/`StencilPass=Replace`/`ReferenceStencil=1` writes a mask over the left half only; a second full-screen `StencilFunction=Equal`/`ReferenceStencil=1` draw shows its new colour ONLY on the left, leaving the right half's background untouched) — **temporarily disabling the new `SDL_SetGPUStencilReference()` calls reproduced the original bug exactly** (right half wrongly passed too), confirming the test and the fix both genuinely matter. **Second real bug found and fixed 2026-07-15 (adversarial-review finding):** `stencilReadMask_`/`stencilWriteMask_` (`DepthStencilState.StencilMask`/`StencilWriteMask`) were captured by `ApplyDepthStencilState()` but were genuinely dead code — `FillDepthStencilState()` always baked `compare_mask`/`write_mask` as hardcoded `0xFF` regardless of their value, and `PipelineCacheKey()` didn't hash them at all. Fixed by moving both into `StencilKeyParams` (`readMask`/`writeMask`, default `0xFF` matching real XNA's `0x7FFFFFFF` truncated to SDL_gpu's `Uint8` fields), hashing them (truncated to `Uint8`) into the pipeline cache key, and having `FillDepthStencilState()` apply the real values instead of the hardcoded constant. New `SdlGpu_RenderState` checks G/H (write-mask: only the half of a render target with a non-zero `StencilWriteMask` actually receives a stencil write, verified via a subsequent `StencilFunction=Equal` reveal pass; read-mask: a deliberately mismatched `StencilFunction=Equal`/`ReferenceStencil=0` compare still passes everywhere because `StencilMask=0x00` zeroes both sides of the compare) bring the suite to 16/16. Git-stash-verified: reverting `FillDepthStencilState()`'s two lines back to hardcoded `0xFF` reproduced exactly the predicted G/H failures; restoring the fix returned 16/16. |
| SDLGPU-20 | `RasterizerState` mapping — `SDL_GPURasterizerState` (`SDL_GPUFillMode`, `SDL_GPUCullMode`, `SDL_GPUFrontFace`), covering `CullMode.CullClockwiseFace`/`CullCounterClockwiseFace`/`None` and `FillMode.WireFrame`/`Solid`. | ✅ | 2026-07-15. `ApplyRasterizerState()` overridden; `ToCullMode` mirrors this project's own EasyGL backend's real, hardware-validated cull mapping (`CullClockwiseFace`→`SDL_GPU_CULLMODE_BACK`, `CullCounterClockwiseFace`→`SDL_GPU_CULLMODE_FRONT`, `front_face` hardcoded `COUNTER_CLOCKWISE` everywhere — **not** Vulkan's own documented Task-870 front/back swap, since this backend's 3D shaders need no NDC Y-flip like EasyGL's). A new shared `FillRasterizerState()` bakes `cull_mode`/`fill_mode` into every pipeline family from the captured snapshot. `SetScissorRect()`/`ApplyScissorForPass()` also added (`SDL_SetGPUScissor`, applied once per render pass using the live scissor state as of that pass's own flush — **not** re-snapshotted per queued draw like blend/cull/stencil, a deliberate, documented simplification; real XNA games rarely change `ScissorRectangle` differently across different render targets within one frame). Depth bias (`RasterizerState.DepthBias`/`SlopeScaleDepthBias`) is captured but **not applied** — SDL_gpu has no dynamic per-draw depth-bias equivalent to Vulkan's `vkCmdSetDepthBias` at all, so this would require folding floats into the pipeline cache key; a genuine SDL_gpu API-surface limitation, not a silent drop. Verified: the same CW-wound quad shows visible under `CullCounterClockwise` (this project's own default) and `CullNone`, culled under `CullClockwise`; the same triangle's centroid pixel is filled under `FillMode.Solid` but stays background under `FillMode.WireFrame`; a full-screen draw with `ScissorTestEnable=true` + a left-half `ScissorRectangle` only affects the left half. |
| SDLGPU-21 | `SamplerState` mapping — `SDL_GPUSamplerCreateInfo` (`SDL_GPUFilter`, `SDL_GPUSamplerMipmapMode`, `SDL_GPUSamplerAddressMode`) for per-slot Wrap/Clamp/Mirror + Point/Linear/Anisotropic. | ✅ | 2026-07-15. `ApplySamplerState(slot, filter, addressU, addressV, maxAnisotropy)` overridden — stores into a new `samplerSlots_[16]` array (matching `SamplerStateCollection::MaxSamplers`), read directly into each `DrawCommand`'s own pre-existing `textureFilter`/`addressU`/`addressV` fields at `Queue*Draw()` time (not via `RenderStateSnapshot`, since these fields already existed and were just hardcoded before). Every direct-3D-draw family (`Textured`/`LitTextured`/`AlphaTest`/`EnvMap`(diffuse texture0 only — the env map itself stays fixed Linear+Clamp, unchanged)/`Skinned`) now reads slot 0's live sampler state instead of a hardcoded Linear+Clamp. **`DualTextureEffect` genuinely needed two independent slots**, not one: its `DualTextureDrawCommand` gained separate `texture0Filter/AddressU/AddressV` (slot 0) and `texture1Filter/AddressU/AddressV` (slot 1) fields, and `RenderDualTextureDraws` now binds two distinct `SDL_GPUSampler` objects instead of reusing one shared sampler for both textures — real XNA lets `GraphicsDevice.SamplerStates[0]`/`[1]` differ for these two texture units. Also corrected the 5 single-texture `DrawCommand` structs' stale hardcoded default (`addressU=1`/`addressV=1`, i.e. Clamp) to `0`/`0` (Wrap), matching `SamplerState.LinearWrap` — the real XNA `GraphicsDevice.SamplerStates[slot]` default (`SpriteCommand`'s own separate default is deliberately left at Clamp, since XNA's real `SpriteBatch` default is `SamplerState.LinearClamp`, a distinct and correct behavioral difference, not an inconsistency to fix). `maxAnisotropy` is stored but not applied — `GetOrCreateSampler()`'s cache has no anisotropic-filtering dimension at all, a pre-existing limitation shared with `SpriteBatch`'s own sampler path, not introduced here. New `SdlGpu_SamplerState` test, 7/7 real `RenderTarget2D::GetData()` pixel checks: `TextureAddressMode` (`PointWrap` reads a wrapped/repeated texel past `U=1`, `PointClamp` reads the held edge texel instead); `TextureFilter` (`PointClamp` snaps to a sharp texel at a colour boundary, `LinearClamp` reads a real, distinctly different blended value — matches the hand-derived blend weight exactly); and `DualTextureEffect`'s two texture units resolving independently (`texture0=Wrap`/`texture1=Clamp` at the same shared UV multiplies to White; either texture using the *other's* address mode would multiply to Black instead — temporarily reverting `ApplySamplerState()` to a no-op reproduced exactly this failure pattern, confirming the test and fix both genuinely matter). Full `ctest -R "SdlGpu"`: **15/15 passing**, zero regressions. |
| SDLGPU-52 | Draw-call ordering — queued 3D/sprite draws must render in the game's real chronological `Draw()`/`SpriteBatch.Draw()` issue order within one render pass, not grouped by shader family, so alpha-blend/transparency layering between interleaved 3D and 2D draws is correct. | ✅ | 2026-07-15 (adversarial-review finding #4). The backend always rendered all 7 3D shader families first, then every sprite last, regardless of real call order — a game doing background `SpriteBatch` → 3D model → HUD `SpriteBatch` got both sprite batches rendered after the 3D draw, breaking layering. Fixed with a new `drawOrder_` (`vector<QueuedDrawRef{kind,index}>`) appended to by every `Queue*Draw()`/`QueueSprite()` call right after its own family `push_back` — no sort needed, since append-at-call-time already is real chronological order. The old fixed `RenderColoredDraws(); RenderTexturedDraws(); ... RenderSprites();` sequence (3 call sites: `EnsureFrameRendered`/`RenderToTarget`/`RenderToTargetCubeFace`) is replaced by one `RenderQueuedDraws()` that replays `drawOrder_` once, dispatching each ref to a new small per-kind `Issue*Draw()` function (mechanical extraction of each old family loop's body, same readiness/target-filter rules, just callable per-index). `boundPipeline` is now tracked globally across every kind (previously only sprites had this rebind-skip). New `SdlGpu_DrawOrder` test, 2/2 real `RenderTarget2D::GetData()` checks: an opaque 3D quad and an opaque sprite drawn into the same target with depth testing off, in both orders (sprite-then-3D and 3D-then-sprite) — whichever was issued LAST correctly wins the final pixel each time. Git-stash-verified: temporarily recreating the old "all 3D first, sprites last" behavior (two passes over `drawOrder_` instead of one) reproduced exactly the predicted failure (the sprite-then-3D check incorrectly read back the sprite's color); restoring the fix returned 2/2. Full `ctest -R "SdlGpu"` + `CnaTests`: 17/17 + 4386/4386, zero regressions. |

---

## Phase SDLGPU-5 — 2D vertical slice: Texture2D + buffers + SpriteBatch

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-22 | `SdlGpuTexture2DBackend` — `SDL_CreateGPUTexture` + upload via `SDL_CreateGPUTransferBuffer`/`SDL_MapGPUTransferBuffer`/`SDL_UnmapGPUTransferBuffer`/`SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture`/`SDL_EndGPUCopyPass`. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + screenshot). `R8G8B8A8_UNORM`, `SAMPLER` usage only, single mip level — no render-target usage, no mip chain, no 3D/cube variants (later phases). |
| SDLGPU-23 | `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` — `SDL_CreateGPUBuffer` (`VERTEX`/`INDEX` usage) + upload via the same transfer-buffer/copy-pass pattern; `SetDataWithOptions` `Discard`/`NoOverwrite` streaming hints. | ✅ | `SetData`/`SetData16`/`SetData32` done and runtime-verified (`SdlGpu_Smoke`'s round-trip checks); every upload recreates the transfer buffer and grows the GPU buffer only when capacity is exceeded. **`SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions` now real overrides, done 2026-07-16** — maps directly to `SDL_UploadToGPUBuffer`'s own `cycle` flag, mirroring `EasyGLVertexBufferBackend`'s established `Discard`→orphan/`NoOverwrite`→in-place convention: `Discard`/`None` cycle the buffer to a fresh backing resource (`SDL_gpu.h`'s own documented `cycle=true` behavior, avoiding a GPU stall on any in-flight read of the old data — this was ALREADY the plain `SetData()`/`SetData16()`/`SetData32()` path's hardcoded behavior, unchanged); `NoOverwrite` does not cycle (`cycle=false`, the caller's own promise that no in-flight GPU read is being overwritten). **Verification note, stated honestly:** a full-buffer overwrite's correctness is identical either way — `cycle` only affects whether the GPU stalls on in-flight reads of the old backing memory, not what ends up readable afterward, since this backend always replaces the buffer's entire contents in one call (never a partial sub-range) — so `SdlGpu_Smoke`'s new checks (byte-exact round-trip after `Discard` then `NoOverwrite`, for both vertex and index buffers) prove the overrides are real and no longer silently ignored (the previous `IGraphicsBackend` default behavior), not a visually-distinguishing behavior difference, since none exists for this call pattern. |
| SDLGPU-24 | `SdlGpuSpriteBatchBackend` — batch quads, bind the `sprite2d` pipeline, bind texture+sampler via `SDL_BindGPUFragmentSamplers`, draw via `SDL_DrawGPUIndexedPrimitives`. Must cover source rectangles, tint/alpha, rotation, both flips, and Linear/Point + Clamp/Wrap/Mirror sampling. | ✅ | Done and runtime-verified 2026-07-15 — uses `SDL_DrawGPUPrimitives` (non-indexed, 6 vertices/sprite, one draw call per sprite into a shared per-frame vertex buffer) rather than `SDL_DrawGPUIndexedPrimitives`; behaviorally equivalent for this milestone, not yet batched into fewer draw calls. `SdlGpu_2D` CTest covers source rects, tint, alpha, rotation, both flips, and Point+Wrap/Linear+Wrap sampling; all confirmed visually correct via screenshot (see `SDLGPU-25`). |
| SDLGPU-25 | **First milestone gate.** A demo/smoke harness renders textured, tinted, rotated, flipped sprites and survives a resize through the SDL GPU backend with no validation error, device loss, or loader failure. | ✅ | Verified 2026-07-15: `examples/sdlgpu_2d_test.cpp` (`SdlGpu_2D` CTest, 3/3 checks) plus a real screenshot (captured via `import -window`, not just "didn't throw") of a 6-sprite scene — opaque quadrant texture, alpha-blended tint, 45°-rotated quadrant, combined horizontal+vertical flip (colors correctly permuted: white/blue/green/red corners), and Point+Wrap/Linear+Wrap sampling with a source rect exceeding the texture bounds (correctly tiles 2×2, unlike the Clamp-default sprites). The screenshot caught and led to fixing `SDLGPU-14`'s Y-flip bug — this is exactly the kind of defect a "did it throw" CTest alone cannot catch, and the reason this milestone is gated on a real visual check, not source inspection. Window-resize survival specifically was not re-tested this session (already covered by `SDLGPU-8`'s swapchain-acquisition handling) — no code path differs between this test and the resize-tested `SdlGpu_Smoke` test. |

---

## Phase SDLGPU-6 — Core 3D vertex formats and BasicEffect

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-26 | `colored3d` pipeline + shader + `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` dispatch (stride 16), with real depth-test verification. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_3D` CTest + screenshot). **Confirmed empirically that, unlike `sprite2d.vert.glsl`, this shader needs NO Y-flip**: `gl_Position = pc.mvp * vec4(inPos,1)` with no negation renders a red/green/blue triangle in the exact expected orientation — SDL_gpu's Vulkan-driver Y-flip only affected the hand-computed pixel-space NDC math in the sprite shader, not a real XNA projection matrix (which already encodes the correct convention). Real depth test verified via a genuine occlusion proof (see SDLGPU-30's note), not just "didn't throw". |
| SDLGPU-27 | `textured3d` (stride 20) — real `Texture2D` sampling, `DiffuseColor` genuinely multiplying it. | ✅ | Done and runtime-verified 2026-07-15 — screenshot confirms the exact quadrant-texture colors/orientation. Fragment shader receives `pc` via its own `SDL_PushGPUFragmentUniformData` push (set 3, binding 0) — a full second push of the same 128 bytes already pushed to the vertex stage (set 1, binding 0), simpler than threading a `textureEnabled` varying through, at the cost of one extra push call per draw. |
| SDLGPU-28 | `colored_textured3d` (stride 24) — vertex-color mixing combined with texture sampling. | ✅ | Done and runtime-verified 2026-07-15 — shares `textured3d`'s fragment shader (`Shaders::kTextured3dFragSpv`) unchanged, only the vertex shader/vertex-input-state differ. The screenshot's green-tinted quad looked unexpectedly dark at first glance — turned out to be **correct**: XNA's `Color::Green` is `(0,128,0)`, not lime `(0,255,0)` (the same real behavioral fact this project's Software backend caught independently, 2026-07-13), so a green-tinted quadrant texture multiplies red/blue channels to black — exactly what rendered. |
| SDLGPU-29 | `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) — FNA's `Lighting.fxh` `ComputeLights()` (3 directional lights, ambient, Blinn-Phong specular, emissive). | ✅ | Done and runtime-verified 2026-07-15 via `BasicEffect.EnableDefaultLighting()`'s real 3-light rig — screenshot shows a visibly different (lit/washed) appearance vs. the plain `textured3d` quad, proving the lighting math genuinely runs. Ported the algorithm from `VulkanGraphicsBackend`'s `lit_textured3d.{vert,frag}.glsl` with the safe-normalize guard from the WebGPU port's own `WEBGPU-22`/`33` finding (a disabled light's zero direction vector). **Did not need WebGPU's CPU-precomputed-normal-matrix workaround** — GLSL has a built-in `inverse()` (unlike WGSL), so the vertex shader computes `transpose(inverse(mat3(world)))` directly, matching `VulkanGraphicsBackend` exactly; the second UBO (`LitLightParams`, vertex+fragment slot 1) is 224 bytes/56 floats here, not WebGPU's 272/68 (no normal-matrix slots needed). |
| SDLGPU-30 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` `GpuDrawParams` dispatch by vertex stride, matching the dispatch-by-stride convention every other 3D-capable backend in this codebase already uses. | ✅ | Done and runtime-verified 2026-07-15. Simpler than `WebGPUGraphicsBackend`'s own dispatch: `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific `GpuDrawParams` fields are not yet checked (later phases), so dispatch is purely stride+`texture0`-based. **Real depth-test proof**: a nearer (blue) and farther (red) quad, same size/position, drawn in that order with `SetDepthTestEnabled(true)`; the screenshot shows solid, unmixed blue with zero red bleed-through — the farther quad's fragments were genuinely rejected by the depth buffer, not just silently painted-over (which would show red instead, since it drew second). |

---

## Phase SDLGPU-7 — Remaining stock effects

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-31 | `AlphaTestEffect` (per-pixel discard) — strides 20/24/32. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_Effects` CTest + screenshot: `AlphaFunction=Greater`/`ReferenceAlpha=128` on a texture with an opaque top half and a fully-transparent bottom half genuinely discards the bottom half — the `CornflowerBlue` background shows through with a hard edge, not a translucent blend, proving real `discard`, not alpha blending). Strides 20/32 share `alpha_test3d.vert.glsl`/one shader but need distinct pipelines (different vertex-input-state) — the pipeline cache key folds in the stride explicitly for this one shader family, unlike every other family here which is already stride-specific by construction. |
| SDLGPU-32 | `DualTextureEffect` (two-sampler multiply, `tex1.rgb*=2.0; result=tex1*tex2*tint`) — strides 20/24. | ✅ | Done and runtime-verified 2026-07-15 — screenshot shows the exact predicted result of multiplying a red/green/blue/white quadrant texture by a uniform yellow second texture: red/green pass through unchanged, blue becomes black (yellow has no blue channel), white becomes yellow. An unambiguous proof the real two-sampler multiply runs, not just one texture being shown. |
| SDLGPU-33 | `EnvironmentMapEffect` (cubemap reflection) — needs `SDL_GPU_TEXTURETYPE_CUBE` texture creation + `SDL_GPUCubeMapFace`-indexed per-face upload. | ✅ | 2026-07-15. New `env_map3d.vert.glsl`/`env_map3d.frag.glsl` (stride-32 `VertexPositionNormalTexture`, identical vertex layout to `lit_textured3d.glsl`), compiled via `compile_shaders.py`. Mirrors `VulkanGraphicsBackend`'s own `env_map3d.vert/frag.glsl` technique field-for-field (reflect(-E,N) off a world-space normal, Fresnel-weighted or flat blend factor, FNA's real `mix(baseColor, envSample*combinedAlpha, blendFactor) + envMapSpecular*envSample.a*combinedAlpha` lerp semantics) — minus fog, deliberately deferred the same way `lit_textured3d.glsl` already defers it for this backend. A new `SdlGpuTextureCubeBackend::Texture()`/`SdlGpuRenderTargetCubeBackend::CubeTexture()` dual-backend resolve (mirrors `SpriteBatch::Draw`'s own `SdlGpuTextureBackend`-vs-`SdlGpuRenderTargetBackend` resolve) lets `GpuDrawParams::envMap` come from either a plain `TextureCube` (`SDLGPU-51`) or a `RenderTargetCube` (`SDLGPU-36`) — both now real backends. Full `EnvMapDrawCommand`/`CreateEnvMapResources`/`DestroyEnvMapResources`/`GetOrCreatePipelineEnvMap3D`/`QueueEnvMapDraw`/`RenderEnvMapDraws` plumbing added, mirroring `lit_textured3d`'s own family shape exactly; wired into `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s dispatch with the same precedence `VulkanGraphicsBackend`/`WebGPUGraphicsBackend` already established (alpha test > dual-texture > env-map > plain lit/textured, since env-map and lit-textured share stride 32); added to all 3 `UploadSceneDrawData`/`RenderEnvMapDraws`(swapchain + 2 render-target passes)/`ReleaseSceneDrawBuffers` call sites. Verified via a new `SdlGpu_EnvMap`, 3/3 checks, directly ported from this project's own existing `vulkan_environmentmapeffect_amount_one_test.cpp` (same values/tolerance, `RenderTarget2D::GetData()` readback instead of `GetBackBufferData()` since this backend's swapchain-download path segfaults — see `SDLGPU-39`'s row): `EnvironmentMapAmount=1` with a solid white cubemap fully replaces the lit/textured color (FNA's real lerp semantics, not an additive contribution); the same with a solid gray cubemap reads back gray, not white or the diffuse texture's own color (proves the cube sample is genuinely being read, not a hardcoded/leftover value); `EnvironmentMapAmount=0` reads back the plain emissive-lit result, nowhere near either cubemap color (proves the blend amount is real, not always-on). `EnvironmentMapEffect::FresnelEnabled` has no public setter in this codebase (defaults permanently `true`, matching real XNA) — all 3 checks exercise the Fresnel-enabled code path by construction, not a separate untested branch. Full `ctest -R "SdlGpu"` re-run: **11/11 passing**, zero regressions. |
| SDLGPU-34 | `SkinnedEffect` (bone-matrix palette). | ✅ | 2026-07-15. New `skinned3d.vert.glsl` (stride-52 `VertexPositionNormalTextureSkinned`: pos/normal/uv + `vec4` blend weight + `SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4` blend indices, read as `uvec4` — ported directly from `VulkanGraphicsBackend`'s own `skinned3d.vert.glsl`, including its established "skin matrix alone transforms the normal, no separate World normal-matrix contribution" simplification). Its fragment stage reuses `litTexturedFragmentShader_` unchanged — byte-identical varying interface and `SkinnedLightParams`/`LitLightParams` UBO layout, so no separate skinned fragment shader exists at all. **Real empirical spike result, found via this task's own byte-exact-position pixel test (binary-searching bone indices 0→71): `SDL_PushGPUVertexUniformData` has a real ~4096-byte cap per slot on this Vulkan-backed environment** — bone indices 0–63 (byte offsets within the first 4096 bytes) read back correctly, but 64–71 (bytes 4096–4608) silently did not update, producing a degenerate skin matrix (all-zero) that discarded the geometry entirely. This cap is **not documented anywhere in `SDL_gpu.h`** — found only by writing the real test the plan's own row anticipated needing. Fixed via the plan's own anticipated fallback: the 72-bone palette (4608 bytes) is uploaded as a real `SDL_GPUBuffer` (`SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ`, uploaded through the same copy-pass mechanism as vertex/index data) and bound via `SDL_BindGPUVertexStorageBuffers`, not pushed as a uniform — a storage buffer has no such per-slot size cap. Per `SDL_CreateGPUShader`'s own binding convention, vertex-stage storage buffers live at `set=0` (same set as sampled/storage textures, ahead of uniform buffers at `set=1`) — the shader declares `layout(std430, set=0, binding=0) readonly buffer BoneBlock { mat4 bones[72]; }`. `PC`/`SkinnedLightParams` still push normally via `SDL_PushGPUVertexUniformData` at vertex slots 0/1 (208 bytes total, nowhere near the 4096-byte cap). Verified: new `SdlGpu_Skinned`, 3/3 checks, Checks A/B directly ported from this project's own existing `vulkan_skinnedeffect_translation_bone_test.cpp`/`vulkan_skinnedeffect_twobone_blend_test.cpp` (same geometry/bone values, `RenderTarget2D::GetData()` readback instead of `GetBackBufferData()` since this backend's swapchain-download path segfaults — see `SDLGPU-39`'s row): a single translation bone (index 0) and a two-bone 50/50 weighted blend (indices 0,1) both shift a quad by the expected amount. Check C is the spike proof itself: a translation bone at index 71 (the LAST slot of the full 72-matrix palette, all others Identity) produces the exact same shift — proving the full 4608-byte palette is transmitted intact and indexable at the far end via the storage-buffer path. This closes Phase `SDLGPU-7` entirely. |

---

## Phase SDLGPU-8 — Render targets

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-35 | `RenderTarget2D` — a `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET \| SDL_GPU_TEXTUREUSAGE_SAMPLER` texture, bound as `SDL_GPUColorTargetInfo.texture` in one pass, sampled in a later pass. | ✅ | 2026-07-15. Required a real architectural refactor, not just a new class: every pipeline cache (`GetOrCreatePipelineColored3D`/`Textured3D`/`ColoredTextured3D`/`LitTextured3D`/`AlphaTest3D`/`DualTexture3D`/sprite) now takes a `colorFormat` param folded into its cache key (previously hardcoded `SDL_GetGPUSwapchainTextureFormat`), and `EnsureFrameRendered()` is now genuinely multi-pass: every distinct `SdlGpuRenderTargetBackend` used this frame gets its own render pass (in first-bind order, via `RenderToTarget()`), all before the swapchain's own pass — this is what makes "bound in one pass, sampled in a later pass" real rather than aspirational. Render targets standardize on `SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM` regardless of the swapchain's native format (XNA's `CreateRenderTarget2D` interface has no format parameter anyway). `Clear()`/`ClearDepth()`/`ClearStencil()` now route to whichever `SdlGpuRenderTargetBackend` is currently bound (each RT owns its own pending-clear state) instead of unconditionally hitting the swapchain's. `SpriteBatch::Draw()` had to learn to resolve either `SdlGpuTextureBackend` (plain `Texture2D`) or `SdlGpuRenderTargetBackend` (`RenderTarget2D`) to a raw `SDL_GPUTexture*`, since the two are unrelated concrete classes. `mipMap` is supported for real (`SDL_GenerateMipmapsForGPUTexture` after that target's pass ends); `multiSampleCount > 0` was a documented throw at the time this row was first closed (`SDLGPU-38` not yet implemented) — real MSAA landed the same session, see `SDLGPU-38`'s own row. Verified via `SdlGpu_RenderTarget2D` (4/4 checks: Clear-only fill, a real colored3d triangle, a depth-tested pair of overlapping quads each with this target's own dedicated depth texture, and a `MultiSampleCount` property-fidelity check) plus a real screenshot (see below) — not just "didn't throw". |
| SDLGPU-36 | `RenderTargetCube` — 6-face color-target texture, including MSAA and mip levels beyond face 0. | ✅ | 2026-07-15. `SdlGpuRenderTargetCubeBackend` owns one `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers); only one face is ever the active target at a time (shared depth texture across faces, matching D3D11/D3D12's own convention), with per-face clear state. MSAA needed no manual resolve step at all (unlike D3D11/D3D12's `ResolveSubresource`) — `SDL_gpu` has no multisampled cube type, so the real render target is a plain 6-layer `2D_ARRAY` MSAA texture, and `SDL_GPUColorTargetInfo.resolve_texture`/`resolve_layer` resolves it directly into the cube texture's active face automatically at render-pass end. `SDL_GenerateMipmapsForGPUTexture` has no per-layer control (unlike D3D12's manual per-face blit), so it regenerates all 6 faces' chains whenever any face of a mip-enabled cube was used in a frame — harmless for untouched faces (same source data, same result). `multiSampleCount` is clamped via a new `SDL_GPUTextureSupportsSampleCount`-based `ClampSampleCount` helper (mirrors `D3D12RenderTargetCubeBackend::ClampMultiSampleCount`), unlike `RenderTarget2D` which still throws for any nonzero request. Required generalizing every queued draw command's `target` field from a single `RenderTarget2D` pointer to a small `DrawTarget{rt, cube, face}` struct, since a draw can now target the swapchain, a 2D RT, or one cube face. **Verified with real per-face GPU readback** (`SdlGpuRenderTargetCubeBackend::GetData`, pulled forward from `SDLGPU-39` — see that row) rather than `EnvironmentMapEffect` reflection sampling (not implemented in this backend, and Vulkan's own equivalent test documents that technique as unable to discriminate between individual faces anyway): `SdlGpu_RenderTargetCube`, 7/7 checks — all 6 faces filled with distinct colors and read back individually, a real colored3d triangle, real per-face depth-test occlusion, `MultiSampleCount` property fidelity + a genuine MSAA fill/resolve/readback round-trip, and mip level 1 of a uniform-color fill reading back correctly. |
| SDLGPU-37 | Multiple Render Targets (MRT) — `SDL_GPUGraphicsPipelineTargetInfo.color_target_descriptions` array + several `SDL_GPUColorTargetInfo` entries in one render pass. | ✅ | 2026-07-15. `SetRenderTargets(rts, count)` binds `rts[0]` exactly like `SetRenderTarget2D(rts[0])` (the real, single draw target); `rts[1..count-1]` are registered via a new `SdlGpuRenderTargetBackend::MarkUsedThisFrame()` (so each still gets its own real pass this frame) and tracked in a new `currentExtraMrtTargets_` list, but never become `currentRenderTarget_` — **draws remain single-target**, matching the exact same honest scope boundary this project's D3D11/D3D12 MRT support already established (no shader in this codebase declares more than one fragment output). `Clear()`/`ClearDepth()`/`ClearStencil()` propagate to every target in `currentExtraMrtTargets_` too, so a single `Clear()` call while MRT is bound really does clear all of them simultaneously, not just the primary. Verified via `SdlGpu_MRT` (3/3 checks: simultaneous MRT clear, a colored3d draw while MRT is bound, `ClearColorAndDepth`+`SetRenderTargets(nullptr,0)` restore, all with no exception) plus a real screenshot confirming both halves of the scope boundary at once — the primary target turned green (the draw succeeded) while the two secondary targets stayed exactly magenta (the shared `Clear()` reached them, and the draw did not). **Real, non-test-specific finding from writing this test:** a `RenderTarget2D`/`RenderTargetCube` that goes out of scope (destructor runs) before this backend's deferred `Present()`-time render pass actually executes is a real use-after-free — the destructor releases the real `SDL_GPUTexture*` immediately, but any sprite/draw commands already queued against it (not yet rendered) still hold that now-freed handle. Caught via a genuine segfault when this test's first draft used `Draw()`-local `RenderTarget2D` instances instead of members; `SdlGpu_RenderTargetCube`'s own test happened to avoid this by forcing an eager flush inside `GetData()` itself. **Fixed for real 2026-07-15 — see execution-order item 12** (a `shared_ptr`-owned GPU-state redesign, not just a deferred-release patch); `currentExtraMrtTargets_` above still has its own narrower, separate version of this risk (not converted, documented as out of scope in that same item). **Real MRT built 2026-07-15 (adversarial-review finding #2 — this row's own ✅ previously overclaimed "several `SDL_GPUColorTargetInfo` entries in one render pass" when only 1 was ever real):** `rts[0]`'s `SdlGpuRenderTarget2DState` gained `mrtSiblings` (populated by `SetRenderTargets(rts, count>1)`) and every other bound target gained `isMrtSibling=true`; `EnsureFrameRendered`'s per-target loop now skips `isMrtSibling` targets (no longer their own separate pass), and `RenderToTarget()` builds `1+mrtSiblings.size()` real `SDL_GPUColorTargetInfo` entries in ONE `SDL_BeginGPURenderPass` call — the task's own literal wording, now true. Stock (single fragment-output) draw families are unaffected and still only ever write attachment 0 (matching D3D11/D3D12's identical scope boundary — no stock shader in this codebase has more than one output); a genuinely new capability was added instead for the one kind of shader that CAN have more than one output: `SdlGpuEffectBackend::GetOrCreatePipeline()` gained a `colorTargetCount` param, building that many `color_target_descriptions` (all sharing this backend's one `R8G8B8A8_UNORM` `RenderTarget2D` format) and folding it into its own pipeline-cache key. **Real proof, not just "didn't throw"**: new `SdlGpu_MRT` Checks D/E — a single `SpriteBatch` draw with a real runtime-compiled 2-output GLSL fragment shader (`layout(location=0) out vec4`/`layout(location=1) out vec4`, the second a channel-swapped derivative of the first) while 2 fresh targets are bound writes two objectively different, independently-verified `RenderTarget2D::GetData()` values — proving one draw call genuinely reaches two simultaneous attachments. Git-stash-verified: temporarily forcing the render pass back to 1 attachment (and the custom pipeline back to `colorTargetCount=1`) reproduced exactly the predicted failure (target A still correct at attachment 0; target B read back untouched black, never written) — restoring the fix returned 5/5. Full `ctest -R "SdlGpu"` + `CnaTests`: 16/16 + 4385/4385, zero regressions. |
| SDLGPU-38 | MSAA — `SDL_GPUSampleCount` texture creation + `SDL_GPUColorTargetInfo.resolve_texture` automatic resolve-on-render-pass-end. | ✅ | 2026-07-15. Closes `RenderTarget2D`'s own MSAA leg (cube MSAA was already done as part of `SDLGPU-36`), using the exact same mechanism: a separate `msaaTexture_` (`SDL_GPU_TEXTURETYPE_2D`, `COLOR_TARGET`-only usage, real `sample_count`) is the actual render target; `SDL_GPUColorTargetInfo.resolve_texture = colorTexture_` + `store_op = SDL_GPU_STOREOP_RESOLVE` resolves it into the sampleable `colorTexture_` automatically at render-pass end — no manual resolve call. `multiSampleCount` is clamped via the `ClampSampleCount`/`SampleCountToInt` helpers `SDLGPU-36` added (`SDL_GPUTextureSupportsSampleCount`-based); MSAA and `mipMap` remain mutually exclusive on the same attachment (same rationale as the cube). The depth texture's `sample_count` is set to match the color attachment's when MSAA is requested. `CreateRenderTarget2D` no longer throws for any `multiSampleCount` value — the `SDLGPU-35`-era throw is gone. Verified via a new dedicated `SdlGpu_RenderTarget2DMSAA` (4/4 checks: `MultiSampleCount` property fidelity — request 4 applies a real device-clamped value >1; a real colored3d quad drawn into the MSAA target and resolved with no exception; a depth-tested MSAA target with its own MSAA depth texture; sustained multi-frame rendering) plus a real screenshot (a full-screen green quad — both the plain MSAA fill and the depth-tested MSAA target's nearer-quad-wins result render as solid green with no visible corruption or resolve artifacts). `sdlgpu_rendertarget2d_test.cpp`'s own former "MSAA throws" check (Check D) was updated in the same commit to a `MultiSampleCount==0` property-fidelity check instead, since the throw it asserted no longer applies. **Real gap found and fixed 2026-07-15 (adversarial-review finding #1):** despite the real MSAA texture creation above, every pipeline-creation function (`GetOrCreatePipelineColored3D`/`Textured3D`/`ColoredTextured3D`/`LitTextured3D`/`AlphaTest3D`/`DualTexture3D`/`EnvMap3D`/`Skinned3D`, the sprite pipeline, and the custom `ShaderEffect` pipeline) hardcoded `pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1` regardless of the real render pass's attachment sample count, and `PipelineCacheKey()` had no sample-count dimension at all — two draws into the same colorFormat/state but different MSAA levels would have shared one (wrongly-declared) pipeline. Fixed by threading a real `SDL_GPUSampleCount sampleCount` parameter through every `Render*Draws`/`RenderSprites`/`GetOrCreatePipeline*` function (sourced from a new `sampleCount` field on `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState`, set at texture-creation time; the swapchain path always passes `SDL_GPU_SAMPLECOUNT_1` since the swapchain is never MSAA), hashing it into `PipelineCacheKey()`, and folding it into `SdlGpuEffectBackend::pipelines_`'s own `int` key (`colorFormat << 4 | sampleCountInt`). **Honest verification note:** unlike this session's other pipeline-state bugs, a git-stash-style revert of just this fix did **not** reproduce any visible pixel-readback difference on this dev environment's Vulkan driver (`SdlGpu_RenderTarget2DMSAA`'s new Checks D/E, added specifically to try to catch this, still read back the correct resolved color with the fix reverted) — matching the adversarial review's own observation that `SDL_gpu`/this driver silently tolerates a pipeline/render-pass sample-count mismatch rather than hard-crashing or visibly corrupting output. This fix's correctness rests on matching `SDL_gpu`'s own documented pipeline/render-pass contract (verified by code inspection and zero regressions across the full 16/16 `SdlGpu` suite + 4385/4385 `CnaTests`), not on a reproduced visual failure — flagged here rather than silently claimed as empirically proven, since it wasn't. |
| SDLGPU-39 | `GetData()` readback — `SDL_DownloadFromGPUTexture` via a transfer buffer + fence wait (`SDL_SubmitGPUCommandBufferAndAcquireFence`/`SDL_WaitForGPUFences`), for `Texture2D`/`TextureCube`/`RenderTarget2D`/`RenderTargetCube`. | 🟨 | **`RenderTargetCube::GetData()` done for real, 2026-07-15** (pulled forward as part of `SDLGPU-36`): `SdlGpuRenderTargetCubeBackend::GetData()` downloads directly from the self-owned cube texture, exercised by all 7 `SdlGpu_RenderTargetCube` checks. **`RenderTarget2D::GetData()` also done for real, 2026-07-15** — this required a genuine cross-backend interface addition: `ITextureBackend` (in `IGraphicsBackend.hpp`, the common interface every backend implements) gained a new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default (mirrors `ITextureCubeBackend::GetData`'s own existing convention), and `Texture2D::GetData()`'s two entry-point overloads (the flat `(Color*, startIndex, elementCount)` form and the `(level, rect, Color*, startIndex, elementCount)` form) now fall back to `backend_->GetData(...)` **only** when the CPU-side `cpuPixels_`/mip shadow is empty (i.e. for a `RenderTarget2D`, whose content comes from GPU rendering, never `SetData()`) — plain, `SetData()`-populated `Texture2D` instances are completely unaffected (still served entirely from the CPU shadow, zero behavior change, confirmed via the full `Texture2DTest`/`TextureCubeTest`/`Texture3DTest` suites, 123/123 passing). `SdlGpuRenderTargetBackend::GetData()` downloads from `colorTexture_` (the always-single-sample, already-resolved-if-MSAA sampleable texture) using the identical transfer-buffer+fence pattern as the cube leg — safe from the swapchain segfault below since it's a self-owned texture, not the swapchain's. Verified: `sdlgpu_rendertarget2d_test.cpp` gained 3 new real pixel-assertion checks (Clear-only fill reads back exact green, a colored3d triangle reads back exact red, a depth-tested target's nearer-quad-wins reads back exact green) alongside its existing no-exception checks, 7/7 passing — genuinely stronger than the screenshot-only verification this row previously depended on. **Root cause of the swapchain-readback blocker found for real 2026-07-16 — this is a documented, permanent SDL_gpu API contract, not a driver bug or an environment limitation.** `SDL_gpu.h`'s own doc comment on `SDL_WaitAndAcquireGPUSwapchainTexture` (the exact function this backend already calls) states explicitly: *"The swapchain texture is write-only and cannot be used as a sampler or for another reading operation."* `SDL_DownloadFromGPUTexture`, `SDL_CopyGPUTextureToTexture` (source), and `SDL_BlitGPUTexture` (source) are all "reading operations" — so **none of them can ever read the swapchain texture, on any driver, by design**; the earlier segfaults were this contract being violated, not a fixable bug, and trying `SDL_BlitGPUTexture` as an alternative would hit the identical wall (confirmed via documentation before attempting, since the doc text is unambiguous and already matches the observed failure pattern exactly). Plain (non-render-target) `Texture2D::GetData()` needs no fix at all -- it already reads an always-accurate CPU shadow, so a GPU path there would be pure redundant scope. **`TextureCube::GetData()` (plain, non-render-target) also done for real, 2026-07-15** (as part of `SDLGPU-51`): `SdlGpuTextureCubeBackend::GetData()` downloads from the self-owned cube texture using the identical transfer-buffer+fence pattern as the `RenderTargetCube`/`RenderTarget2D`/`Texture3D` legs, indexed via `region.layer` for the face — exercised by all 5 `SdlGpu_TextureCube` checks. **The only real fix for `ReadBackbuffer`/`GetBackBufferData`**: stop rendering the swapchain pass directly into the acquired swapchain texture at all — render every frame into a self-owned proxy texture (`SAMPLER | COLOR_TARGET`, swapchain-sized, exactly like `RenderTarget2D` already is) instead, then one `SDL_BlitGPUTexture` (proxy → the real swapchain texture) right before submit for presentation; `ReadBackbuffer` would then read the proxy via the same transfer-buffer+fence mechanism already proven for `RenderTarget2D`/`RenderTargetCube`/`TextureCube`. **Deliberately not implemented 2026-07-16** — this is a genuine, permanent per-frame cost (one extra texture + one extra blit *every* frame regardless of whether the game ever calls `ReadBackbuffer`, since the proxy must be in place before the frame's draws happen, not allocated lazily on first read request), and the user chose to document this rather than pay that cost. Do not re-attempt `SDL_DownloadFromGPUTexture`/`SDL_CopyGPUTextureToTexture`/`SDL_BlitGPUTexture` directly against the swapchain texture — re-read this note first; the proxy-texture redesign above is the only viable path if this is revisited. |
| SDLGPU-53 | Depth-stencil render-pass compatibility warning found during `SDLGPU-6`'s `debug_mode` investigation (2026-07-16). | ⬜ | With `debug_mode` genuinely on for the first time, `SdlGpu_RenderTargetCube`'s depth-tested check (`SDLGPU-36`) triggers a real Vulkan validation warning: `VUID-vkCmdDraw-renderPass-02684` — "pSubpasses[0].pDepthStencilAttachment->attachment is incompatible between [render pass A] and [render pass B]... the first is `VK_ATTACHMENT_UNUSED` while the second is 1", i.e. a pipeline created assuming a depth attachment is bound in a render pass that (at that specific point) has none, or vice versa. Does **not** affect that check's own correctness readback — `SdlGpu_RenderTargetCube` still passes 7/7 with this warning present. Root cause not yet investigated; likely somewhere in how per-face pipeline creation/caching accounts (or doesn't) for whether a depth attachment is bound for that specific face's pass. Deliberately not investigated further in the same session that found it — reported for its own separate scoping/triage, per this plan's own established practice of not silently expanding an already-agreed task's scope. |

---

## Phase SDLGPU-9 — Texture3D and remaining texture surface

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-40 | `Texture3D` — `SDL_GPU_TEXTURETYPE_3D`, sub-volume upload/readback. | ✅ | 2026-07-15. `SdlGpuTexture3DBackend : ITexture3DBackend` — a single `SDL_GPU_TEXTURETYPE_3D` texture, `SAMPLER` usage only (never a render target, so no `EnsureFrameRendered()` flush needed before `GetData()`, unlike the render-target `GetData()` overrides — a plain texture's content only ever changes via its own immediate, synchronous `SetData()` uploads). `SetData`/`GetData` both carry the mip `level` straight through to `SDL_GPUTextureRegion.mip_level`/`region.z`/`region.d`, matching `Texture3D.cpp`'s own already-correct, unconditional `backend_->SetData/GetData(...)` dispatch (no XNA-layer change needed at all — `Texture3D::GetData()` already called through to the backend the same way `TextureCube::GetData()` does, confirming this codebase's established "plain non-render-target textures always ask the backend directly" convention). Wired into `CreateTexture3D()`, no longer the inherited `nullptr` default. **Real bug found and fixed via this task's own byte-exact round-trip test**: `SDL_UploadToGPUTexture`'s `cycle=true` (copied from `SdlGpuTextureBackend::UpdatePixels`'s own single-full-replace pattern) silently cycles the texture to a **fresh, separate underlying GPU resource** each time it's called on an already-"bound" texture — fine for a single full-texture replace, but `Texture3D` content is built up via multiple independent sub-volume/per-level `SetData()` calls that must all land on the SAME resource; with `cycle=true`, an earlier partial write (e.g. a sub-volume at Z=0) was silently orphaned onto an abandoned resource the moment a second write (Z=1) followed it, reading back as zero/uninitialized instead of its real content. Fixed by passing `cycle=false` for `Texture3D` uploads specifically (`SdlGpuTextureBackend::UpdatePixels`'s own single-full-replace `cycle=true` is unaffected and correct as-is). **Real proof** (matches the byte-exact bar `D3D12Texture3DBackend`/`DX-122` already met): `SdlGpu_Texture3D`, 6/6 checks — a deliberately off-center 2×2×2 sub-volume within a 4×4×2 texture, with a *different* solid color per Z slice, round-trips byte-exact (this is the exact check that caught the `cycle` bug above); a full-volume round-trip; and a check that level 0 remains intact after a later, separate level-1 `SetData()` call (genuinely proves cumulative writes, not just "the last write is readable"). |
| SDLGPU-41 | Mipmaps — allocate the FNA-compatible authored chain and address each level explicitly through `SDL_GPUTextureRegion.mip_level`. | ✅ | Corrected by `REMED-GFX-099` on 2026-07-25. The original 2026-07-15 implementation correctly supported authored per-level data, but also invented a plain-Texture3D automatic-generation behavior: full level-0 `SetData` called `SDL_GenerateMipmapsForGPUTexture` and widened the texture to `SAMPLER | COLOR_TARGET`. FNA and CNA's Vulkan/EasyGL/WebGPU/Bgfx backends allocate authored levels and never make that implicit call. The extension exposed invalid SDL Vulkan 3D views/blits and its uniform test could not prove depth handling. Automatic generation is removed; Texture3D is `SAMPLER`-only and every mip is populated explicitly. `SdlGpu_Texture3D` is now 30/30 across level 0/nonzero mips, repeated/order/state cycles, distinct depth planes, partial volume, NPOT, and two resources, with zero validation. |
| SDLGPU-54 | `Texture3D` generated-mipmap depth-dimension correctness/validity gap found during `SDLGPU-6`'s `debug_mode` investigation. | ✅ | **Closed by `REMED-GFX-099` on 2026-07-25.** The exact prefix was 32 errors: 12 image-view messages and 20 blit messages. For an 8×8×2 four-level 3D image, SDL's Vulkan backend created `VK_IMAGE_VIEW_TYPE_2D` target views for every base depth plane at every mip, so plane 1 was out of range at mips 1–3; its generator likewise blitted base plane 1 through every shrunk mip. A create-only native SDL reproducer proved the first bad event occurs during texture creation. The installed SDL 3.5.0 API inputs were valid and a newer local `origin/main` retains both loops, but CNA should never have selected this non-FNA path. Restoring authored-only mips removes `COLOR_TARGET`, all target views, and all generator blits. Final dedicated 31/31 checks and full SDL_GPU 36/36 are globally validation-clean. |
| SDLGPU-51 | Plain, non-render-target `TextureCube` — `CreateTextureCube()` currently returns `IGraphicsBackend`'s default `nullptr`; needs a real `SdlGpuTextureCubeBackend : ITextureCubeBackend` (upload via `SDL_GPU_TEXTURETYPE_CUBE` + `SDL_GPUCubeMapFace`-indexed per-face `SetData`/`GetData`), analogous to `SdlGpuTextureBackend` for plain `Texture2D`. | ✅ | 2026-07-15. `SdlGpuTextureCubeBackend : ITextureCubeBackend` — a single `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers), `SAMPLER` usage only (never a render target). Each face maps to one array layer; `SetData`/`GetData` carry `face` straight through to `SDL_GPUTextureRegion.layer` (the same convention `SdlGpuRenderTargetCubeBackend::GetData` already established) and `level` through to `mip_level`, matching `Texture3D`'s pattern. `TextureCube.cpp`'s XNA layer was already correctly wired to call `backend_->SetData/GetData(...)` unconditionally, so no XNA-layer change was needed, only the backend class itself. Wired into `CreateTextureCube()`, no longer the inherited `nullptr`. **Uploads use `cycle=false` from the start** — the exact bug `SDLGPU-40` found and fixed for `Texture3D` (`SDL_UploadToGPUTexture`'s `cycle=true` silently orphaning earlier writes onto an abandoned GPU resource) applies equally here, since a real cube map is built up via multiple independent per-face `SetData` calls that must all land on the same resource; this was checked for and confirmed absent via a deliberate regression check (all 6 faces written with distinct colors, then face 0 re-read *last*, after the other 5 writes). The original implementation regenerated the whole 6-face mip chain after one face's level-0 upload because SDL exposes no per-layer generator; **SDLGPU-70 supersedes that historical choice** after the EasyGL oracle proved it overwrote mip levels already authored on other faces. Plain TextureCube mips are now explicitly authored, matching XNA/EasyGL, and the five-check regression was revised to distinguish cross-face preservation. This also fully unblocks `SDLGPU-33` (`EnvironmentMapEffect`) on the texture-source side — see that row. |

---

## Phase SDLGPU-10 — Custom ShaderEffect (`IEffectBackend`)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-42 | **Decision task.** `SDL_gpu` only accepts precompiled bytecode, so an arbitrary user-authored `ShaderEffect` needs either (a) a runtime GLSL→SPIR-V compile via `libshaderc` for the Vulkan-driver case only, or (b) deferring custom-`ShaderEffect` support on this backend until `SDLGPU-13`'s `SDL_shadercross` alternative (if chosen) is vendored. Document the choice and its exact platform scope — do not silently support it on Linux only while implying full parity with `D3D11`'s runtime `D3DCompile()` path. | ✅ | **Decided 2026-07-15 (user choice): (a), real runtime `libshaderc` compile — Linux/Vulkan-driver scope only, same as this backend's entire current scope (no Windows/macOS driver work has started).** A real architectural finding changed the framing of this decision before it was made: `VulkanGraphicsBackend`'s own `VulkanEffectBackend::CompileProgram` does **not** runtime-compile GLSL text at all — despite `ShaderEffect`'s own documented contract ("Contents of the GLSL vertex shader source, not a file path"), Vulkan's implementation expects the caller to already hand it precompiled SPIR-V bytes (its own doc comment literally says "`vertSpv` and `fragSpv` contain raw SPIR-V bytecode"), and `libshaderc` is used only by this project's own build-time `compile_shaders.py` scripts, never at runtime in any backend's actual C++ code before this task. `EasyGLEffectBackend`, by contrast, genuinely compiles raw GLSL text at runtime since the OpenGL driver does that natively. So the real choice was between mirroring Vulkan's own (narrower, arguably contract-violating) precompiled-bytes scope, or building the first-ever backend in this project whose `ShaderEffect` support matches the class's own documented contract for real. User chose the latter. This environment has no `libshaderc-dev` package (no unversioned `.so` symlink, no headers) — `CMakeLists.txt`'s `SDL_GPU` branch does a `find_library()` first (works if a dev package exists elsewhere/CI), falling back to globbing standard multiarch library directories for the runtime-only versioned `libshaderc.so.1`, linked by its exact path (mirrors how this project's own `.sdl-prebuilt-*` SDL3 libraries are already linked by full versioned path elsewhere in this file). Confirmed via `ldd` on a real built test binary that `libshaderc.so.1` is a genuine, resolved runtime dependency, not merely compiled-against. |
| SDLGPU-43 | Implement `SdlGpuEffectBackend::CompileProgram`/`Bind`/`Unbind`/`IsValid`/`GetCompileError` per the `SDLGPU-42` decision. | ✅ | 2026-07-15. `SdlGpuEffectBackend : IEffectBackend`. `CompileProgram(vertSrc, fragSrc)` hand-declares the small `extern "C"` shaderc-C-API subset this project's own `compile_shaders.py` (ctypes) already proves correct against the identical shared library — no `shaderc.h` exists in this environment, so these prototypes are declared locally rather than pulled from a header (opaque handles all `void*`, matching `ctypes.c_void_p`). Compiles both stages independently to SPIR-V, then `SDL_CreateGPUShader`s each; failure at any step populates `GetCompileError()` and leaves `IsValid()` false — matches `VulkanGraphicsBackend`/`D3D11GraphicsBackend`'s own "report via the backend object, don't throw" convention. `Bind()`/`Unbind()` are real no-ops (documented why): this backend defers ALL actual GPU state changes to `Present()` time, so there is nothing for an immediate `Bind()` call to usefully do — the real "bind" is `RenderSprites()` selecting this object's own compiled pipeline over the stock sprite one, driven by a per-`SpriteCommand` pointer set at `Draw()`-call time (see below), not by a live "current effect" side-channel like `VulkanGraphicsBackend::activeCustomEffect_` uses. Fixed vertex contract (`SpriteVertex`-shaped, 32 bytes) and fixed 128-byte uniform layout mirror `D3D11EffectBackend`'s own byte-for-byte convention exactly (`[0..15]`=vpSize, `[16..79]`=matrix, `[80..95]`=color, `[96..99]`=float/int slot 0); per `SDL_CreateGPUShader`'s own binding-convention doc comment, the custom GLSL must declare its uniform block at vertex `set=1`/`binding=0` and fragment `set=3`/`binding=0`, and its one sampler at fragment `set=2`/`binding=0` — documented in the class's own header comment, not silently left implicit. `ISpriteBatchBackend::SetCustomEffect` no longer throws for a non-null effect (previously an honest "not implemented yet" stub) — stores the `Effect*`; `SdlGpuSpriteBatchBackend::Draw()` resolves `effect->GetEffectBackendPtr()` (the same clean accessor `D3D11SpriteBatchBackend` already uses, simpler than Vulkan's own side-channel) via `dynamic_cast<SdlGpuEffectBackend*>` and **snapshots its current 128-byte uniform state into the `SpriteCommand` right there at Draw()-call time** — not at `Present()` time, since a game may reasonably change uniforms between individual `Draw()` calls within one `Begin`/`End` cycle using the same effect object, and this backend's rendering is deferred until `Present()`, potentially long after those later `SetUniform*` calls would otherwise retroactively (and wrongly) apply to an already-queued sprite. `RenderSprites()` now checks each queued sprite's own `customEffect` pointer and binds that object's own cached pipeline (compiled per-`colorFormat` on first use, mirroring every other per-format pipeline cache in this backend) instead of the stock sprite pipeline, re-stamping only the render-time-known `vpSize` into the snapshot before pushing. Verified via a new `SdlGpu_ShaderEffect`, 3/3 checks, through the real public API (`ShaderEffect` → `SpriteBatch.Begin(effect)` → `Draw()` → `End()`, `RenderTarget2D::GetData()` readback since this backend's swapchain-download path segfaults, `SDLGPU-39`): `IsEffectValid()` true after a real runtime compile; a white 1×1 texture drawn through the custom shader with `SetUniformVec4` reads back the *exact* tint color (byte-exact, no blend artifacts — proves the compiled GLSL's own uniform is genuinely read, not the stock vertex-color path); the identical draw with `SetCustomEffect(nullptr)` reads back plain white, not the tint (the real discriminator that Check B's result came from genuine custom-shader binding, not a no-op that always "wins" regardless). Full `ctest -R "SdlGpu"` re-run: **13/13 passing**, zero regressions; the default `EASYGL`-backend debug build was also rebuilt to confirm zero cross-backend impact (all changes were SDL_GPU-backend-local). This closes Phase `SDLGPU-10` and every row currently in `plan_sdlgpu.md`. |

---

## Phase SDLGPU-11 — Known permanent limitation: occlusion queries

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-44 | Confirm and document that `SDL_gpu` has no occlusion-query API in the vendored SDL3 (`third_party/SDL/include/SDL3/SDL_gpu.h`, checked 2026-07-15 — no query-pool type exists). `CreateOcclusionQuery()` returns `nullptr`, matching `IGraphicsBackend`'s own documented default and the posture `Headless`/`Software` already take. | ⬜ | Not a gap to close by writing code — only re-check this row if a future SDL3 upgrade adds real query support. |

---

## Phase SDLGPU-12 — Testing and CI

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-45 | `SdlGpu_Smoke` CTest target (mirroring `D3D11_Smoke`/`D3D12_Smoke`/`WebGPU_Native2D_Smoke`), reporting SKIPPED with a clear reason when no `DISPLAY`/`WAYLAND_DISPLAY` is present. | ✅ | Fixed 2026-07-15 (adversarial-review finding). All 16 `sdlgpu_*.cpp` test/example files ran their real test logic unconditionally — none of them implemented this project's own established `SKIP_RETURN_CODE 77` convention (Task 470), so `ctest -R SdlGpu` reported hard FAILED, not SKIPPED, on any machine without a real X11/Wayland display, unlike every other backend's tests. Fixed by adding a shared `CNA::Examples::ProbeGpuDisplayAvailable()` free function to `examples/common/PixelTestGame.hpp` (same `SDL_InitSubSystem(SDL_INIT_VIDEO)`/`SDL_QuitSubSystem` probe `RunPixelTest<TGame>()` already uses internally, exposed standalone for these hand-rolled multi-frame `Game` subclasses that don't fit `PixelTestGame`'s single-shot shape), and calling it at the top of `main()` in all 17 `sdlgpu_*.cpp` files, returning `kSkipExitCode` (77) before constructing the real `Game` if no display is available. Verified both directions: `ctest -R "SdlGpu"` still 16/16 PASS with a real display; running a test binary directly with `DISPLAY`/`WAYLAND_DISPLAY` unset and an invalid `SDL_VIDEODRIVER` prints `[SKIP] ...` and exits 77 as expected. |
| SDLGPU-46 | One CTest binary per capability, matching this codebase's existing per-backend test structure: `SdlGpu_Colored3D`, `SdlGpu_Textured3D`, `SdlGpu_ColoredTextured3D`, `SdlGpu_LitTextured3D`, `SdlGpu_AlphaTest3D`, `SdlGpu_DualTexture3D`, `SdlGpu_EnvMap`, `SdlGpu_Skinned`, `SdlGpu_RenderTarget2D`, `SdlGpu_RenderTargetCube`, `SdlGpu_MRT`, `SdlGpu_MSAA`, `SdlGpu_Texture3D`. | ⬜ | Every public method/operator/constant this backend newly exposes still needs its own test per `CLAUDE.md`'s testing rules — this row is the backend-specific CTest scaffolding, not a substitute for per-class unit tests. |
| SDLGPU-47 | Cross-backend diagnostic — compare SDL GPU output against an existing verified backend (Vulkan or Software) pixel-for-pixel for at least one shared scene. | ⬜ | `CNA_GRAPHICS_BACKEND` is a compile-time choice, so this needs two separate builds compared offline, exactly as documented in `CMakeLists.txt` (~L6799) for the existing Software-backend cross-backend diagnostic (Phase S9). |

---

## Phase SDLGPU-13 — Platform expansion (deferred until the Linux/Vulkan-driver path is fully verified)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-48 | Windows validation via `SDL_gpu`'s D3D12 driver. | ⬜ | Code paths only until run on real (or Wine/DXVK-class) Windows infrastructure, same caveat structure already used for `D3D11`/`D3D12`/WebGPU. |
| SDLGPU-49 | macOS/iOS validation via `SDL_gpu`'s Metal driver. | ⬜ | `needs_human` — no Apple hardware in this dev environment, same class of gate as `DX-90`/`DX-91`/`DX-114`. |
| SDLGPU-50 | Android validation via `SDL_gpu`'s Vulkan driver. | ⬜ | The Android prebuilt SDL3 package already exists (`.sdl-prebuilt-Android-aarch64/`) and already compiles `SDL_gpu.c` — check whether its Vulkan GPU driver was also compiled in before assuming this is free. |

---

## Closing notes

- Follow `CHECKLIST.md`'s per-file checklist for every new `.hpp`/`.cpp` pair this plan produces
  (SPDX header, Doxygen on every public member, `GetTypeName()` override, etc.) — this plan file
  only tracks *what* to build, not the per-file compliance checklist, which is authoritative on
  its own.
- One task = one commit, per `CLAUDE.md`'s git-commit rules — do not bundle multiple `SDLGPU-`
  rows into a single commit, and update this file's status column in the same commit that closes
  a task.
- Update the phase-level summary at the top of this file once the Phase `SDLGPU-5` 2D vertical
  slice is verified, the same way `plan_webgpu.md`'s header was rewritten once its own 2D
  baseline landed — do not let the header go stale relative to the task table below it.
