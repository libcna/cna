# Lane card — `llgl` · **INTEGRATED 2026-08-09**

| Field | Value |
|---|---|
| Logical lane | `llgl` |
| Original refs | `refs/heads/feature/llgl` and `refs/remotes/origin/feature/llgl` — both unchanged at **`fa26e72dcda612de2a8cff814e748c7479e45836`** |
| Archive tag | **`archive/preintegration/llgl-20260804`** · annotated object `8f2945091aacf294c104d4117447bc6a97232ea2` · peels to `fa26e72d` · sole LLGL archive · GPG-good · unchanged |
| Fork/base | **`1eb22c1174525b33f1359c3dfacafa971f9e8cb2`** |
| Historical commits / files | **68 / 135** · linear · zero merges |
| Replay head | **`61e75c3e234b7bd67f669274b7be648889f7b383`** — all 68 meaningful commits replayed chronologically |
| Adaptation head | **`c74fbaebb93745de08130d050e11230639df3259`** — 69 signed commits: 68 replay + one stabilization |
| Integration merge/head | **`4ac696c748fb18eef7dd06cca82a0486549bcd5d`** · signed `--no-ff` · parents `21b1fcd1` / `c74fbaeb` |
| Historical worktree | `/rv/data/development/github.com/openeggbert/cnallgl` — clean |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-llgl` — clean |
| Subsystem | LLGL rendering abstraction, pinned OpenGL RenderSystem over native OpenGL/GLX |
| Supported target | Linux/X11 x86_64; runtime validated on Xvfb/Mesa llvmpipe |
| Public identity | **40th** CNA backend: `LLGL`; LLGL's internal renderer modules are not extra CNA identities |
| Shared production delta | One LLGL-guarded window-flag hook in current `GraphicsDevice.cpp`; no common backend-interface, capability-enum, or shared Texture2D production change |
| Conflict class | MEDIUM — 68 one-to-one range-diff pairs: 20 `=`, 48 `!`; 47 include patch/context adaptation, 47 include required author cleanup, and 46 overlap |
| Final status | **READY / INTEGRATED** — no unresolved defect remains on the supported LLGL/OpenGL path |

## 1. i686 / sharp-runtime decision gate

The recorded blocker is real but was attributed to the wrong lane. Exact failure:

- source: `sharp-runtime/include/System/Int128.hpp`, `System::Int128::value_`, line 31,
  `__int128 value_;`;
- compiler: `/usr/bin/i686-w64-mingw32-g++`;
- first diagnostic: `Int128.hpp:31:9: error: expected unqualified-id before '__int128'`;
- first failing translation unit: `sharp-runtime/src/System/BitConverter.cpp`, through
  `BitConverter.hpp` including `Int128.hpp`/`UInt128.hpp`;
- concrete preserved CNA route: the Glide build using
  `cmake/toolchains/mingw-w64-i686.cmake`, selected because Glide's ABI and dgVoodoo DLL are x86.

No historical LLGL i686/Win32/MinGW configure, command, test, log, option or public claim exists.
The LLGL lane plan, backend documentation and SDL native-surface adapter instead claim Linux/X11.
Its historical CMake cache is native ELF64 x86_64 and compiles LLGL Null/OpenGL/Vulkan. Upstream
LLGL 0.04b knows IA32 and advertises Win32 under MSVC, but that does not create a CNA Windows or
i686-MinGW contract. Current sharp-runtime documents a verified x86_64 MinGW target only.

**Classification A.** i686 is non-contract historical Glide coverage and is non-gating for LLGL.
Owner disposition: preserve this record; make no sharp-runtime change; integrate and validate the
strongest truthful Linux/X11 x86_64 route. sharp-runtime stayed unchanged.

## 2. Dependency, identity and native route

The exact dependency is LLGL **`Release-v0.04b`** at
**`1e78d8fa497f5cab76b231ba13f4d6249dac0e7e`**, BSD-3-Clause. CMake uses a pinned shallow
`FetchContent` checkout or the reproducible `CNA_LLGL_ROOT` escape hatch. Builds disable LLGL
examples/tests/C99/C# wrappers, enable exceptions, and compile the static Null, OpenGL and Vulkan
modules on this host.

The supported runtime chain is:

```text
CNA public identity LLGL
  -> LLGL OpenGL RenderSystem
    -> native OpenGL / GLX on X11
```

OpenGL is now a required module. `auto` and explicit `opengl` choose it. The pinned Vulkan module
remains compile coverage, but explicit Vulkan selection rejects because a native validation run
reported CNA/LLGL-originating descriptor, image-layout and teardown violations. Null remains an
explicit lifecycle diagnostic and is never an automatic rendering fallback. Wayland rejects with
an actionable X11 message. Windows, i686 and non-X11 Linux are not claimed.

`GraphicsBackendType::Llgl`, public name `LLGL`, `CNA_GRAPHICS_BACKEND=LLGL`,
`CNA_BACKEND_LLGL`, backend target, selector, factory and dependency gate are token-exact. Selecting
LLGL constructs `LlglGraphicsBackend`; it does not alias Vulkan, EasyGL, Diligent, Sokol, bgfx,
Software or another renderer. Public identities were counted directly from the enum: **39 -> 40**.

## 3. Historical baseline and adaptation

The historical lane's native Debug x86_64 build used its recorded local dependency revision,
ccache, NET=OFF and LLGL Null/OpenGL/Vulkan. It built all 69 LLGL test executables, then the full
build failed at the unrelated final `cna_reference_dump` static-library cycle. Its strongest
dedicated baseline was **143 registered / 118 passed / 25 failed / 0 skipped**. Failures mixed
real historical LLGL defects (ordering, state, RT/depth/stencil, MSAA and camera paths) with the
original broad module claims. The historical full corpus record was **5698 total / 5688 passed /
7 skipped / 3 unrelated failures**.

All 68 original commits were replayed in chronological order; none was omitted. Author date and
technical subject match the original sequence exactly. Forty-seven prohibited-author commits were
reauthored to Robert Vokac; all recreated commits are Robert-authored, attribution/trailer sweeps
are empty, and all 69 adaptation-range commits verify Good with fingerprint
`255C 69CC 1D09 CA54 EF0C C9DF FB9C E8E2 0AAD A55F`.

Range-diff accounts for every commit one-to-one: **20 `=` / 48 `!`**. Of the 48 non-equal pairs,
47 include patch/context adaptation, 47 include the required prohibited-author cleanup, and 46 do
both; the single remaining rows are one metadata-only and one patch-only pair. They preserve LLGL
intent while retaining the post-audit registration union, stream arrays, declaration validation,
deferred capture, command ordering, capabilities, lifetime and Texture2D authority. Historical
shared `GraphicsDevice`/interface files were never restored wholesale.

Conflict/adaptation classes:

- **A — still required:** LLGL-local backend, surface adapter, renderer selection, shaders,
  dependency integration and dedicated tests;
- **B — independently present:** current shared validation, declaration ownership, cache authority,
  render-target and ordered-command infrastructure;
- **C — superseded:** historical shared draw fields and broad Vulkan/cube/stencil/MSAA claims;
- **D — semantic adaptation:** stream-array consumption/rejection, capability truth, current
  offsets, resource deferral, link-group/static-cycle handling and OpenGL-only supported selection;
- **E — tests:** current shared oracles gained LLGL contract rows; stale historical expectations
  were not restored;
- **F — build/docs:** required OpenGL gate, CTest registration/disable boundary and authoritative
  post-audit documentation;
- **G — unrelated:** none carried.

## 4. Supported capability contract

| `GraphicsCapability` | Result | Evidence class |
|---|---:|---|
| `ThreeD` | true | runtime-tested vertex/index, depth, state and stock effects |
| `DepthStencilBuffer` | true | real depth attachment/test/write; stencil is separately false |
| `StencilBuffer` | false | enabling stencil rejects deterministically |
| `MultiSampleAntiAliasing` | false | back-buffer sample requests clamp off; no general public claim |
| `MultipleRenderTargets` | true | 2-4 `RenderTarget2D` slots through SpriteBatch/custom effect; cube/mip MRT rejects |
| `AnisotropicFiltering` | device-dependent true | reported from measured LLGL limit; runtime-covered |
| `OcclusionQuery` | true | LLGL query-heap runtime coverage |
| `CustomEffects` | true | documented SpriteBatch GLSL path only |
| `Texture3D` | true | transfer/readback storage; shader sampling not claimed |
| `MultiStreamVertexInput` | false | more than one stream rejects before submission |
| `Instancing` | false | instance-frequency and instanced draws reject before submission |
| `WireFrame` | device-dependent | reported only when the active device exposes it; runtime control |
| `AdditiveBlending` | true | pixel-covered; constant blend-factor states still reject |

PBR is not a capability-enum member; the LLGL OpenGL `PbrEffect` path is runtime-covered.

## 5. Draw, effects, textures and lifetime

The post-audit `GpuDrawParams` stream arrays are authoritative. LLGL consumes one geometry stream
and honours geometry `VertexOffset`, `vertexStart`, `startIndex` and `baseVertex`; indexed and
non-indexed shared controls pass. Declaration validation, ownership, duplicate semantics and
`BasicEffect.VertexColorEnabled` remain covered by the full corpus. Ordinary multistream, classic
1+1 instancing, per-instance `VertexOffset`/`InstanceFrequency`, and more than one instance reject
deterministically. No removed draw field was restored.

Shaders remain authoritative GLSL sources plus the deterministic repository generator and checked-
in generated header. No shader source or generated output changed during stabilization. OpenGL GLSL
backs SpriteBatch, BasicEffect colour/texture/fog/lighting, AlphaTest, DualTexture, Skinned and PBR.
Custom `ShaderEffect` support is SpriteBatch-only. Vulkan SPIR-V is not a supported runtime claim.

Texture2D upload/readback, transfer ranges, odd widths/row pitch, render-target unbind sampling,
first use, producer/consumer ordering, mip-mapped `RenderTarget2D`, depth, MRT and recreation pass.
Plain TextureCube uses exact bounded CPU face/mip transfer storage on this contract; cube shader
sampling and `RenderTargetCube` reject. Texture3D transfer passes. Unsupported MRT+mip and cube-face
compositions reject rather than silently omitting mip regeneration.

Buffers, textures, shaders, layouts, pipelines, render targets and command resources are released
through their owning LLGL RenderSystem. Deferred frame commands retain/capture the resources and
state they consume; wrapper destruction schedules release after submission. Device teardown and
the bound-target/deferred-source lifetime matrix pass. No stale LLGL object survives its owner in
the supported route.

## 6. Findings and validation

- `LLGL-48` resolved: the complete blend state has its own primitive-pipeline cache key element;
  the shared 3D colour-write-channel oracle is gating.
- `LLGL-52` resolved: orthographic/CreateLookAt camera and indexed BasicEffect controls pass.
- `LLGL-53` resolved by truthful boundary: viewport/depth/RT controls pass; non-zero depth bias and
  stencil reject and are not advertised.
- `LLGL-54` preserves proven MRT/MSAA; unsupported cube and mip-MRT combinations reject.
- `LLGL-55/-56` are satisfied/narrowed for X11/OpenGL. Vulkan is unsupported; sanitizer allocations
  rooted in pinned LLGL/SDL/Mesa GLX visual selection are external.
- **`LLGL-57` (new, resolved):** reset/first-frame swap-chain extent drift. Virtual-resolution and
  capture boundaries synchronize LLGL resolution and resize invalidates the readback cache.
- **`LLGL-58` (new, resolved):** pinned LLGL's deferred GL command buffer passed a null pointer to a
  zero-byte `memcpy`. Both CNA BeginRenderPass call sites now preserve count zero with a valid
  address; no dependency patch was made.

Final supported-path evidence from persistent lane trees:

- `cmake-build-llgl`: Debug x86_64, ccache, LLGL Null/OpenGL/Vulkan compiled; complete LLGL label
  **145 registered / 137 executed and passed / 0 failed / 8 explicitly disabled**;
- full `CnaTests`: **5210 total / 5203 passed / 7 skipped / 0 failed** with explicit
  `SDL_VIDEODRIVER=x11` on Xvfb `:98`;
- ASan+UBSan runtimes proven linked; strict focused matrix **9/9**, zero CNA OOB/UAF/double-release/
  stale-resource/overflow/uninitialized/UB report;
- LeakSanitizer's representative run reports **482104 bytes in 2147 allocations**, rooted in
  pinned LLGL `LinuxGLContextX11::ChooseVisual`, SDL3 X11 visual selection and Mesa/GLX; the same
  binaries pass strictly with leak detection disabled, so this is narrowly external, not hidden;
- OPENGLES/EasyGL principal control: **9/9** runtime contract tests and **15/15** cache/validation/
  backend-definition units; `CnjCacheIsolationTest` remains green;
- accepted Direct2D control under Wine/Xvfb: **4/4** smoke/parity/lifetime/unit;
- other backend controls: none required; shared production changes are LLGL-guarded and the EasyGL
  control exercises the shared-example inverse branches.

`REMED-GFX-223` remains resolved; `REMED-GFX-224` remains **MEDIUM/OPEN** and untouched.
`REMED-CONTENT-007/-008/-011` remain DONE; `REMED-BUILD-019` remains resolved; Direct2D
`D2D-134/-135/-136` remain resolved. No finding was absorbed or renamed.

## 7. Process, group and next action

All compilation used a numeric bound of four jobs or fewer. The vendored SDL helper was inspected:
its own numeric bound is two. The provable whole-session maximum is **4**, never 8. ccache was used.
No process was signalled; one session-owned hung SDL submodule checkout during baseline setup was
interrupted through its exact terminal session and reinitialized, then every affected gate reran.

Persistent build-tree sizes at handoff are `cmake-build-llgl` **6.7 GiB**,
`cmake-build-llgl-asan` **3.2 GiB**, `cmake-build-llgl-easygl` **1.6 GiB**, and historical
`cmake-build-llgl-pre` **6.2 GiB**. The build volume has **63 GiB free**. Final ccache state is
21431 cacheable calls, 1619 hits (7.55%), 19812 misses, and 3.9/10.0 GiB local storage.

One runtime-validation attempt incorrectly used real `DISPLAY=:0`, which opened many short-lived
test windows on the owner's desktop. This was acknowledged as a process-safety deviation. It was
not used as final evidence; after the owner report, every final LLGL, sanitizer, EasyGL and Direct2D
runtime gate used dedicated Xvfb `:98` with explicit X11 selection. No unrelated process was
inspected or controlled.

LLGL is the twentieth integrated lane. The logical inventory is **20/21**; pending is exactly
`metal`. Group G is **3/4**: Skia, Direct2D and LLGL integrated; Metal pending. Metal did not begin.
No Batch 6 checkpoint is defined or eligible while Metal remains, so no checkpoint was created.
The recommended next action is Metal, subject to its authoritative no-Mac validation boundary; do
not begin it from this record.

`audit/` remains tree `168c9b668763b78e63106e27d942a76d2457f41d`; the four protected stash
objects are unchanged; nothing was pushed.
