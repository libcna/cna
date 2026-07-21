# Metal graphics backend

`METAL` is CNA's native Apple graphics backend (macOS/iOS/tvOS), selected via
`-DCNA_GRAPHICS_BACKEND=METAL`. It fronts `id<MTLDevice>`/`id<MTLCommandQueue>`/`CAMetalLayer`
directly through Objective-C++ (`.mm`) source files — no intermediate abstraction library (unlike
`BGFX`) and no cross-platform shader translation (unlike `WEBGPU`'s WGSL or `VULKAN`'s SPIR-V):
shaders are Metal Shading Language (MSL), compiled at runtime from an embedded C++ raw string via
`MTLDevice::newLibraryWithSource:`.

Full task-by-task history, every phase's design rationale, and the authoritative "what's actually
left" summary live in `plan_metal.md` (`METAL-1`–`METAL-257`, 30 phases). This document is the
durable capability-boundary reference `CLAUDE.md`'s `docs/webgpu-backend.md` precedent points to —
kept current at the end of each session, not a full narrative retelling.

## Current verification status (updated 2026-07-21)

Every development *session* on this backend still runs on Linux (Debian 13), which has no Apple
Clang, no Metal framework headers, and no Metal-capable GPU — every line of `.mm` code is still
written and self-reviewed without ever compiling it locally. But since `.github/workflows/
metal-macos-ci.yml` started actually building this backend on a real `macos-14` GitHub Actions
runner, that is no longer the whole story:

- **This backend genuinely compiles cleanly on real Apple Clang.** Confirmed across many separate
  CI runs (e.g. `29800865929`, `29801955085`, `29804175429`, `29804647754`), including every
  Objective-C++ change landed to date — resource-lifetime fixes (`METAL-256`), enum-table
  extractions (`METAL-19`), and the full custom-`ShaderEffect`/MSL facility (Phase 14). "Never
  compiled" is no longer an accurate description of this backend as a whole.
- **Most of its `ctest` suite passes on real Apple hardware.** As of the CI run above: `132` total
  Metal-labeled tests, `128` passing (`97%`).
- **A specific, confirmed, unresolved bug accounts for every current failure.** `Metal_PbrEffect_
  Golden`/`Metal_SkinnedPbrEffect_Golden`/`Metal_DrawUserPrimitives_VPC`/`Metal_SpriteBatch_
  CustomEffect` all fail for the *same* underlying reason: `GraphicsDevice::GetBackBufferData()`
  (via `ReadBackbuffer()`) reads back only the `Clear()` color, never the content of a real draw
  that ran before it — confirmed (not just suspected) by printing the actual observed pixel values,
  which are byte-for-byte each test's own `Clear` color. This reproduces for both 2D (`SpriteBatch`)
  and 3D draws, ruling out `PipelineKind`/shader-specific causes, and is **not** attributable to
  either of two real, already-found-and-fixed bugs from the same investigation (a `vertexStart`
  offset bug, a premature-`presentDrawable:` bug). Root cause remains undetermined — the
  investigation is paused, not abandoned: pending either a physical Mac (for `Xcode`/`MTLCaptureManager`-
  based GPU debugging — attempted once via CI, the specific `macos-14` runner's own GPU does not
  support `MTLCaptureDestinationGPUTraceDocument`) or a `MTLCommandBuffer` runtime-error check
  (already added, came back clean — rules out a GPU-side execution error too). See `plan_metal.md`
  narrative items 67–76/82/84/85 for the full investigation history.

Every "landed" phase below was written by careful, line-by-line comparison against
`EasyGLGraphicsBackend`'s already-shipping, already-tested equivalent logic (CNA's most mature 3D
backend) before ever reaching a compiler — but treat the distinction below (🟨 vs. ✅) as the
authoritative word on what's actually been machine-checked, not the prose.

## Status legend

- ✅ — **real, machine-verified**: actually compiled and executed by a real test binary on some
  real machine (Linux for the plain-C++ subset, real Apple hardware via CI for everything else),
  with the specific `ctest`/CI evidence cited. Never claimed on inspection alone.
- 🟨 — source-complete: written and reviewed line-by-line against the FNA/EasyGL reference. Split
  into two sub-cases, not distinguished further below (check `plan_metal.md`'s own per-task table
  for the specific CI run backing any given 🟨 item): (a) genuinely CI-confirmed to *compile and
  pass its own test* on real Apple hardware, just not yet promoted to ✅ in this summary doc, or
  (b) written but not yet pushed/exercised by any CI run at all. Never treat a 🟨 row here as
  functionally verified correct beyond what its own cited evidence says.
- ⬜ — not started.

One exception predates real CI entirely: 15 pieces of this backend's logic touch *no* Objective-C or
Metal-framework types at all (they only read/write plain C++ structs, XNA framework types, and
`float` arrays) — see "Real, machine-verified subset" below. Those pieces carry a genuine ✅ from a
plain Linux build, no Apple hardware required.

## Implemented baseline (🟨 unless noted otherwise)

- Compile-time backend selection (`CNA_GRAPHICS_BACKEND=METAL`, `CNA_BACKEND_METAL`), Apple-only
  CMake hard gate, Objective-C++ enablement only when selected.
- Native device/window/swapchain bring-up: `MTLDevice`, `MTLCommandQueue`, `CAMetalLayer`,
  drawable acquisition and presentation, BGRA8 swapchain, native depth32+stencil8 attachment,
  color/depth/stencil clear combinations (Phase 1).
- Pipeline-state cache keyed by a `PipelineKind` enum (one entry per concrete shader+vertex-layout
  combination this backend emits — a fixed-variant simplification of the fully generic
  `VertexElement`-driven descriptor builder `METAL-26`/`27` still describe and leave open) (Phase 2).
- `BasicEffect` full shader parity: colored/textured/lit 3D draw paths, per-pixel *and* per-vertex
  (Gouraud) lighting selected by the real XNA `PreferPerPixelLighting` default, fog, specular,
  emissive (Phase 3).
- `AlphaTestEffect`/`DualTextureEffect` (Phase 4/5).
- `EnvironmentMapEffect`: world-space cube-map reflection, flat + Fresnel-weighted blend, lit+fogged,
  built on real `TextureCube`/`Texture3D` backends (Phase 6, 11).
- `SkinnedEffect`: 72-bone GPU skinning via a real `MTLBuffer`, `WeightsPerVertex` branching, a
  NaN-safety guard for near-180°-relative-bone-rotation blends, per-pixel and per-vertex lit
  variants, lit/fog/specular/emissive (Phase 7).
- `PbrEffect`/`SkinnedPbrEffect` (NOXNA): glTF 2.0 metallic-roughness Cook-Torrance BRDF,
  tangent-space normal mapping, 4 optional PBR maps with safe default-texture fallbacks (Phase 8).
  Cross-backend PBR support/verification status lives in `plan_cnj.md`'s `CNB-103`–`111` table
  (one row per backend), not duplicated here.
- Custom `ShaderEffect`/MSL contract (Phase 14) — a `SpriteBatch`-scoped facility (fixed 32-byte
  `Sprite2DVertex`-shaped contract, matching the exact scope `VulkanEffectBackend`/
  `D3D11EffectBackend`/`D3D12EffectBackend` already commit to, not the arbitrary-3D-vertex-layout
  facility only `EasyGLGraphicsBackend` supports): runtime-compiled MSL vertex+fragment pair via
  two separate `MTLLibrary` objects, fixed-slot uniform contract (`docs/metal-shader-effect-
  contract.md`), real per-`BlendState` pipeline selection (an improvement over the Vulkan/D3D11/
  D3D12 precedent's own hardcoded blend). `SupportsCapability(CustomEffects)` is real (`true`).
- Instancing (Phase 9) — still blocked, on the general 3D `GpuDrawParams::customEffectBackend`
  bypass (`METAL-148`) and the generic `VertexElement`-driven descriptor builder specifically, not
  on Phase 14 as a whole (Phase 14's own landed scope is `SpriteBatch`-only and does not touch
  this). No established structured-pipeline backend (Vulkan/D3D11/D3D12) wires `customEffectBackend`
  into its general 3D draw path either — only `EasyGLGraphicsBackend` does, since GL's attribute
  binding needs no rigid vertex descriptor.
- `RenderTarget2D`/`RenderTargetCube` bind/unbind, sampleable afterward, `DiscardContents`/
  `PreserveContents`, mip regeneration on unbind, `GetColorGLHandle`-equivalent accessor (Phase 10;
  MRT and MSAA remain ⬜, see Known limitations).
- GPU readback (`ReadBackbuffer`, `RenderTarget2D`/`Cube`/`Texture3D`/`TextureCube::GetData()` via a
  shared `blitTextureToClientBuffer` helper) and real occlusion queries
  (`MTLVisibilityResultBuffer`, genuine `uint64_t` pixel counts — a real capability advantage over
  EasyGL's GLES3 boolean) (Phase 12/13).
- Letterbox/overscan/stretch/native/fixed-height-dynamic-width logical-viewport transform
  (Phase 15) — the CPU-side formula is in the real ✅ subset below.
- Resize/HiDPI research, frame pacing/`swapInterval`, resource-lifetime/command-buffer audit,
  `SupportsCapability` accuracy review (Phases 16–18, 20).
- `SpriteBatch` parity: transform matrix, real blend state (Phase 19).
- iOS/tvOS buildability audited from source (no macOS-only API spotted; SDL3's own generic
  `uikit` Metal driver already covers it) — likely true, not just aspirational, but genuinely
  unverified until a real build-only CI job exists (Phase 29, partial).

## Real, machine-verified subset (✅, verified on Linux — see caveat on scope)

15 pieces of this backend's logic are plain C++ with zero Objective-C/Metal-framework
dependency — extracted out of `MetalGraphicsBackend.mm` into standalone headers under
`include/CNA/Internal/Backends/Metal/`, with `MetalGraphicsBackend.mm` reduced to thin same-name
aliases/wrappers (or, for the enum-mapping headers, a trivial 1:1 final switch onto the real
`MTL*` enum) so every call site is unaffected. Each has a dedicated GoogleTest suite under
`tests/CNA/Internal/Backends/Metal/` that carries **no** `#if defined(CNA_BACKEND_METAL)` gate
(deliberately — unlike every other backend's own tests, these must run under whichever backend
this machine can actually build, e.g. `HEADLESS`). All 127 tests pass under a real
`-DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON` `CnaTests` build and `ctest -R "^Metal"` run
on Linux (`ctest --test-dir cmake-build-headless -R "^Metal" -N` reports `Total Tests: 127`); an
independent adversarial audit separately re-derived every expected value against the real source
structs/functions for the first 7 and found no functional defects (see `plan_metal.md`'s narrative
items 29–35 for that audit's full detail).

| Header | What it covers |
|---|---|
| `MetalPipelineKey.hpp` | `MetalPipelineKind` enum, `MetalBlendKey`, `MetalPipelineCacheKey`/Hash |
| `MetalNormalMatrix.hpp` | `ComputeMetalNormalMatrixCols` — `transpose(inverse(world3x3))` shortcut |
| `MetalPrimitiveVertexCount.hpp` | `ComputeMetalPrimitiveVertexCount` — `PrimitiveType` → vertex count |
| `MetalLogicalViewport.hpp` | `ComputeMetalLogicalViewport` — letterbox/overscan/etc. formula |
| `MetalMat4.hpp` | `MetalMat4Multiply`/`FromXna`/`Transpose` — the WVP matrix helper set |
| `MetalSelectPipelineKind.hpp` | `SelectMetalPipelineKind` — the shader-variant dispatch decision |
| `MetalUniformFill.hpp` | `FillMetal{Lit,Env,Skinned,Pbr,SkinnedPbr}Uniforms` — `GpuDrawParams` → GPU uniform mapping |
| `MetalSamplerFilter.hpp` | `DescribeMetalSamplerFilter` — `TextureFilter` → min/mag/mip-is-point plan |
| `MetalVertexAttribFormat.hpp` | `MetalVertexAttribKind` — all 12 `VertexElementFormat` values, neutral (non-`MTL`) form |
| `MetalVertexDescriptorPlan.hpp` | `BuildMetalVertexDescriptorPlan` — arbitrary-`VertexElement`-list attribute-layout building (`METAL-26`/`27`'s core logic; not yet wired into any live draw path, see Known limitations) |
| `MetalCompareFunction.hpp` | `DescribeMetalCompareFunction` — `CompareFunction` → `MetalCompareFunctionKind` |
| `MetalStencilOperation.hpp` | `DescribeMetalStencilOperation` — `StencilOperation` → `MetalStencilOperationKind` |
| `MetalBlend.hpp` | `DescribeMetalBlendFactor` — `Blend` → `MetalBlendFactorKind` |
| `MetalBlendFunction.hpp` | `DescribeMetalBlendOperation` — `BlendFunction` → `MetalBlendOperationKind` |
| `MetalCullMode.hpp` | `DescribeMetalCullMode` — `CullMode` → `MetalCullModeKind` |

The last 5 rows (`METAL-19`) switch on the real XNA enumerator name rather than a raw `int`
literal, so a future reordering of any of those 5 XNA enums' declarations is compile-time-
irrelevant here — the only part that still needs the Apple SDK is each header's own thin `.mm`-side
final translation to the matching `MTL*` enum (a trivial 1:1 name match, CI-confirmed to compile
correctly, see `plan_metal.md` narrative items 81/82).

This subset does not include anything that constructs or issues real Metal API calls
(`vertexDescriptorForStride`, `makePipeline`, the `metalPrimitive`/etc. final enum-translation
switches, all real command-encoder work) — those return or consume genuine `MTL*` types declared
only in Apple's Metal framework headers and cannot be extracted the same way without either the
framework itself or hardcoding Apple's numeric enum values as an out-of-pattern risk. That
remaining surface stays 🟨 (or ✅ per-item once its own CI evidence is checked, see the status
legend above) pending real hardware for anything not yet exercised by a passing CI run.

## Known limitations / explicitly open

- **The confirmed readback bug** — see "Current verification status" above. The single largest
  known issue: any real draw (2D `SpriteBatch` or 3D) followed by a same-process
  `GetBackBufferData()`/`ReadBackbuffer()` call reads back only the `Clear()` color. Affects 4
  `CTest`s today (`Metal_PbrEffect_Golden`/`Metal_SkinnedPbrEffect_Golden`/`Metal_
  DrawUserPrimitives_VPC`/`Metal_SpriteBatch_CustomEffect`) and will affect any *future* golden-
  image/readback-based `CTest` too, until resolved. Root cause undetermined; investigation paused
  pending a physical Mac (see `plan_metal.md` narrative items 67–76/82/84/85 for what has already
  been ruled out).
- **Fully generic `VertexElement`-driven descriptor builder** (`METAL-26`/`27`) — the current
  pipeline cache uses a fixed `PipelineKind` enum (one entry per concrete shader+layout this
  backend actually emits) rather than a hashed arbitrary-`VertexElement`-list key. Lower risk to
  get right without a compiler than inventing the generic version blind; blocks the general 3D
  `GpuDrawParams::customEffectBackend` bypass (`METAL-148`) and, transitively, Phase 9 (Instancing)
  — no longer blocks Phase 14 as a whole, whose own landed scope (`SpriteBatch`-only custom
  effects) needed no dependency on this builder (see Phase 14's bullet above).
- **MRT / MSAA** (`METAL-104`/`105`/`112`/`113`) — the rest of Phase 10.
- **`METAL-147`** — `IEffectBackend::BindTexture`/`BindTextureCube`/`BindTexture3D` for *extra*
  sampler units on a custom `ShaderEffect` are not implemented (inherit the no-op default), matching
  `VulkanEffectBackend`/`D3D11EffectBackend`'s own identical scope boundary — texture unit 0 is
  always driven by the caller (`SpriteBatch`'s own texture parameter).
- **`METAL-257`** — a cross-backend missing-`SDL_WINDOW_HIGH_PIXEL_DENSITY` gap Phase 16's research
  found; deliberately left for a cross-backend task since it isn't Metal-specific (confirmed to
  also affect iOS/tvOS, not just macOS).
- **Phases 21–24** (argument buffers/bindless, indirect command buffers, MetalFX upscaling, GPU
  counters/Xcode frame capture) — all NOXNA extensions requiring real hardware/Xcode to design
  meaningfully, not attempted.
- **Phase 28** (cross-backend pixel parity) and the rest of Phase 29 (a real iOS/tvOS build-only CI
  job and everything downstream of it) — ⬜.
- **Every 🟨 row above not otherwise called out with its own CI evidence** — see "Current
  verification status": source-complete, and possibly already CI-compiled (check `plan_metal.md`'s
  own per-task table for the specific run), but not yet promoted to ✅ in this summary doc.

## Verification methodology

Two genuinely different tiers of evidence exist for this backend, and neither should be
represented as the other:

1. **Real ✅ on Linux (the extracted plain-C++ subset only)**:
   ```
   cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON
   cmake --build cmake-build-debug --target CnaTests -j4
   cd cmake-build-debug && ctest -R "^Metal" --output-on-failure
   ```
   (Anchored `-R "^Metal"`, not a bare `Metal` substring match — an unanchored filter also matches
   6 unrelated `PbrEffectDefaultsTest.Metallic*`/`SkinnedPbrEffectDefaultsTest.Metallic*` tests.)

2. **Real ✅ on macOS (per-item, once each specific item's own evidence is checked)**:
   `.github/workflows/metal-macos-ci.yml` on a `macos-14` GitHub Actions runner — configures with
   `-DCNA_GRAPHICS_BACKEND=METAL -DCNA_BUILD_TESTS=ON`, builds, then runs `ctest -R "^Metal"
   --output-on-failure`, which covers `Metal_Smoke` (the real end-to-end device/window/swapchain/
   draw smoke test), the 132-and-growing full Metal `CTest` suite (SpriteBatch, custom effects,
   golden-image, readback, occlusion queries, etc.), and the tier-1 extraction tests again (this
   time on real Apple Clang, not just Linux). **This tier is real and has been observed passing for
   most of the suite on multiple separate runs** — do not read this doc's own age as evidence
   nothing has changed; check `plan_metal.md`'s narrative for the specific run ID behind any given
   claim. The one standing exception is the confirmed readback bug in "Current verification status"
   above, which currently keeps the overall `ctest` exit code nonzero (`Errors while running CTest`)
   even though the great majority of individual tests pass — a job showing `conclusion: failure`
   in `gh run list` does **not** by itself mean nothing works; always check the actual `ctest`
   pass/fail line, not just the job's aggregate GitHub Actions conclusion.

**Runtime validation** (`METAL-218`/`229`): `MTL_SHADER_VALIDATION=1` and `MTL_DEBUG_LAYER=1` are
Apple's own documented runtime validation environment variables — the Metal-side equivalent of
Vulkan's `VK_LAYER_KHRONOS_validation` already used elsewhere in this project. They surface real
API misuse (out-of-bounds shader access, invalid resource binding, mismatched attachment formats,
etc.) as a clear diagnostic instead of silent undefined behavior or a hard-to-diagnose GPU hang.
Set for the `metal-macos-ci.yml` job's test-running step (read at runtime by whatever process
creates the `MTLDevice`, not needed at build time). Anyone reproducing a CI failure locally on a
real Mac should set both before running `Metal_Smoke`/`CnaTests` by hand, for the same reason.

## Command-buffer / encoder lifecycle model (`METAL-181`)

One `id<MTLCommandQueue>` for the backend's lifetime. Within a single logical frame, zero or more
`id<MTLCommandBuffer>`/`id<MTLRenderCommandEncoder>` pairs may be created and ended — a fresh pair
is created lazily by `ensureFrame()` whenever none is active, and `endActiveEncoding(bool
presentBackbuffer)` is the *only* function allowed to end one: it always commits (a Metal command
buffer cannot be resumed once an encoder ends), but only presents+releases `drawable` when
`presentBackbuffer` is `true`.

Exactly one call site passes `true`: `endFrame()`, itself only reachable from the public
`Present()`. Every other encoder-ending call site (`clear()` starting a fresh pass,
`MetalRenderTargetBackend::BindAsRenderTarget()`/`UnbindAsRenderTarget()`/its own destructor)
passes `false`. The backbuffer's `drawable` is acquired at most once per real frame (lazily, the
first time `resolveActiveAttachments()` needs it with no `RenderTarget2D` bound) and persists
across any number of mid-frame encoder boundaries until genuinely presented — a `Clear()` call or a
render-target switch mid-frame does **not** present a partial backbuffer, matching
`GraphicsDevice.Present()`'s real XNA contract regardless of how many render-target switches
happened in between a game's `Begin`/`End` frame.

`ReadBackbuffer()` is the one documented exception: it deliberately forces an early, self-contained
end-of-frame (ends encoder, blits, presents, commits, waits, all as one unit) because once a
command buffer is committed it cannot be resumed for a later, separate `Present()` — the game's own
subsequent `Present()` call becomes a safe no-op via `endFrame()`'s `if (!command) return;` guard
rather than a double-present.

This model replaced an earlier, genuinely buggy version: `endActiveEncoding()` originally
unconditionally called `presentDrawable:` on every encoder-ending call, so any mid-frame
render-target switch would present a partial backbuffer (visible tearing/flicker) and re-acquire a
fresh drawable on the next backbuffer touch — a single logical frame with N target switches could
present up to N+1 times instead of exactly once. Found and fixed during Phase 18's audit, after
Phase 10 (render targets) had already shipped and been self-reviewed once without catching it.

## Architecture notes

- **MSL is embedded, not vendored as separate `.metal` files**: `kMetalShaderSource` is a single
  raw C++ string compiled at runtime via `newLibraryWithSource:`. Keeps the shader source
  co-located with the C++ structs it must byte-layout-match (`LitUniforms`/`EnvUniforms`/etc.).
- **Plain-C++ mirror structs**: every MSL uniform struct (`LitTransform`/`LitUniforms`/...) has a
  hand-written C++ mirror with identical field layout (`float[4]`-padded vec3s, 3 separate
  `float[4]` "columns" for a normal matrix rather than a 3x3) so `std::memcpy` into a real
  `MTLBuffer` is safe. These mirrors are part of the real ✅ subset (`MetalUniformFill.hpp`).
  Changing MSL-side layout without updating the mirror (or vice versa) is the single easiest way to
  silently corrupt every draw of that shader family — there is no compiler-enforced link between
  the two representations, which is exactly why `MetalUniformFillTests.cpp` exists.
- **Extraction pattern for future plain-C++ candidates**: if a future phase adds another
  `static`/free function or type in `MetalGraphicsBackend.mm` that touches only plain C++ (no
  `id<MTL...>`, no Objective-C), the established pattern is: move the logic verbatim into
  `include/CNA/Internal/Backends/Metal/MetalXxx.hpp` (same names, `Metal`-prefixed), replace the
  original inline definition in the `.mm` with a `using`/one-line wrapper so every existing call
  site is unaffected, write a real GoogleTest suite with no `CNA_BACKEND_METAL` gate, and verify via
  the tier-1 Linux build above before claiming ✅.
