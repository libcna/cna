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
| `cmake-build-d3d9` | D3D9 (Windows cross-compile, MinGW-w64) | Not yet created — `D9-10` (CMake wiring) has not landed. |

### Phase D9-0 — feasibility spikes (done, in `dx9-spike/`, being formalized into the real tree)

| Task | Status |
|---|---|
| `D9-1` — real Microsoft `d3dcompiler_47.dll` compiles all 66/66 stock-effect entry points | ✅ |
| `D9-73` — 61/66 byte-identical to Microsoft's shipped `.fxb`; decision made (CNA compiles its own) | 🟨 (decided; 5 `PixelLighting` variants still need oracle-proof, `D9-73`'s own obligation) |
| `D9-A1`/`D9-A2` — real XNA 4.0 runs under Wine and renders a verified `CornflowerBlue` triangle | ✅ |
| `D9-2` — confirm minimum link set (`d3d9` alone) | ⬜ next |
| `D9-3` — Wine+DXVK D3D9 loop end-to-end (device/clear/present/readback/`D3DCAPS9` dump) | ⬜ |
| `D9-4` — `D3DPOOL_MANAGED` `LockRect`/`Reset()` behavior under DXVK | ⬜ |
| `D9-5` — `scripts/run-wine-dxvk9.sh` (new script, DXVK-marker gate) | ⬜ |

### Does NOT work yet

Everything — no `D3D9GraphicsBackend` code exists in `src/`/`include/` yet. This section will track
real gaps once Phase D9-1 (CMake skeleton) lands.

---

## 3. Recent changes

Most recent first. Full detail lives in `plan_dx9.md` — this is a short index.

| Commit(s) | Summary |
|---|---|
| `59a35d4c` | Recorded the project owner's two 2026-07-14 decisions in `plan_dx9.md`: implementation authorized through Phase D9-13, and the `IGraphicsBackend` boundary problem resolved via an approved additive extension. |
| `d1ae928f` | Added `plan_dx9.md` and the proven Phase D9-0 spike artifacts (`dx9-spike/`: shader compiler, `.fxb` bytecode oracle, real XNA 4.0 reference renderer) to the `feature/dx9` worktree. |

---

## 4. Current blocker / main problem

**No blocker.** Both of this plan's original gating questions are resolved (see the banner above and
`plan_dx9.md`'s top banner / "The `IGraphicsBackend` boundary problem" section). Next work is
finishing Phase D9-0's remaining spikes (`D9-2` through `D9-5`), then Phase D9-1 (CMake integration +
backend skeleton).

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
~/.wine-cna-d3d11        # DO NOT TOUCH -- D3D11/D3D12 CTest prefix, builtin compiler can't do SM2/SM3

# Once D9-10 lands (CMake wiring), the configure command will mirror D3D11's:
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D9 -DCNA_BUILD_TESTS=ON
```

---

## 8. Next smallest tasks

1. **`D9-2`** — confirm the minimum link set empirically (expect `d3d9` alone, no `dxguid`).
2. **`D9-3`** — prove the Wine+DXVK D3D9 loop end-to-end (device/clear/present/readback), log the full
   `D3DCAPS9`.
3. **`D9-4`** — confirm `D3DPOOL_MANAGED` textures are genuinely `LockRect`-readable under DXVK and
   survive `Reset()` (decides how big `Texture2D::GetData()` needs to be later, `D9-52`).
4. **`D9-5`** — write `scripts/run-wine-dxvk9.sh` (new file, mirrors `run-wine-dxvk.sh`'s structure and
   `DX-85` DXVK-marker gate), prove the gate fires on a deliberately-broken prefix.
5. Then Phase D9-1 (`D9-10`/`D9-11`/`D9-12`): CMake wiring (7 sites), `D3D9GraphicsBackend` skeleton,
   `GraphicsDevice.cpp` `#ifdef` audit.

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
