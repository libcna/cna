# NEXT.md — CNA Project Handoff (`feature/dx9` branch — Direct3D 9 backend only)

> **This `NEXT.md` is scoped to the D3D9 backend only, per explicit project-owner instruction
> (2026-07-14).** This branch (`feature/dx9`, worktree `cnadx9`) is a parallel effort to the
> established EasyGL/Vulkan/Bgfx/SDL_Renderer/WebGPU/Headless/Software/D3D11/D3D12 backends, all of
> which are developed on other branches (`develop` and friends) and are **not tracked here**. For
> their status, see `plan_graphics.md`, `plan_dx.md`, `plan_webgpu.md`, `plan_software.md`,
> `plan_headless.md`, and `git log` on those branches — this file will not duplicate it, and will
> not be updated for non-D3D9 work. Full D3D9 task-by-task detail and history lives in
> **`plan_dx9.md`** (`D9-0`–`D9-140`); this file is a short current-state index, the same relationship
> `plan_dx.md`/`NEXT.md` had for D3D11/D3D12 before this branch existed.
>
> **Status (2026-07-14): implementation authorized, Phase D9-0 spikes closed, no backend code written
> yet.** The project owner has authorized implementation through Phase D9-13 (`plan_dx9.md`'s own
> "Boundaries" still require asking before Phase D9-11 "custom `ShaderEffect`"; Phase D9-14 needs real
> Windows hardware and is `needs_human`). The plan's one architectural blocker — the
> `IGraphicsBackend`/`GraphicsBackendCreateArgs` boundary problem — is also resolved: an additive
> extension (new optional presentation-parameter fields + a narrow device-event notification channel)
> is approved, unblocking `D9-30`/`D9-32`/`D9-33`/`D9-34`. See `plan_dx9.md`'s top banner and "The
> `IGraphicsBackend` boundary problem" section for the full record.

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend layer. This branch
adds a **Direct3D 9** backend — see `plan_dx9.md` for the full plan. Unlike every other CNA backend,
this one is not a coverage/parity effort: its stated goal (set by the project owner) is that a CNA
game running on D3D9 be **indistinguishable** from the same game running on the original XNA 4.0
runtime, verified against a real XNA 4.0 oracle running under Wine (Phase D9-A), not just "renders
plausibly."

- **Key decisions already made** (see `plan_dx9.md` design decisions 1–17 for the full rationale):
  - Plain `Direct3DCreate9`, **not** D3D9Ex — `D3DPOOL_MANAGED` for user resources so they survive
    `Reset()`, and the real XNA device-lost lifecycle (`DeviceLost`/`DeviceResetting`/`DeviceReset`)
    is implemented for real, for the first time in this project.
  - Microsoft's own XNA 4.0 Stock Effects HLSL (`BasicEffect.fx` and 5 siblings, from the FNA tree)
    are **vendored verbatim** and compiled by CNA itself (`D3DCompile`, `vs_2_0`/`ps_2_0`) — not
    reimplemented, not ported. The `.fxb` shipped bytecode is a verification oracle only.
  - `D3DCommon` (shared with D3D11/D3D12) is **not** expanded — D3D9 gets its own
    `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations`.
  - Render state, not state objects (`SetRenderState`/`SetSamplerState` sequences — no D3D9 state
    objects exist to cache).
  - This is the **only** CNA backend that can natively answer `GraphicsAdapter::IsProfileSupported()`
    for real (`D3DCAPS9`) — Phase D9-10.
- **A cross-cutting finding, not this plan's to fix**: taking XNA seriously as the spec surfaced six
  confirmed CNA-vs-XNA divergences that exist on **every** CNA backend today (worst: CNA always
  lights per-pixel; XNA's default is per-vertex, and CNA has no per-vertex lighting shader anywhere).
  This plan measures and reports them (Phase D9-A6, `D9-81`); it does **not** fix them — that is a
  `plan_graphics.md`-level, project-owner decision. See `plan_dx9.md`'s "CNA's divergences from XNA
  4.0" section before touching any of this.

---

## 2. Current status

### Build status

| Build dir | Backend | Status |
|---|---|---|
| `cmake-build-d3d9` | D3D9 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-14**: `cmake -DCNA_GRAPHICS_BACKEND=D3D9 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_BUILD_TESTS=ON` configures; `CNA`/`cna_backend_graphics_d3d9`/`cna_test_d3d9_common`/`cna_test_d3d9_smoke` all build clean. `D3D9_Common` 28/28 + `D3D9_Smoke` 36/36 pass via `ctest --test-dir cmake-build-d3d9 -L D3D9`. A real device now creates, clears, presents, reads back pixels, resizes, recovers from a (simulated) device-lost event, round-trips real vertex/index buffer data, and round-trips real 2D/cube/volume texture data, all through the actual public `Game`/`GraphicsDeviceManager`/`GraphicsDevice` API. |

### Phase D9-0 — feasibility spikes: CLOSED 2026-07-14

| Task | Status |
|---|---|
| `D9-1` — real Microsoft `d3dcompiler_47.dll` compiles all 66/66 stock-effect entry points | ✅ |
| `D9-73` — 61/66 byte-identical to Microsoft's shipped `.fxb`; decision made (CNA compiles its own) | 🟨 (decided; 5 `PixelLighting` variants still need oracle-proof, `D9-73`'s own obligation) |
| `D9-A1`/`D9-A2` — real XNA 4.0 runs under Wine and renders a verified `CornflowerBlue` triangle | ✅ |
| `D9-2` — confirm minimum link set (`d3d9` alone, no `dxguid`) | ✅ |
| `D9-3` — Wine+DXVK D3D9 loop end-to-end: exact pixel round-trip + full `D3DCAPS9` dump | ✅ |
| `D9-4` — `D3DPOOL_MANAGED` genuinely `LockRect`-readable and survives `Reset()` intact | ✅ |
| `D9-5` — `scripts/run-wine-dxvk9.sh` (new script, DXVK-marker gate, positive+negative proven) | ✅ |

**Phase D9-0 is fully closed.** Next up: Phase D9-1 (CMake integration + backend skeleton).

### Phase D9-1 — CMake integration and skeleton: CLOSED 2026-07-14

| Task | Status |
|---|---|
| `D9-10` — `D3D9` added to all 7 `CMakeLists.txt` `"D3D12"` sites, minus one real correction | ✅ |
| `D9-11` — `D3D9GraphicsBackend` skeleton (22 pure virtuals + 10 silently-empty ones handled) | ✅ |
| `D9-12` — `GraphicsDevice.cpp` `#ifdef` audit | ✅ (zero changes needed) |

**Phase D9-1 is fully closed.** `D9-10` found one real, worth-fixing gap in this plan's own text: it
described CMake line 288 as "a second Windows-only-related OR chain" needing a D3D9 sibling, but that
line is actually the `D3DCommon` shared-core conditional — adding D3D9 there would have violated
design decision 12 ("`D3DCommon` is not expanded"). Left untouched, with an explanatory comment;
`plan_dx9.md`'s own `D9-10` row now records the correction. Line 392 (the `CNA` circular-link `OR`
chain) was also deliberately left out of D3D9's `OR` chain — nothing calls back into a CNA-defined
symbol yet (that's `D9-112`, Phase D9-11, ask-first).

### Phase D9-2 — mapping layer: CLOSED 2026-07-14 (one row 🟨)

| Task | Status |
|---|---|
| `D9-20` — `D3D9FormatMapping` (`SurfaceFormat`/`DepthFormat` → `D3DFORMAT`) | ✅ |
| `D9-21` — `D3D9StateMapping` (7 state enums → D3D9 equivalents) | 🟨 (table done; `D3DCULL` pixel-proof against the oracle owed to `D9-84`) |
| `D9-22` — `D3D9VertexDeclarations` (stride-keyed `D3DVERTEXELEMENT9` arrays) | ✅ |
| `D9-23` — `D3D9_Common` CTest, mutation-verified | ✅ (28/28 checks) |

**Phase D9-2 is closed** (one honestly-flagged partial, not a blocker). Two non-obvious findings
worth knowing before touching this code: **`SurfaceFormat::Color` → `D3DFMT_A8B8G8R8`, NOT
`D3DFMT_A8R8G8B8`** (D3D9's channel-order naming reads MSB→LSB, opposite DXGI's convention — get this
backwards and every Color-format texture samples with R/B swapped); and **`Rgba1010102` →
`D3DFMT_A2B10G10R10`, NOT the superficially-similar `D3DFMT_A2R10G10B10`** (that one has no DXGI
equivalent at all — different alpha-bit position). Both verified against Microsoft's own published
D3D9→DXGI legacy-format table, not derived by name resemblance. Next up: Phase D9-3 (device, present,
device-lost).

### Phase D9-3 — device, present, device-lost: ALL 5 rows closed (D9-32/D9-34 honestly 🟨)

| Task | Status |
|---|---|
| `D9-30` — real `Direct3DCreate9`/`GetDeviceCaps`/`CreateDevice` with real presentation parameters | ✅ |
| `D9-31` — `Clear` + all 6 `Clear*` combos + `Present` + `ReadBackbuffer`, each pixel-verified | ✅ (`D3D9_Smoke`) |
| `D9-32` — enforce `GraphicsProfile` floor at construction | 🟨 (shader-model floor real; full Reach/HiDef table is `D9-100`'s job) |
| `D9-33` — window resize via device `Reset()` | ✅ (mechanism + dedicated 64×64→96×80 test, Check L) |
| `D9-34` — XNA device-lost lifecycle | 🟨 (real mechanism + real event order proven via `DebugSimulateContextLoss`; genuine driver-triggered loss + event-payload-vs-real-XNA fidelity are `D9-A`/`D9-140`'s own jobs) |

**Phase D9-3 is now fully closed** (both 🟨 rows have named, honest, out-of-this-plan's-current-reach
gaps, not missed work). `D9-34`: `Present()` detects real `D3DERR_DEVICELOST`, fires `DeviceLost`;
`PollDeviceLost()` polls `TestCooperativeLevel()` until `D3DERR_DEVICENOTRESET`, then
`PerformResetRecovery()` fires `DeviceResetting`, calls a real `Reset()`, restores the viewport, fires
`DeviceReset`. Since DXVK will rarely lose the device naturally, the full sequence was exercised
deterministically via the pre-existing `DebugSimulateContextLoss()`/`DebugRestoreContext()` test
channel (`D3D9_Smoke` Check M, 8 new checks) — real event counts/order, a real `Clear()` throwing the
real XNA `DeviceLostException` while lost, a real `Reset()` call during recovery, and the device
genuinely rendering again afterward. Also fixed a separate, pre-existing gap found along the way:
`GraphicsDevice::getGraphicsDeviceStatusProperty()` was hardcoded `return
GraphicsDeviceStatus::Normal;` always — now tracks the real backend-reported state.

**Two real, unplanned findings surfaced while closing D9-30/D9-31, both fixed in place:**

1. **D3D9 rejects `SurfaceFormat::Color`'s own `D9-20` back-buffer format.** DXVK's D3D9
   implementation (correctly matching real D3D9 behavior) refused `D3DFMT_A8B8G8R8` as a *swap-chain*
   format — that format is legal for textures but D3D9 restricts the primary back buffer to a small
   set of display-compatible formats. Fixed with a back-buffer-specific substitution to `A8R8G8B8`
   (`ReadBackbuffer()` already handles both byte orders). Not a DXVK quirk — a real, confirmed D3D9
   API restriction, documented in `D3D9GraphicsBackend.cpp`.
2. **`GraphicsDevice::Reset()` never told an already-constructed backend about updated back-buffer/
   depth-stencil/fullscreen settings** — only virtual resolution and MSAA were re-pushed. This matters
   because `Game` typically constructs its `GraphicsDevice` (and backend) with *default*
   `PresentationParameters`, before `GraphicsDeviceManager.ApplyChanges()` ever applies the game's real
   preferences. Fixed with one more small additive `IGraphicsBackend` method,
   `UpdatePresentationFormatEXT()` (empty default; every other backend ignores it unchanged) — the
   same category of fix as the already-approved boundary-problem resolution, not a new architectural
   decision.

**A third finding forced Phase D9-6 (render states) in far earlier than planned.**
`GraphicsDevice`'s own constructor unconditionally pushes `BlendState::Opaque`/
`DepthStencilState::Default`/`RasterizerState::CullCounterClockwise` and the viewport (Task 896/955) —
meaning `ApplyBlendState`/`SetBlendFactor`/`ApplyDepthStencilState`/`SetReferenceStencil`/
`ApplyRasterizerState`/`SetViewport`/`SetScissorRect` could not stay `NotYetImplemented()` stubs for
*any* device to finish constructing, regardless of this plan's own phase ordering. All are now real
(`D3DRS_*` `SetRenderState()` sequences via the `D9-21` mapping tables — see §2's Phase D9-6 entry
below). Along the way, also found that `D9-11`'s own "10 silently-empty virtuals" count missed 4 more
(`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState`) because their
`{}` defaults span multiple lines, invisible to a single-line `grep`; `ApplySamplerState` now throws
`NotYetImplemented()` like the original 10 (nothing forced it in early — no texture/sampler work
exists yet).

### Phase D9-6 — render states: D9-60/D9-61/D9-62 forced in early (🟨), D9-63/D9-64 still open

| Task | Status |
|---|---|
| `D9-60` — `ApplyBlendState`/`SetBlendFactor` | 🟨 (real; `D3DRS_COLORWRITEENABLE` genuinely out of scope — see plan) |
| `D9-61` — `ApplyDepthStencilState`/`SetReferenceStencil` | ✅ |
| `D9-62` — `ApplyRasterizerState`/`SetScissorRect`/`SetViewport` | 🟨 (real; oracle pixel-proof owed to `D9-84`, same as `D9-21`'s own `D3DCULL` obligation) |
| `D9-63` — `ApplySamplerState` | ⬜ (no textures/samplers exist yet) |
| `D9-64` — reuse backend-agnostic state CTest sources | ⬜ |

Real, confirmed finding: D3D9's `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` are floats, and XNA's own
float `DepthBias`/`SlopeScaleDepthBias` map through with **no unit conversion** (unlike D3D11, which
needs float→`INT` rounding) — `SetRenderState()` still takes a `DWORD` parameter, so the float bits
are reinterpreted (`std::bit_cast`), not numerically converted.

### Phase D9-4 — buffers: D9-40/D9-41/D9-42 CLOSED

| Task | Status |
|---|---|
| `D9-40` — `D3D9VertexBufferBackend` | ✅ |
| `D9-41` — `D3D9IndexBufferBackend`, 16-bit and 32-bit, `CreateIndexBuffer32()` explicit | ✅ |
| `D9-42` — byte-exact round-trip tests | ✅ (folded into D9-40/41's own checks) |

Real architectural finding, not anticipated by this row's own plan text: `D3DUSAGE_DYNAMIC` requires
`D3DPOOL_DEFAULT` (D3D9 forbids `DYNAMIC` with `POOL_MANAGED`), so these buffers do **not** survive a
device `Reset()` the way ordinary `D3DPOOL_MANAGED` resources do. New `ID3D9DefaultPoolResourceEXT`
interface + a small registry on `D3D9GraphicsBackend` lets `D9-34`'s `PerformResetRecovery()` release
every live `D3DPOOL_DEFAULT` resource before `Reset()`; each recreates lazily on next use — real,
authentic D3D9/XNA behavior (a `DYNAMIC` buffer's content genuinely does not survive `DeviceReset` in
real XNA either). Mutation-verified: temporarily broke `CreateIndexBuffer32()` to build a 16-bit
buffer instead — caught immediately (a real, uncaught exception from the existing type-mismatch
guard), reverted, reconfirmed green. Also confirmed and fixed the exact "pointer-inequality is not
sound proof of recreation" false-negative this project's own D3D12 work already found once (see
`plan_dx9.md`'s `D9-40` row). `D3D9_Smoke` is now 30/30 checks.

### Phase D9-5 — textures/render targets/readback: D9-50/D9-51/D9-52 CLOSED, D9-53–56 open

| Task | Status |
|---|---|
| `D9-50` — `D3D9TextureBackend` (`IDirect3DTexture9`, `D3DPOOL_MANAGED`), mip levels, sub-rect `SetData` | ✅ |
| `D9-51` — `D3D9TextureCubeBackend`/`D3D9Texture3DBackend`, volume support gated on real `D3DCAPS9` | ✅ |
| `D9-52` — `GetData()` for 2D/cube/3D | ✅ (found empirically: 2D has none to implement — `Texture2D::GetData()` is CPU-shadow-based, same as D3D11; cube/3D genuinely delegate to the backend and are real `LockRect`/`LockBox` reads) |
| `D9-53` — `D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend` (`D3DUSAGE_RENDERTARGET`, `D3DPOOL_DEFAULT`) | ⬜ |
| `D9-54` — MRT via `SetRenderTarget(i, surface)`, capped at `NumSimultaneousRTs` | ⬜ |
| `D9-55` — `D3D9OcclusionQueryBackend` | ⬜ |
| `D9-56` — NPOT handling driven by `D3DPTEXTURECAPS_POW2`/`NONPOW2CONDITIONAL` | ⬜ |

New `include/`/`src/CNA/Internal/Backends/D3D9/D3D9Textures.hpp`+`.cpp`. All three texture backends use
`D3DFMT_A8B8G8R8`/`D3DPOOL_MANAGED` (RGBA8 storage only, same simplification D3D11 already documents —
`surfaceFormat` accepted for signature compatibility, not honored). Since `D3DPOOL_MANAGED` (not
`DEFAULT`), none of these register with the `D9-40` device-lost registry — they survive `Reset()`
automatically, same as `D9-4`'s own spike found. Cube-face order (0..5 = +X,-X,+Y,-Y,+Z,-Z) matches
D3D9's own native `D3DCUBEMAP_FACES` enum order, so no face-remapping table is needed. Volume-texture
creation is gated on `D3DCAPS9::MaxVolumeExtent > 0`, cube-map creation on
`D3DPTEXTURECAPS_CUBEMAP` — both report supported on this dev environment's DXVK device, so `D3D9_Smoke`
Check R exercises the real creation path for both, not just the capability-gate branch (the
unsupported/`nullptr` branch is exercised by construction but not provably reachable without
lesser-capable hardware — an honest gap, not a hidden one). Wired into `D3D9GraphicsBackend::CreateTexture()`/
`CreateTextureCube()`/`CreateTexture3D()` (previously stubs/inherited `nullptr` defaults). `D3D9_Smoke`
Checks Q/R (6 new checks) verify exact-byte round-trips via direct `LockRect`/`LockBox` on the
`D3DPOOL_MANAGED` resources themselves — no staging-texture copy needed, unlike D3D11's equivalent
check. Mutation-verified (see §3). `D3D9_Smoke` now 36/36.

### Does NOT work yet

Render targets, MRT, occlusion queries, NPOT capability surfacing (`D9-53`–`56`), draws, `SpriteBatch`
— all still throw `NotYetImplemented()` or return `nullptr` naming their own follow-up task, by design.
`D9-63` (sampler state) and `D9-64` (reused state CTests) are the remaining open rows in Phase D9-6.
The mapping tables (`D9-20`–`23`) are now consumed by the render-state push path, the buffer-creation
path, and the texture-creation path; draw consumers still come later.

---

## 3. Recent changes

Most recent first. Full detail lives in `plan_dx9.md` — this is a short index.

| Commit(s) | Summary |
|---|---|
| *(pending)* | **Phase D9-5 partially closed** (`D9-50`/`D9-51`/`D9-52`): new `D3D9TextureBackend`/`D3D9TextureCubeBackend`/`D3D9Texture3DBackend` (`D3DFMT_A8B8G8R8`, `D3DPOOL_MANAGED`). Found empirically that `D9-52`'s own premise only half-applies: `ITextureBackend` (2D) has no `GetData()` at all — `Texture2D::GetData()` is CPU-shadow-based, same architecture as D3D11 — while `ITextureCubeBackend`/`ITexture3DBackend` genuinely delegate `GetData()` to the backend, and those ARE real `LockRect`/`LockBox` reads, exactly as `D9-4`'s spike predicted (no staging/`SYSTEMMEM` fallback needed). Volume/cube-map creation gated on real `D3DCAPS9` (`MaxVolumeExtent`/`D3DPTEXTURECAPS_CUBEMAP`), not assumed. New `D3D9_Smoke` Checks Q/R (6 checks): exact-byte round-trips via direct locks on the `D3DPOOL_MANAGED` resources (no staging texture needed). Mutation-verified: corrupting the 2D upload's source-row offset turned exactly Check Q's first assertion red, nothing else; reverted, reconfirmed 36/36 green. `D9-53`–`56` (render targets/MRT/occlusion/NPOT) remain open. |
| `3e855b2d` | **Phase D9-4 fully closed** (`D9-40`/`D9-41`/`D9-42`): real `D3D9VertexBufferBackend`/`D3D9IndexBufferBackend` (16-bit and 32-bit, `CreateIndexBuffer32()` explicitly overridden), `Lock`/`Unlock` with `SetDataOptions` → `D3DLOCK_DISCARD`/`NOOVERWRITE`. Real finding: `D3DUSAGE_DYNAMIC` requires `D3DPOOL_DEFAULT` (forbidden with `POOL_MANAGED`), so these buffers do NOT survive `Reset()` automatically — new `ID3D9DefaultPoolResourceEXT` registry lets `D9-34`'s recovery path release them before `Reset()`, each recreating lazily on next use (real XNA/D3D9 behavior). Mutation-verified (`CreateIndexBuffer32()` temporarily broken to build a 16-bit buffer, caught immediately via a real uncaught exception, reverted). Also avoided the "pointer-inequality isn't sound recreation proof" false-negative this project's own D3D12 work already found once. `D3D9_Smoke` now 30/30. |
| `cbd75a0b` | **Phase D9-3 fully closed — `D9-34` (device-lost lifecycle)**: `Present()` detects real `D3DERR_DEVICELOST`, fires `DeviceLost`; while lost, polls `TestCooperativeLevel()` until `D3DERR_DEVICENOTRESET`, then fires `DeviceResetting`, calls a real `Reset()`, restores the viewport, fires `DeviceReset`. `Clear`/all `Clear*` combos/`ReadBackbuffer` now throw the real XNA `DeviceLostException` while lost. Exercised deterministically (DXVK rarely loses the device naturally) via the pre-existing `DebugSimulateContextLoss()`/`DebugRestoreContext()` test channel — new `D3D9_Smoke` Check M (8 checks): real event counts/order, a real `Reset()` during recovery, and the device genuinely working again afterward. Also fixed a separate pre-existing gap: `GraphicsDevice::getGraphicsDeviceStatusProperty()` was hardcoded to `Normal` always; now tracks the real backend-reported state. `D3D9_Smoke` now 24/24. Verified no regression on EasyGL/CnaTests. |
| `70e81079` | **`D9-32` closed (shader-model floor) + `D9-33`'s dedicated resize test (Check L)**: `GraphicsProfile::HiDef` now checked against the real `D3DCAPS9` at construction, throwing the real XNA `NoSuitableGraphicsDeviceException` if below `vs_3_0`/`ps_3_0` (only the positive path provable on this real, already-SM3-capable GPU); a new `D3D9_Smoke` Check L resizes 64×64→96×80 via the real `GraphicsDeviceManager` path and confirms the viewport, a post-resize pixel readback at both the origin and the new far edge, and `PresentationParameters` all reflect the new size. `D3D9_Smoke` now 17/17. |
| `50954798` | **`D9-30`/`D9-31` closed + `D9-33`'s resize mechanism + Phase D9-6's `D9-60`/`D9-61`/`D9-62` forced in early**: real `Direct3DCreate9`/`CreateDevice` using the game's actual requested back-buffer/depth-stencil format (the approved `GraphicsBackendCreateArgs` extension, finally consumed for real); all 6 `Clear*` combos + `Present` + `ReadBackbuffer` pixel-verified (`D3D9_Smoke` 12/12); a real `EnsureDeviceSize()` resize-via-`Reset()` mechanism (proven working, not theoretical — it's what makes the smoke test converge to the requested 64×64 size at all). Two real, unplanned findings fixed in place: DXVK genuinely rejects `SurfaceFormat::Color`'s own `D3DFMT_A8B8G8R8` as a *swap-chain* format (a real D3D9 display-format restriction, fixed with a back-buffer-specific substitution to `A8R8G8B8`); and `GraphicsDevice::Reset()` never forwarded updated presentation settings to an already-constructed backend, fixed with one more small additive `IGraphicsBackend` method (`UpdatePresentationFormatEXT`, same category as the already-approved extension). Separately, `GraphicsDevice`'s own constructor turned out to unconditionally push `BlendState`/`DepthStencilState`/`RasterizerState`/viewport defaults, forcing `D9-60`/`D9-61`/`D9-62` in immediately (real `D3DRS_*` `SetRenderState()` sequences) — no device could otherwise finish constructing. Also found 4 more silently-empty `IGraphicsBackend` virtuals `D9-11`'s own grep missed (multi-line `{}` defaults). Verified no regression on EasyGL (34 gtest+CTest checks, including 5 resize/reset-specific ones). |
| `bf26d7d1` | **Phase D9-2 fully closed** (`D9-20`–`D9-23`): new `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations` + a 28-check `D3D9_Common` CTest, mutation-verified. Two non-obvious, easy-to-get-backwards findings, both verified against Microsoft's own published D3D9→DXGI legacy-format table rather than assumed: `SurfaceFormat::Color` → `D3DFMT_A8B8G8R8` (not the superficially-obvious `A8R8G8B8`), and `Rgba1010102` → `D3DFMT_A2B10G10R10` (not `A2R10G10B10`, which has no real DXGI equivalent at all). `TextureFilter` needed a new `{min,mag,mip}` triple struct, not a single enum, since D3D9 has no composed filter value. One row (`D9-21`) is 🟨: the mapping table is done, but its own "pixel-test `D3DCULL` against the oracle" obligation is honestly deferred to `D9-84` (no draw path exists yet to test it with). |
| `1a3ca71f` | **Phase D9-1 fully closed** (`D9-10`/`D9-11`/`D9-12`): D3D9 wired into `CMakeLists.txt` (6 of 7 `"D3D12"` sites, correcting a stale plan claim about the 7th — see `plan_dx9.md`'s `D9-10` row); new `D3D9GraphicsBackend` skeleton + shared `NotYetImplemented.hpp`; `GraphicsDevice.cpp` audited, zero changes needed. `CNA_GRAPHICS_BACKEND=D3D9` configures and builds clean; a runtime check confirms the skeleton's real bookkeeping methods work and its throwing methods actually throw. |
| `09121309` | **Phase D9-0 fully closed** (`D9-2`–`D9-5`): confirmed `d3d9`-alone link set (no `dxguid`); a real Wine+DXVK D3D9 device/swap-chain/`Clear`/`Present`/`GetRenderTargetData`/`LockRect` round-trip with an exact pixel match plus a full `D3DCAPS9` dump (`vs_3_0`/`ps_3_0`, `NumSimultaneousRTs=4`, 16384 max texture size, DXVK reports unconditional NPOT support — flagged as provisional/synthetic, not an authentic XNA-era driver's caps); confirmed `D3DPOOL_MANAGED` textures are genuinely `LockRect`-readable and survive `Reset()` with no re-upload (so `Texture2D::GetData()` can be a plain `LockRect` later, `D9-52`); and a new `scripts/run-wine-dxvk9.sh` (mirrors `run-wine-dxvk.sh`'s DXVK-marker gate under new `CNA_D3D9_*` env-var names), proven both ways — passes against the real `~/.wine-cna-d3d11` DXVK prefix, and correctly fails (exit 3) against a freshly-initialized, DXVK-less prefix that silently fell back to WineD3D. |
| `59a35d4c` | Recorded the project owner's two 2026-07-14 decisions in `plan_dx9.md`: implementation authorized through Phase D9-13, and the `IGraphicsBackend` boundary problem resolved via an approved additive extension. |
| `d1ae928f` | Added `plan_dx9.md` and the proven Phase D9-0 spike artifacts (`dx9-spike/`: shader compiler, `.fxb` bytecode oracle, real XNA 4.0 reference renderer) to the `feature/dx9` worktree. |

---

## 4. Current blocker / main problem

**No blocker.** Phases D9-0/D9-1/D9-2/D9-3/D9-4 are fully closed (D9-32/D9-34 honestly 🟨 — see their
own plan rows for exactly what's deferred and why). Phase D9-6: `D9-60`/`D9-61`/`D9-62` were forced in
early (real) as a side effect of D9-30's own work; `D9-63`/`D9-64` remain open. Phase D9-5:
`D9-50`/`D9-51`/`D9-52` (2D/cube/volume textures) are now closed; `D9-53`–`56` (render targets, MRT,
occlusion queries, NPOT capability surfacing) remain open. Next smallest task: `D9-53`
(`D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend`) — it's the next sequential row in Phase D9-5
and, per its own plan note, is exactly what the `D9-40` device-lost registry (`D3DPOOL_DEFAULT`) must
release/recreate, so wire it in as it lands, not afterwards.

---

## 5. Known bugs and limitations

None yet specific to this backend — no backend code exists. See `plan_dx9.md`'s "CNA's divergences
from XNA 4.0" for the six pre-existing, cross-cutting CNA-vs-XNA fidelity gaps this plan will measure
(not fix) once Phase D9-A's oracle is complete.

---

## 6. Architecture notes

### Main modules (D3D9-relevant)

| Layer | Location | Notes |
|---|---|---|
| Backend contracts | `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` | Being extended additively (approved) for D3D9's needs — see `plan_dx9.md`. |
| **D3D9 backend** | `include/\|src/CNA/Internal/Backends/D3D9/` | **Not yet created.** Windows-only, MinGW-w64 cross-compiled, own format/state/vertex-declaration mapping (not `D3DCommon`). |
| Vendored XNA stock effects | `src/CNA/Internal/Backends/D3D9/shaders/xna/` (destination) | Microsoft's `.fx`/`.fxh`, verbatim, MS-PL. |
| Spike artifacts (temporary) | `dx9-spike/` | Proven Phase D9-0 code, being moved into the real tree task by task. |

### Critical invariants (do not break these)

Same project-wide invariants as `plan_dx.md`'s `NEXT.md` used to list (Doxygen/SPDX/NOXNA/property
convention/stride-keyed vertex layout/etc.) — see `CLAUDE.md` and `CHECKLIST.md`, not repeated here.
D3D9-specific invariants (from `plan_dx9.md` design decisions): plain D3D9 not D3D9Ex;
`D3DPOOL_MANAGED` for user resources; Microsoft's `.fx`/`.fxh` sources are never edited; shader
targets stay `vs_2_0`/`ps_2_0` for stock effects (never "upgraded" to SM3); no D3DX linked, ever.

### FNA / XNA reference

Authoritative behavioral reference for this backend is **not** FNA (FNA has no D3D9 driver) — it is
XNA itself, in two forms: Microsoft's Stock Effects HLSL sources
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/`) for the shaders, and the
real XNA 4.0 runtime under Wine (`~/.wine-cna-xna40`, `dx9-spike/xna-oracle/`) for behavior.

---

## 7. Useful commands

```bash
# Wine prefixes (see dx9-spike/README.md for full detail)
~/.wine-cna-d3d9-spike   # real Microsoft d3dcompiler_47.dll -- shader compile work ONLY
~/.wine-cna-xna40        # real XNA 4.0 (win32, .NET 4.0, in-prefix csc.exe) -- the oracle
~/.wine-cna-d3d11        # D3D9 RUNTIME device tests use this one too (its own dxvk-setup install
                         # already wires d3d9.dll to DXVK) -- do not touch its D3D11/D3D12 CTest role

# Run a D3D9 .exe under Wine+DXVK, with the DXVK-marker gate (mirrors run-wine-dxvk.sh's DX-85 gate)
scripts/run-wine-dxvk9.sh path/to/some_d3d9_test.exe
# Override the prefix (defaults to ~/.wine-cna-d3d11): CNA_D3D9_WINEPREFIX=...
# Bypass the DXVK gate for a deliberate non-DXVK diagnostic: CNA_D3D9_ALLOW_WINED3D=1
# Skip the gate for a binary that never opens a device (e.g. a future D3D9_Common): CNA_D3D9_SKIP_DXVK_GATE=1

# Once D9-10 lands (CMake wiring), the configure command will mirror D3D11's:
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D9 -DCNA_BUILD_TESTS=ON
```

---

## 8. Next smallest tasks

**Phases D9-0/D9-1/D9-2/D9-3/D9-4 are all closed** (`D9-32`/`D9-34` honestly 🟨 — see `plan_dx9.md`
for exactly what's deferred to `D9-100`/`D9-A`/`D9-140` and why). Phase D9-6: `D9-60`/`D9-61`/`D9-62`
closed (forced in early). Phase D9-5: `D9-50`/`D9-51`/`D9-52` (2D/cube/volume textures) closed.

1. **`D9-53`** — `D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend` (`D3DUSAGE_RENDERTARGET`,
   `D3DPOOL_DEFAULT`, depth-stencil surface, MSAA via `CheckDeviceMultiSampleType`). These are
   `D3DPOOL_DEFAULT` → exactly what the `D9-40` device-lost registry (`ID3D9DefaultPoolResourceEXT`)
   must release/recreate — wire them in as they land, don't reinvent the registry.
2. **`D9-54`** — MRT via `SetRenderTarget(i, surface)`, capped at `D3DCAPS9::NumSimultaneousRTs`,
   same-bit-depth check, over-request throws (needs `D9-53` first).
3. **`D9-55`** — `D3D9OcclusionQueryBackend` (`D3DQUERYTYPE_OCCLUSION`).
4. **`D9-56`** — NPOT handling driven by `D3DPTEXTURECAPS_POW2`/`NONPOW2CONDITIONAL` (authenticity,
   not a limitation to hide — feeds Phase D9-10's Reach/HiDef capability table).
5. **`D9-63`** — `ApplySamplerState` (now meaningful — `D9-50` textures exist).
6. **`D9-64`** — reuse the backend-agnostic `easygl_blendstate_*`/`easygl_depthstencilstate_*`/
   `easygl_rasterizerstate_*` CTest sources verbatim (needs a real draw path, `D9-82`, to mean
   anything — likely sequenced after Phase D9-8, not literally next).
7. Then Phase D9-7 (Microsoft's stock effects: vendor, compile, embed — `D9-70`–`74`).

See `plan_dx9.md`'s "Execution order" table for the full sequence beyond this.

---

## 9. Do not do yet

- **Do not fix any of the six CNA-vs-XNA divergences** (`plan_dx9.md`'s own section) from inside this
  branch — measure with the oracle, report, propose to the project owner for a `plan_graphics.md`
  task. Never "just add the flag while in there."
- **Do not start Phase D9-11 (custom `ShaderEffect`)** without asking first — explicitly flagged
  optional/ask-first in `plan_dx9.md`'s execution order.
- **Do not edit Microsoft's vendored `.fx`/`.fxh` files**, ever, for any reason (`D9-70`).
- **Do not "upgrade" stock effects to `vs_3_0`/`ps_3_0`** because the hardware supports it.
- **Do not widen an oracle tolerance to turn a red test green** (`D9-A4`) — that silently converts
  this from an authenticity project into a parity project.
- **Do not touch `GpuDrawParams`, `D3DCommon/`, `D3D11/`, or `D3D12/`** — still off-limits regardless
  of branch state (cross-cutting or another backend's active territory).
- **Do not touch `IGraphicsBackend.hpp` beyond the approved additive extension** (new
  `GraphicsBackendCreateArgs` fields + the one device-event channel) — nothing else, no drive-by
  refactors.
- **Do not bundle multiple task numbers into one commit** — one task per commit, staged by explicit
  filename (never `git add -A`/`.`).
- **Do not claim indistinguishability from Wine+DXVK results alone** — `D3DCAPS9` under DXVK is
  synthesized, not driver-reported, and device-lost rarely fires naturally under Wine. Real hardware
  verification is `D9-140`, `needs_human`.

---

## 10. Resume prompt

```
Read NEXT.md first (this file, feature/dx9 branch), then plan_dx9.md in full before touching any
code -- this is a much stricter plan than the other CNA backends (indistinguishability from real
XNA 4.0, verified against a real oracle, not just "renders plausibly").

Implementation is authorized through Phase D9-13. The IGraphicsBackend boundary problem is resolved
(additive GraphicsBackendCreateArgs extension + device-event channel, approved 2026-07-14). Phase
D9-11 (custom ShaderEffect) still needs an explicit ask before starting. Phase D9-14 needs real
Windows hardware, out of reach here.

Pick exactly one task from Sec.8 "Next smallest tasks" (default to the first one unless told
otherwise). Inspect only the files that task names.

Make one small, verified improvement:
1. Investigate/reproduce first (run the exact command named in the task).
2. Implement the smallest correct thing per plan_dx9.md's design decisions -- do not improvise past
   what the plan already decided.
3. Where the task is a rendering/behavior claim, verify it against the real XNA 4.0 oracle
   (dx9-spike/xna-oracle/, ~/.wine-cna-xna40), not just "looks right" -- that is this plan's whole
   point.
4. Update plan_dx9.md's own task table (status + notes) with the real result.
5. Update this NEXT.md: Sec.2/Sec.3/Sec.8, following the same short-index style as the rest of the
   file -- do not let it grow into a duplicate of plan_dx9.md.
6. Commit (staged by explicit filename, one task per commit), following this repo's existing
   commit-message style (git log --oneline).

Do not start a second task in the same session unless the first is fully closed, tested, and
committed, and NEXT.md/plan_dx9.md are updated.
```
