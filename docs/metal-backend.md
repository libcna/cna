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

## The central caveat: this backend has never been compiled

Every development session on this backend to date has run on Linux (Debian 13), which has no Apple
Clang, no Metal framework headers, and no Metal-capable GPU. **The `.mm` files that make up the
bulk of this backend's implementation have never been compiled, linked, or executed anywhere.**
Every "landed" phase below is source-complete and was written by careful, line-by-line comparison
against `EasyGLGraphicsBackend`'s already-shipping, already-tested equivalent logic (CNA's most
mature 3D backend) — but "matches the reference implementation on paper" is not the same claim as
"builds and runs correctly." Do not treat any 🟨 row below as verified in the sense CNA's other
backend docs use that word.

`.github/workflows/metal-macos-ci.yml` is the first thing that will ever actually build this
backend, on a real `macos-14` GitHub Actions runner. Until a run of that job (or a physical Mac) is
observed to pass, every 🟨 status below should be read as "carefully written, never compiled."

## Status legend

- ✅ — **real, machine-verified**: actually compiled and executed by a real test binary on some
  real machine, with the specific `ctest`/CI evidence cited. Never claimed on inspection alone.
- 🟨 — source-complete: written and reviewed line-by-line against the FNA/EasyGL reference, but
  never compiled (see caveat above). This is the ceiling reachable without an Apple toolchain for
  any code that touches `id<MTL...>`/`CAMetalLayer`/Objective-C directly.
- ⬜ — not started.

One narrow exception exists to the "never compiled" rule: 7 pieces of this backend's logic touch
*no* Objective-C or Metal-framework types at all (they only read/write plain C++ structs, XNA
framework types, and `float` arrays) — see "Real, machine-verified subset" below. Those pieces
carry a genuine ✅, extracted specifically so they could earn one without Apple hardware.

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
- Instancing (Phase 9, blocked downstream on Phase 14's generic descriptor builder for anything
  beyond the fixed `PipelineKind` set).
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

Seven pieces of this backend's logic are plain C++ with zero Objective-C/Metal-framework
dependency — extracted out of `MetalGraphicsBackend.mm` into standalone headers under
`include/CNA/Internal/Backends/Metal/`, with `MetalGraphicsBackend.mm` reduced to thin same-name
aliases/wrappers so every call site is unaffected. Each has a dedicated GoogleTest suite under
`tests/CNA/Internal/Backends/Metal/` that carries **no** `#if defined(CNA_BACKEND_METAL)` gate
(deliberately — unlike every other backend's own tests, these must run under whichever backend
this machine can actually build, e.g. `HEADLESS`). All 55 tests pass under a real
`-DCNA_GRAPHICS_BACKEND=HEADLESS -DCNA_BUILD_TESTS=ON` `CnaTests` build and `ctest -R "^Metal"` run
on Linux; an independent adversarial audit separately re-derived every expected value against the
real source structs/functions and found no functional defects (see `plan_metal.md`'s narrative
items 29–35 for full detail and the exact CTest numbers).

| Header | What it covers |
|---|---|
| `MetalPipelineKey.hpp` | `MetalPipelineKind` enum, `MetalBlendKey`, `MetalPipelineCacheKey`/Hash |
| `MetalNormalMatrix.hpp` | `ComputeMetalNormalMatrixCols` — `transpose(inverse(world3x3))` shortcut |
| `MetalPrimitiveVertexCount.hpp` | `ComputeMetalPrimitiveVertexCount` — `PrimitiveType` → vertex count |
| `MetalLogicalViewport.hpp` | `ComputeMetalLogicalViewport` — letterbox/overscan/etc. formula |
| `MetalMat4.hpp` | `MetalMat4Multiply`/`FromXna`/`Transpose` — the WVP matrix helper set |
| `MetalSelectPipelineKind.hpp` | `SelectMetalPipelineKind` — the shader-variant dispatch decision |
| `MetalUniformFill.hpp` | `FillMetal{Lit,Env,Skinned,Pbr,SkinnedPbr}Uniforms` — `GpuDrawParams` → GPU uniform mapping |

This subset does not include anything that constructs or issues real Metal API calls
(`vertexDescriptorForStride`, `makePipeline`, the `metalPrimitive`/`metalCompareFunction`/
`metalBlendFactor`/etc. enum-mapping functions, all real command-encoder work) — those return or
consume genuine `MTL*` types declared only in Apple's Metal framework headers and cannot be
extracted the same way without either the framework itself or hardcoding Apple's numeric enum
values as an out-of-pattern risk. That remaining surface stays 🟨 pending real hardware.

## Known limitations / explicitly open

- **Fully generic `VertexElement`-driven descriptor builder** (`METAL-26`/`27`) — the current
  pipeline cache uses a fixed `PipelineKind` enum (one entry per concrete shader+layout this
  backend actually emits) rather than a hashed arbitrary-`VertexElement`-list key. Lower risk to
  get right without a compiler than inventing the generic version blind; blocks Phase 14 (custom
  `ShaderEffect`) and, transitively, Phase 9 (Instancing) for anything beyond the fixed set.
- **MRT / MSAA** (`METAL-104`/`105`/`112`/`113`) — the rest of Phase 10.
- **`METAL-256`** — a real texture-update CPU/GPU-sync hazard Phase 18's audit found but did not
  fix (documented, not silently ignored).
- **`METAL-257`** — a cross-backend missing-`SDL_WINDOW_HIGH_PIXEL_DENSITY` gap Phase 16's research
  found; deliberately left for a cross-backend task since it isn't Metal-specific (confirmed to
  also affect iOS/tvOS, not just macOS).
- **Phases 21–24** (argument buffers/bindless, indirect command buffers, MetalFX upscaling, GPU
  counters/Xcode frame capture) — all NOXNA extensions requiring real hardware/Xcode to design
  meaningfully, not attempted.
- **Phase 28** (cross-backend pixel parity) and the rest of Phase 29 (a real iOS/tvOS build-only CI
  job and everything downstream of it) — ⬜.
- **Every 🟨 row above** — see the central caveat: source-complete, never compiled, until
  `metal-macos-ci.yml` or a physical Mac proves otherwise.

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

2. **Real ✅ on macOS (everything else, once observed passing)**: `.github/workflows/metal-macos-ci.yml`
   on a `macos-14` GitHub Actions runner — configures with `-DCNA_GRAPHICS_BACKEND=METAL
   -DCNA_BUILD_TESTS=ON`, builds, then runs the same `ctest -R "^Metal"` filter, which now also
   covers `Metal_Smoke` (the real end-to-end device/window/swapchain/draw smoke test) alongside the
   55 tests from tier 1. As of this writing this job has been extended to run that broader filter
   but a passing run has not yet been observed from this Linux sandbox — treat as pending until a
   real CI run or physical Mac confirms it.

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
