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
| `cmake-build-d3d9` | D3D9 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-14**: `cmake -DCNA_GRAPHICS_BACKEND=D3D9 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_BUILD_TESTS=ON` configures; `CNA`/`cna_backend_graphics_d3d9`/`cna_test_d3d9_common`/`cna_test_d3d9_smoke` all build clean. `D3D9_Common` 28/28 + `D3D9_Smoke` 12/12 pass via `ctest --test-dir cmake-build-d3d9 -R D3D9`. A real device now creates, clears, presents, and reads back pixels through the actual public `Game`/`GraphicsDeviceManager`/`GraphicsDevice` API. |

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

### Phase D9-3 — device, present, device-lost: D9-30/D9-31 CLOSED, D9-33 mechanism CLOSED, D9-32/D9-34 open

| Task | Status |
|---|---|
| `D9-30` — real `Direct3DCreate9`/`GetDeviceCaps`/`CreateDevice` with real presentation parameters | ✅ |
| `D9-31` — `Clear` + all 6 `Clear*` combos + `Present` + `ReadBackbuffer`, each pixel-verified | ✅ (12/12 `D3D9_Smoke` checks) |
| `D9-32` — enforce `GraphicsProfile` floor at construction | ⬜ next |
| `D9-33` — window resize via device `Reset()` | 🟨 (mechanism real and proven; dedicated test still owed) |
| `D9-34` — XNA device-lost lifecycle | ⬜ (channel exists, `TestCooperativeLevel` loop not yet implemented) |

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

### Does NOT work yet

Buffers, textures, draws, `SpriteBatch` (Phase D9-4 onward) — all still throw `NotYetImplemented()`
naming their own follow-up task, by design. `D9-32` (profile enforcement) and `D9-34` (real device-lost
recovery loop) are the remaining open rows in Phase D9-3. `D9-63` (sampler state) and `D9-64` (reused
state CTests) are the remaining open rows in Phase D9-6. The mapping tables (`D9-20`–`23`) are now
partly consumed (by the render-state push path); buffer/texture/draw consumers still come later.

---

## 3. Recent changes

Most recent first. Full detail lives in `plan_dx9.md` — this is a short index.

| Commit(s) | Summary |
|---|---|
| *(pending)* | **`D9-30`/`D9-31` closed + `D9-33`'s resize mechanism + Phase D9-6's `D9-60`/`D9-61`/`D9-62` forced in early**: real `Direct3DCreate9`/`CreateDevice` using the game's actual requested back-buffer/depth-stencil format (the approved `GraphicsBackendCreateArgs` extension, finally consumed for real); all 6 `Clear*` combos + `Present` + `ReadBackbuffer` pixel-verified (`D3D9_Smoke` 12/12); a real `EnsureDeviceSize()` resize-via-`Reset()` mechanism (proven working, not theoretical — it's what makes the smoke test converge to the requested 64×64 size at all). Two real, unplanned findings fixed in place: DXVK genuinely rejects `SurfaceFormat::Color`'s own `D3DFMT_A8B8G8R8` as a *swap-chain* format (a real D3D9 display-format restriction, fixed with a back-buffer-specific substitution to `A8R8G8B8`); and `GraphicsDevice::Reset()` never forwarded updated presentation settings to an already-constructed backend, fixed with one more small additive `IGraphicsBackend` method (`UpdatePresentationFormatEXT`, same category as the already-approved extension). Separately, `GraphicsDevice`'s own constructor turned out to unconditionally push `BlendState`/`DepthStencilState`/`RasterizerState`/viewport defaults, forcing `D9-60`/`D9-61`/`D9-62` in immediately (real `D3DRS_*` `SetRenderState()` sequences) — no device could otherwise finish constructing. Also found 4 more silently-empty `IGraphicsBackend` virtuals `D9-11`'s own grep missed (multi-line `{}` defaults). Verified no regression on EasyGL (34 gtest+CTest checks, including 5 resize/reset-specific ones). |
| `bf26d7d1` | **Phase D9-2 fully closed** (`D9-20`–`D9-23`): new `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations` + a 28-check `D3D9_Common` CTest, mutation-verified. Two non-obvious, easy-to-get-backwards findings, both verified against Microsoft's own published D3D9→DXGI legacy-format table rather than assumed: `SurfaceFormat::Color` → `D3DFMT_A8B8G8R8` (not the superficially-obvious `A8R8G8B8`), and `Rgba1010102` → `D3DFMT_A2B10G10R10` (not `A2R10G10B10`, which has no real DXGI equivalent at all). `TextureFilter` needed a new `{min,mag,mip}` triple struct, not a single enum, since D3D9 has no composed filter value. One row (`D9-21`) is 🟨: the mapping table is done, but its own "pixel-test `D3DCULL` against the oracle" obligation is honestly deferred to `D9-84` (no draw path exists yet to test it with). |
| `1a3ca71f` | **Phase D9-1 fully closed** (`D9-10`/`D9-11`/`D9-12`): D3D9 wired into `CMakeLists.txt` (6 of 7 `"D3D12"` sites, correcting a stale plan claim about the 7th — see `plan_dx9.md`'s `D9-10` row); new `D3D9GraphicsBackend` skeleton + shared `NotYetImplemented.hpp`; `GraphicsDevice.cpp` audited, zero changes needed. `CNA_GRAPHICS_BACKEND=D3D9` configures and builds clean; a runtime check confirms the skeleton's real bookkeeping methods work and its throwing methods actually throw. |
| `09121309` | **Phase D9-0 fully closed** (`D9-2`–`D9-5`): confirmed `d3d9`-alone link set (no `dxguid`); a real Wine+DXVK D3D9 device/swap-chain/`Clear`/`Present`/`GetRenderTargetData`/`LockRect` round-trip with an exact pixel match plus a full `D3DCAPS9` dump (`vs_3_0`/`ps_3_0`, `NumSimultaneousRTs=4`, 16384 max texture size, DXVK reports unconditional NPOT support — flagged as provisional/synthetic, not an authentic XNA-era driver's caps); confirmed `D3DPOOL_MANAGED` textures are genuinely `LockRect`-readable and survive `Reset()` with no re-upload (so `Texture2D::GetData()` can be a plain `LockRect` later, `D9-52`); and a new `scripts/run-wine-dxvk9.sh` (mirrors `run-wine-dxvk.sh`'s DXVK-marker gate under new `CNA_D3D9_*` env-var names), proven both ways — passes against the real `~/.wine-cna-d3d11` DXVK prefix, and correctly fails (exit 3) against a freshly-initialized, DXVK-less prefix that silently fell back to WineD3D. |
| `59a35d4c` | Recorded the project owner's two 2026-07-14 decisions in `plan_dx9.md`: implementation authorized through Phase D9-13, and the `IGraphicsBackend` boundary problem resolved via an approved additive extension. |
| `d1ae928f` | Added `plan_dx9.md` and the proven Phase D9-0 spike artifacts (`dx9-spike/`: shader compiler, `.fxb` bytecode oracle, real XNA 4.0 reference renderer) to the `feature/dx9` worktree. |

---

## 4. Current blocker / main problem

**No blocker.** Phases D9-0/D9-1/D9-2 are fully closed. Phase D9-3: `D9-30`/`D9-31` are closed and
`D9-33`'s resize mechanism is real and proven; `D9-32` (profile enforcement) and `D9-34` (real
device-lost recovery loop) remain. Phase D9-6: `D9-60`/`D9-61`/`D9-62` were forced in early (real) as
a side effect of D9-30's own work; `D9-63`/`D9-64` remain. Next smallest task: `D9-32`.

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

**Phases D9-0/D9-1/D9-2 closed. Phase D9-3: `D9-30`/`D9-31` closed, `D9-33` mechanism closed. Phase
D9-6: `D9-60`/`D9-61`/`D9-62` closed (forced in early).**

1. **`D9-32`** — enforce the `GraphicsProfile` floor at construction: reject a device below the
   requested profile's real `D3DCAPS9` with a specific diagnostic, not a deferred shader-creation
   failure. `graphicsProfile` already flows into the backend via `GraphicsBackendCreateArgs`
   (`D9-30`) — this task is purely about consuming it.
2. **`D9-33` (remaining half)** — a dedicated resize CTest asserting post-resize backbuffer
   dimensions/content directly (the resize *mechanism* itself, `EnsureDeviceSize()`, is already real
   and is what made `D9-31`'s own smoke test converge to 64×64 — see `plan_dx9.md`'s `D9-33` row).
3. **`D9-34`** — XNA's real device-lost lifecycle (`TestCooperativeLevel`/`D3DERR_DEVICELOST`/
   `DeviceLost`/`DeviceResetting`/`DeviceReset`, `D3DPOOL_MANAGED` resources surviving `Reset()`
   untouched — `D9-4` already confirmed this really works under DXVK). The notification channel
   (`BackendDeviceEvent`/`deviceEventCallback`) already exists (`D9-30`) but is not yet wired to a
   real `TestCooperativeLevel()` polling loop — that loop itself is this task's own remaining work.
4. **`D9-63`** — `ApplySamplerState` (needs `D9-50` textures to be meaningful; can also land as pure
   render-state plumbing first, same pattern as `D9-60`–`62`).
5. **`D9-64`** — reuse the backend-agnostic `easygl_blendstate_*`/`easygl_depthstencilstate_*`/
   `easygl_rasterizerstate_*` CTest sources verbatim (needs a real draw path, `D9-82`, to mean
   anything — likely sequenced after Phase D9-8, not literally next).
6. Then Phase D9-4 (buffers: `D9-40`–`42`).

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
