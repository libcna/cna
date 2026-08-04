# Legacy DirectX graphics backends (DirectX 3 / 5 / 6 / 7 / 8) — feasibility analysis

> **Status: ANALYSIS ONLY — nothing is authorized or implemented by this document.** It answers a
> project-owner question: *what is DirectX 7, and could CNA gain graphics backends for the old
> DirectX 5/6/7/8 (and 3) families — at least for 2D?* It surveys the DirectX version history, maps
> each version onto the `IGraphicsBackend` contract CNA actually requires, and estimates build/runtime
> feasibility against the two backend-delivery patterns this repo already uses. It does **not** open
> tasks, edit any plan file, or change any build. Any real backend would need its own `plan_dx*.md`,
> its own owner authorization, and its own existence-gate spike — exactly as `plan_dx3.md`/`plan_dx9.md`
> already did.

---

## 0. TL;DR

- **DirectX 7 (1999)** is the last *fixed-function-only* DirectX: DirectDraw 7 + Direct3D 7, with
  hardware transform & lighting (T&L), cube environment mapping, and a fully integrated
  DirectDraw/Direct3D object model — but **no programmable shaders**. Shaders arrive one version
  later, in DirectX 8.
- **2D is feasible on every one of DX5/6/7/8**, and on DX5+ it is actually *easier and faster* than
  the existing `DX3` backend — DX5+ have hardware-accelerated textured-triangle rasterization
  (`DrawPrimitive`), so `SpriteBatch` becomes a GPU quad-batcher instead of `DX3`'s CPU compositor.
- **DirectX 3 is already done.** It ships today as the `DX3` backend (`CNA_GRAPHICS_BACKEND=DX3`),
  a complete 2D-only DirectDraw backend built on the `../free-direct` sibling reimplementation. See
  `docs/dx3-backend.md`.
- **The hard wall is programmable shaders, not "2D vs 3D."** XNA 4.0's own floor is Direct3D 9 /
  Shader Model 2.0. Every DirectX older than 9 is a *strict subset* of what XNA requires:
  - DX5/6/7: fixed-function only → 2D works; the stock effects (`BasicEffect` &c.) can be
    *emulated* via the fixed-function pipeline; **custom user `Effect`s (arbitrary HLSL) cannot run**.
  - DX8: adds Shader Model 1.x → 2D works, stock effects work, but XNA's `ps_2_0`/`vs_2_0`+ effects
    still **do not fit** SM 1.x, so custom effects mostly cannot run either.
- **An existing 3D game *can* run on DX2/3/5/6/7/8 if the backend degrades instead of throwing** (§3.2).
  CNA forwards a *semantic* effect description (`GpuDrawParams`: matrices, lights, materials, textures,
  fog, skinning), not opaque shader bytecode, and already blesses an "accept-and-ignore unsupported
  fields" pattern — so a fixed-function backend can reconstruct the stock effects from it and silently
  ignore what it can't do (custom shaders → fallback render). The only change from `DX3` is *policy*
  (best-effort vs `ThrowNo3D`); it needs **no** shared-code or XNA-layer change. Fidelity climbs with
  the version; *whether the game runs* does not.
- **If any single legacy backend were worth building, it is D3D8**, because DXVK ships a `d3d8`
  runtime (D3D8→D3D9→Vulkan), so it reuses the *exact* MinGW-cross-compile + Wine + DXVK toolchain
  the shipping `D3D9`/`D3D11` backends already prove. A "D3D7" fixed-function backend is also
  feasible but rides Wine's legacy `wined3d` (not DXVK) and can never run a shader.
- **None of these can meet the `D3D9` backend's bar** (byte-exact against real XNA 4.0). They are
  *retro / alternative* backends in the same spirit as `DX3`, `ASCII`, or `CANVAS` — interesting for
  authenticity, old hardware, and completeness, not for XNA fidelity.

---

## 1. What each DirectX version actually is

A compressed reference, focused on the *graphics* stack (DirectDraw + Direct3D), since that is all a
CNA graphics backend touches. Audio/input/networking (DirectSound/DirectInput/DirectPlay) are out of
scope — CNA's audio/input already live elsewhere and are explicitly not part of the `DX3` backend
either (`docs/dx3-backend.md`, "Known limitations").

| Version | Year | Graphics headline | Programmable shaders? | 3D pipeline |
|---|---|---|---|---|
| DirectX 1–2 | 1995–96 | DirectDraw; first Direct3D via *execute buffers* (clumsy retained/immediate hybrid) | No | Fixed-function, execute buffers |
| **DirectX 3** | 1996 | DirectDraw matures; Direct3D still execute-buffer based | No | Fixed-function |
| *(DirectX 4)* | — | **Never released** (skipped; 3 → 5) | — | — |
| **DirectX 5** | 1997 | Direct3D **`DrawPrimitive`** immediate-mode API replaces execute buffers — the modern draw-call shape begins | No | Fixed-function |
| **DirectX 6** | 1998 | Multitexturing, texture compression (DXTn), stencil buffer, tables-based fog, geometry blending | No | Fixed-function |
| **DirectX 7** | 1999 | **Hardware T&L**, cube environment maps, fully integrated DirectDraw7/Direct3D7 object model | **No** | Fixed-function (peak) |
| **DirectX 8** | 2000 | DirectDraw + Direct3D merged into **"DirectX Graphics"**; **Shader Model 1.0/1.1** (VS) and **1.0–1.4** (PS) | **Yes (SM 1.x)** | Fixed-function **+** programmable |
| **DirectX 9** | 2002–04 | **Shader Model 2.0**, then 3.0 (9.0c); HLSL matures | Yes (SM 2.0/3.0) | Programmable |

So the direct answer to *"what is DirectX 7?"*: it is the high-water mark of the **fixed-function**
era — the most capable DirectX you could target before shaders existed. It has real hardware 3D
(including hardware T&L, which DX9-era games take for granted), but every pixel and vertex is
processed by configurable-but-not-programmable hardware. `SetRenderState`/`SetTextureStageState`/
`SetLight`/`SetMaterial`, not `vs_2_0`/`ps_2_0`.

**Key framing:** XNA 4.0 was itself built on **Direct3D 9**. XNA's `Reach` profile targets SM 2.0 and
`HiDef` targets SM 3.0. So **D3D9 is the *floor* of full XNA fidelity**, and CNA's `D3D9` backend is
already at that floor (oracle-verified byte-exact vs. real XNA — see `docs/d3d9-backend.md`). Anything
older than DX9 is, by construction, *below XNA's own baseline*. That is why "a piece of CNA won't
work" (the owner's own phrasing) is not a limitation to be engineered around — it is a mathematical
property of targeting pre-shader hardware.

---

## 2. What the CNA backend contract actually requires

A backend implements `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`. Its surface splits
cleanly into a **2D subset** and a **3D/shader subset**, and the existing `DX3` backend is the proof
of exactly where that line falls — it implements the first group and throws (`ThrowNo3D`) or degrades
to `nullptr` on the second.

**2D subset — everything a `SpriteBatch`/`SpriteFont` game needs:**

- `CreateTexture`, `SetData*`/`GetData` (CPU↔texture round-trip)
- `CreateRenderTarget2D`, `SetRenderTarget2D`, MRT rejected by design
- `CreateSpriteBatch` + `Draw` (rotation / scale / flip / source-crop / transform matrix)
- `Clear` (color only), `Present`
- `SetBlendEnabled` / blend presets, `SetSamplerFilter`, `SetSamplerAddressMode` (Wrap/Mirror/Clamp)
- `SetPresentationMode`, `SetVirtualResolution`, window↔logical transforms

**3D / shader subset — everything `DX3` throws on:**

- `CreateVertexBuffer` / `CreateIndexBuffer16/32`
- `DrawColoredPrimitives`, `DrawPrimitivesEx`, `DrawIndexedPrimitivesEx`, `DrawInstancedPrimitivesEx`
- `SetDepthTestEnabled` / `SetDepthWriteEnabled`, depth/stencil `Clear*` variants
- `CreateEffectBackend` (**custom shaders** — the `Effect`/`ShaderEffect` path)
- `CreateTexture3D` / `CreateTextureCube` / `CreateRenderTargetCube`, `CreateOcclusionQuery`

The `Effect` system is the crux. The stock effects (`BasicEffect`, `DualTextureEffect`,
`AlphaTestEffect`, `EnvironmentMapEffect`, `SkinnedEffect`) and any user `Effect` are, in XNA/CNA on
the real GPU backends, **compiled shaders**. On a pre-shader backend they can only exist as
*fixed-function emulation*, and only for the stock effects whose behavior the fixed-function pipeline
happens to reproduce.

---

## 3. Capability ladder — what each version could offer CNA

Marked from CNA's perspective (what a game using the XNA API would see), not raw DirectX capability.

| Backend | 2D `SpriteBatch`/`SpriteFont` | Fixed-function 3D | Stock effects (`BasicEffect` &c.) | Custom HLSL `Effect` | Depth/stencil |
|---|---|---|---|---|---|
| **DX3** (DirectDraw) — *shipping* | ✅ CPU compositor | ❌ none | ❌ throw | ❌ throw | ❌ none |
| **DX5/6/7** (D3D immediate mode) | ✅ HW quads (better than DX3) | 🟨 fixed-function | 🟨 emulate via fixed-function | ❌ impossible (no shader stage) | ✅ real z-buffer |
| **DX8** (Direct3D 8) | ✅ HW quads | 🟨 fixed-function | 🟨 fixed-function *or* SM 1.x | ❌ XNA SM 2.0/3.0 won't fit SM 1.x | ✅ |
| **DX9** (Direct3D 9) — *shipping* | ✅ | ✅ | ✅ real XNA shaders | ✅ | ✅ |

Notes on the "emulate the stock effects" claim — it is more plausible than it first sounds, because
`BasicEffect` was *designed to mirror the fixed-function pipeline*:

- `BasicEffect`: world/view/proj transforms, up to 3 directional lights + ambient, one texture, fog,
  vertex color, alpha → maps directly onto D3D7/8 `SetTransform` / `SetLight` / `SetMaterial` /
  texture-stage states / `D3DRS_FOG*`. **Good fit.**
- `DualTextureEffect`, `AlphaTestEffect`: two texture stages / alpha test render state. **Good fit.**
- `EnvironmentMapEffect`: cube environment mapping is a real DX7+ fixed-function feature. **Partial fit.**
- `SkinnedEffect`: DX7/8 fixed-function has *indexed vertex blending* (up to 4 matrices) — a **partial**
  fit, but XNA's `SkinnedEffect` allows more bones per vertex and does it in a shader.
- **Per-pixel lighting** (`PreferPerPixelLighting`): fixed-function is per-vertex only. XNA's *default*
  is per-vertex, so the default path is reachable, but the per-pixel path is **not** without shaders.

So a fixed-function DX7 backend could plausibly render a large fraction of *stock-effect* XNA 3D
content, while being fundamentally unable to run a single line of user HLSL. That is the honest shape
of "a piece of CNA won't work."

### 3.1 How much *3D* does each real Direct3D version actually offer? (incl. DX1/2)

§3 answered "2D vs 3D vs effects." This subsection zooms into the **3D** column specifically: how much
of CNA's 3D/`Effect` contract (§2's second list) each version's *real* Direct3D can back. The amount
does **not** grow smoothly — it steps at three hard boundaries: the `DrawPrimitive` API (DX5),
hardware T&L + cube maps + skinning (DX7), and programmable shaders (DX8).

**First, the two the previous version of this doc waved off in one line:**

- **DirectX 1 (1995): zero 3D.** Direct3D did not exist yet. DX1 is DirectDraw + DirectSound +
  DirectInput + DirectPlay only. A "DX1 backend" would be *less* than `DX3` — early DirectDraw, 2D
  only, no reason to build it.
- **DirectX 2 (1996): the *first* Direct3D — but via *execute buffers*.** You describe geometry by
  filling a buffer with vertices + rendering opcodes and submitting it (`IDirect3DDevice::Execute`),
  not with draw calls. It has a z-buffer, gouraud shading, one texture, alpha blending, and
  fixed-function lighting/materials — so *minimal* real 3D exists — but the execute-buffer model is
  awkward and single-texture-limited. DX3's Direct3D is essentially the same execute-buffer 3D.

**3D-depth ladder** (the "% of CNA's 3D contract" figures are deliberately rough qualitative
estimates for intuition, **not** a measured metric):

| Version | 3D tier | What the real Direct3D backs for CNA 3D | ~share of CNA 3D |
|---|---|---|---|
| **DX1** | none | No Direct3D at all — 2D only | **0%** |
| **DX2 / DX3** | minimal, *execute buffers* | z-buffer, gouraud, **1 texture**, alpha blend, fixed-function lights/materials — but no `DrawPrimitive`, no stencil, no multitexture | **~15%** |
| **DX5** | clean fixed-function | **`DrawPrimitive`/`DrawIndexedPrimitive`** (modern draw-call model), depth, alpha blend/test, fog, 1–2 textures, software T&L — `DrawPrimitivesEx` maps here cleanly | **~30%** |
| **DX6** | full fixed-function | + **multitexturing** (stage cascade → `DualTextureEffect`), + **stencil** (stencil ops), DXTn compression, `IDirect3DVertexBuffer` objects, table fog | **~45%** |
| **DX7** | peak fixed-function | + **hardware T&L**, + **cube env maps** (→ `EnvironmentMapEffect`), + **indexed vertex blending** (→ `SkinnedEffect`, partial). Effectively **all *stock* XNA effects emulable**. Still no shaders, no volume textures, no occlusion query | **~65%** |
| **DX8** | early programmable | + **Shader Model 1.x** (VS 1.1 / PS 1.0–1.4), + **volume/3D textures** (→ `Texture3D`), + **MSAA**. But XNA effects are `ps_2_0`+ → **do not fit SM 1.x**, so `CreateEffectBackend` still throws for real XNA effects | **~80%** |
| DX9 *(ref)* | XNA parity floor | SM 2.0/3.0, occlusion queries, everything — the floor of full XNA fidelity | **100%** |

**Per-feature view** (CNA 3D contract rows × version — `~` = present but limited/partial):

| CNA 3D contract feature | DX1 | DX2/3 | DX5 | DX6 | DX7 | DX8 |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| Any Direct3D at all | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `DrawPrimitive` model (not execute buffers) | — | ❌ | ✅ | ✅ | ✅ | ✅ |
| `VertexBuffer`/`IndexBuffer` objects | — | ~ | ~ | ✅ | ✅ | ✅ |
| Depth test/write | — | ✅ | ✅ | ✅ | ✅ | ✅ |
| Stencil ops | — | ❌ | ❌ | ✅ | ✅ | ✅ |
| Fixed-function lights/materials (`BasicEffect` core) | — | ✅ | ✅ | ✅ | ✅ HW | ✅ |
| Multitexturing (`DualTextureEffect`) | — | ❌ | ~ | ✅ | ✅ | ✅ |
| Hardware T&L | — | ❌ | ❌ | ❌ | ✅ | ✅ |
| Cube maps (`EnvironmentMapEffect`) | — | ❌ | ❌ | ❌ | ✅ | ✅ |
| Skinning (`SkinnedEffect`) | — | ❌ | ❌ | ~ | ~ | ~ |
| Volume/3D textures (`Texture3D`) | — | ❌ | ❌ | ❌ | ❌ | ✅ |
| Programmable shaders (custom `Effect`) | — | ❌ | ❌ | ❌ | ❌ | ~ SM 1.x |
| MSAA | — | ❌ | ❌ | ❌ | ~ | ✅ |
| Occlusion query (`CreateOcclusionQuery`) | — | ❌ | ❌ | ❌ | ❌ | ❌ |

Two things worth calling out from the matrix:

- **Occlusion query is DX9-only.** It is absent in *every* version through DX8 — and not even a gap
  unique to legacy backends: CNA's own shipping `D3D9` and `D3D12` backends don't wire it either
  (`docs/graphics-backend-feature-matrix.md`).
- **`DX3` as shipped is 0% 3D by choice, not by DirectX limit.** DX2/3's execute-buffer Direct3D
  could back ~15% of the 3D contract in principle, but the shipping `DX3` backend deliberately does
  0% because its `../free-direct` sibling implements **DirectDraw only, no Direct3D** — so there is no
  3D device to call. Reaching that ~15% would mean adding an execute-buffer Direct3D reimplementation
  (Route A cost, §4) for a very low payoff.

The exact DirectX interface-version boundaries above (e.g. vertex-buffer objects at DX6, volume
textures at DX8, queries at DX9) are stated from known Direct3D history and should be spike-confirmed
against the actual MinGW-w64 headers before any backend work — same "pending a spike" caveat as §4/§8.

### 3.2 Best-effort 3D: would an existing 3D game actually *run* (degrade, not throw)?

The most important design question for these legacy backends is **not** "how much can they render"
(§3.1) but "does an existing CNA/XNA 3D game **run without crashing** on them, even if a lot is
ignored?" The answer is **yes**, and the reason is a specific, already-shipped property of CNA's draw
path — this is not a hypothetical.

**Key finding — CNA forwards the *semantics* of an effect, not opaque shader bytecode.** The per-draw
`IGraphicsBackend::GpuDrawParams` struct (`IGraphicsBackend.hpp`) is a fully structured description of
the stock effect: world/view/projection matrix, three directional lights (dir/diffuse/specular),
material colors, diffuse/dual/cube textures, fog, alpha-test, per-bone skinning transforms, and
boolean flags selecting the variant (`textureEnabled`, `lightingEnabled`, `dualTexture`, `envMapping`,
`skinned`, `fogEnabled`, …). It is populated by `BasicEffect::FillGpuDrawParams()` &c. Because
`BasicEffect` *is* effectively the fixed-function pipeline, a fixed-function DX5/6/7/8 backend has
everything it needs to reproduce the stock effects **with no shader at all**, by translating
`GpuDrawParams` straight into fixed-function state:

| `GpuDrawParams` field | Fixed-function mapping (DX5–8) |
|---|---|
| `worldColMajor` (+ view/proj) | `SetTransform(D3DTS_WORLD/VIEW/PROJECTION)` |
| `light{0,1,2}{Dir,Diffuse,Specular}`, `lightingEnabled` | `SetLight()` + `LightEnable()` |
| `diffuseColor`/`ambientColor`/`emissiveColor`/`specularColor`/`specularPower` | `SetMaterial()` |
| `texture0`, `textureEnabled` | `SetTexture(0)` + texture-stage states |
| `texture1`, `dualTexture` | second texture stage |
| `envMap`, `envMapping` | cube texture + reflection texcoord-gen (DX7+) |
| `fogEnabled`/`fogColor`/`fogStart`/`fogEnd` | `D3DRS_FOG*` render states |
| `alphaTest` | `D3DRS_ALPHATESTENABLE`/`ALPHAREF`/`ALPHAFUNC` |
| `boneTransforms`/`boneCount`, `skinned` | indexed vertex blending (DX7+) or CPU skinning |
| `vertexColorEnabled` | `D3DRS_COLORVERTEX` / texture-stage color arg |

**Key finding — "accept and ignore" is already a blessed pattern.** The custom-shader path is a single
nullable pointer, `GpuDrawParams::customEffectBackend`. Its own doc-comment states that backends which
don't implement it "**safely ignore it, matching the established accepted-and-ignored pattern for
other not-yet-backend-supported `GpuDrawParams` fields.**" So graceful degradation of unsupported
features is not a new concept to be invented — it is how the interface already works. A fixed-function
backend that ignores `customEffectBackend` renders that draw from the (default) stock fields — i.e.
the geometry still appears, flat/vertex-colored, instead of throwing. That is exactly the owner's
"ignore a lot, but the game runs" model.

**Consequence — the only change from `DX3` is *policy*, not architecture.** `DX3` chose to
`ThrowNo3D` on every 3D entry point (so a 3D game crashes on it). A "best-effort 3D" backend makes the
opposite choice: implement `CreateVertexBuffer`/`CreateIndexBuffer`/`DrawPrimitivesEx`/… as real
fixed-function work, and *no-op or approximate* whatever it can't do, **never throwing**. This is
entirely backend-local — the 3D methods are virtual; a backend just implements them instead of
throwing. **No shared-code or XNA-layer change is required.**

**The spectrum of "runs"** (climbs with the version — this is the real answer to the owner's question):

| Level of "runs" | Where it is reachable |
|---|---|
| **Does not crash** (every 3D call returns / no-ops instead of throwing) | DX2/3, DX5, DX6, DX7, DX8 |
| **Renders the geometry** (fixed-function from `GpuDrawParams`) | DX2/3 (execute buffers), DX5–8 cleanly |
| **Stock-effect games render ~correctly** (`BasicEffect`/`DualTexture`/`AlphaTest`/`EnvironmentMap`/`Skinned`) | DX7/DX8 fully, DX6 mostly, DX5 partially |
| **Custom-shader games render *correctly*** | DX9+ only (below it they run, but the shader is ignored → fallback image) |

The owner's bar — "an existing 3D game would run, even ignoring a lot" — is level 2–3, and it is
reachable on **all** of DX2/3/5/6/7/8. What climbs with the version is *fidelity*, not *whether it
runs*.

**What degrades badly (runs, but looks different — must be declared up front, not discovered later):**
custom HLSL effects (post-processing, custom lighting) → ignored, fallback render; per-pixel lighting
(`preferPerPixelLighting`) → per-vertex only; render-target-as-shader-input, MRT, `Texture3D` (below
DX8), and occlusion query (below DX9 → "everything visible" fallback).

**Practical shape.** Write **one shared `GpuDrawParams` → fixed-function-state translator**, reusable
across a DX5/6/7/8 family (write once), with a "degrade, don't throw" policy and a sensible
custom-shader fallback (e.g. draw with `texture0`/`diffuseColor` + gouraud so custom-shader geometry
is visible, not black). The single most feasible first target is still **D3D8** (DXVK `d3d8` path,
§4), now for a stronger reason than §7 gave: it can host that fixed-function translator *and* run
under the same proven MinGW+Wine+DXVK toolchain as the shipping backends.

---

## 4. The two delivery routes CNA already uses

Every existing DirectX-family backend in this repo was delivered one of two ways. Any new legacy
backend must pick one, and the choice dominates the feasibility.

### Route A — reimplementation sibling (how `DX3` was done)

`DX3` does **not** talk to a real DirectDraw. It links `../free-direct`, a sibling C++20
reimplementation of a *narrow* `IDirectDraw`/`IDirectDrawSurface` subset, itself built on SDL3
(`cmake/BackendSelection.cmake`, `cmake/BackendLibraries.cmake`). This is why `DX3` builds and runs
**natively on Linux** with no Windows, no Wine, no emulation.

- **Pro:** native, portable, no Wine/GPU-translation stack; total control.
- **Con:** someone has to *write the reimplementation*. `free-direct` implements DirectDraw (2D
  blitting) **only** — it has **no Direct3D** at all ("Direct3D not implemented", `docs/dx3-backend.md`).
  Delivering DX5/6/7 this way would mean reimplementing a fixed-function Direct3D immediate-mode
  device (transform/lighting/rasterization/texture stages) on top of SDL3 or OpenGL — a large,
  from-scratch software-or-GL fixed-function pipeline. That is a project in itself, well beyond a
  backend.

### Route B — real Windows headers + MinGW cross-compile + Wine translation (how `D3D9/11/12` were done)

`D3D9`/`D3D11`/`D3D12` `#include` the genuine Windows SDK headers (`d3d9.h`, `d3d11.h`, `d3d12.h`,
`dxgi.h`), which **only exist when targeting Windows** — hence the hard `FATAL_ERROR` gate in
`cmake/BackendSelection.cmake` unless building on Windows or cross-compiling with
`cmake/toolchains/mingw-w64.cmake`. The resulting `.exe` is then run on this Linux dev machine under
**Wine**, with the D3D calls translated to the host GPU:

- `D3D9`/`D3D11` → **DXVK** (D3D→Vulkan), via `scripts/run-wine-dxvk*.sh`.
- `D3D12` → **vkd3d-proton**, via `scripts/run-wine-vkd3d.sh`.

A backend's link set is minimal and version-specific: `D3D9` links just `d3d9`; `D3D11` links
`d3d11 dxgi d3dcompiler` (`cmake/BackendLibraries.cmake`).

This route is what makes the legacy question tractable **without** writing a reimplementation — *if*
a translation layer exists for that DirectX version. That "if" is the whole game:

| Version | MinGW-w64 header available? | Wine/host translation | Route-B feasibility |
|---|---|---|---|
| **D3D8** | `d3d8.h` (yes) | **DXVK ships `d3d8.dll`** (D3D8→D3D9→Vulkan, merged from D8VK in DXVK 2.0) | **High — same pattern as `D3D9`** |
| **D3D7 / 6 / 5** | `ddraw.h`, `d3d.h` (immediate-mode) (yes) | **Wine's built-in `ddraw.dll` + `wined3d`** (fixed-function → OpenGL); DXVK does **not** cover DDraw/D3D≤7 | Medium — legacy `wined3d` path, less-proven, fixed-function only |
| **DX3 (DirectDraw)** | `ddraw.h` (yes) | Wine `ddraw`, *or* Route A (chosen) | Already shipped via Route A |

> The header/library availability above is the expected MinGW-w64 state and should be spike-verified
> before any real work — exactly as `plan_dx9.md`'s "D9-0 existence gate" spiked `d3d9.h` + real XNA
> before a line of backend code was written. This document deliberately does **not** claim those
> spikes have been run.

---

## 5. Per-version feasibility

### DirectX 3 — **already shipping** (`DX3`)
Nothing to analyze; it exists. Complete 2D-only DirectDraw backend via `free-direct`, Route A, native
Linux, `ThrowNo3D` on everything 3D. Full status in `docs/dx3-backend.md` and `plan_dx3.md`. If the
owner's "a co DirectX 3?" was asking whether it *could* be done — it is done. If it was asking whether
the *analysis* here changes anything for it — it does not.

### DirectX 8 — **most feasible new backend**
- **Route B, DXVK `d3d8`.** Cross-compile the backend with MinGW-w64 against `d3d8.h`, run under Wine
  with DXVK's `d3d8.dll`. This is the **same delivery mechanism** the shipping `D3D9`/`D3D11` backends
  already prove works on this machine — the lowest-novelty path of any option here.
- **Reuse:** could share `src/CNA/Internal/Backends/D3DCommon` (format/state/vertex-format mapping)
  the way `D3D11`/`D3D12` do — `D3D9` deliberately does not, but a fixed-function-ish D3D8 backend is
  closer to `D3D9`'s shape.
- **Capability:** full 2D `SpriteBatch`/`SpriteFont`; fixed-function stock-effect 3D; **but XNA's
  `ps_2_0`/`vs_2_0`+ custom effects will not compile/run** on SM 1.x — `CreateEffectBackend` would
  have to throw for anything above SM 1.x, i.e. essentially all real XNA effects. So D3D8 buys "2D +
  fixed-function 3D," meaningfully more than `DX3`, but is **not** a shader backend in any useful XNA
  sense.
- **Verdict:** the natural candidate *if* the goal is "one more retro DirectX backend, cheaply."

### DirectX 5 / 6 / 7 — **one fixed-function backend, not three**
- DX5, DX6, DX7 share the same immediate-mode, fixed-function `IDirect3DDevice{2,3,7}` model. Their
  differences (DX5: `DrawPrimitive`; DX6: multitexture/DXTn/stencil; DX7: hardware T&L/cube maps) are
  *features within* the fixed-function pipeline, not different CNA-facing contracts. Building three
  separate backends would be near-duplicate effort. **If pursued at all, target one — realistically
  "D3D7"** (the most capable, `IDirect3DDevice7`), and treat DX5/6 as "the same backend with fewer
  render states available."
- **Route B, Wine `wined3d`.** MinGW has `ddraw.h`/`d3d.h`; Wine's built-in `ddraw`→`wined3d`→OpenGL
  runs it. This is a *different, less-exercised* translation path than DXVK (DXVK does not do DDraw),
  so it carries more environment risk than the D3D8 path.
- **Route A** would mean building a fixed-function Direct3D reimplementation in `free-direct` (or a new
  sibling) — large, and explicitly out of `free-direct`'s current scope.
- **Capability:** full 2D (HW-accelerated, better than `DX3`); fixed-function stock-effect 3D
  (`BasicEffect`-class, as in §3); **no custom effects ever**.
- **Verdict:** technically feasible for 2D + fixed-function 3D, but the highest effort-to-payoff of
  the options, and on the least-proven runtime path.

### DirectX 1 / 2 / 4 — not worth analyzing
DX1 has **no Direct3D at all** (2D only — less than `DX3`); DX2 has only execute-buffer Direct3D
(pre-`DrawPrimitive`, single-texture, ~15% of the 3D contract — see §3.1), strictly worse than DX5
with no CNA-facing benefit; DX4 was never released. No reason to target any of them.

---

## 6. The owner's specific question: "would at least 2D work?"

**Yes — unambiguously, for all of DX5/6/7/8, and it would be a *better* 2D backend than `DX3`.**

`DX3`'s 2D is genuinely hard: DirectDraw has no triangle rasterizer, so the backend carries a CPU
compositor (`CompositeQuad`, an edge-function rasterizer) to implement rotation/scale/blend/sampling —
see `docs/dx3-backend.md` §3. Every DirectX from 5 onward has **hardware-accelerated textured-triangle
`DrawPrimitive`**, so `SpriteBatch` collapses to "batch quads → two triangles each → one draw call
with alpha blending + a sampler state," which is exactly what the GPU backends already do. The 2D
feature set CNA needs (blend presets, Wrap/Mirror/Clamp sampling, source-crop, rotation, flip,
`SpriteFont`) is all expressible in fixed-function render/texture-stage states.

So the 2D answer is the *opposite* of a concern: 2D is the easy, well-supported part on every legacy
version. The part that "won't work" is programmable shaders (custom `Effect`s), and for DX5/6/7 also
per-pixel-lit / shader-only stock-effect paths.

---

## 7. Recommendation (analysis-level only)

Ranked by value-for-effort, purely as input to an owner decision — **no task is opened here**:

1. **If the appetite is "one more authentic retro backend, cheaply": a `D3D8` backend.** It reuses the
   proven MinGW + Wine + **DXVK** toolchain (DXVK's `d3d8`), can share `D3DCommon`, and delivers 2D +
   fixed-function 3D — strictly more than `DX3`. Custom XNA effects remain out of reach (SM 1.x wall),
   which must be stated up front, not discovered later.
2. **A single fixed-function `D3D7` backend** is feasible for 2D + fixed-function stock effects, but
   rides Wine's legacy `wined3d` (not DXVK), never runs a shader, and is the most work. Reasonable only
   if authentic DX7-era output is itself the goal.
3. **DX5/DX6 as separate backends: not recommended** — collapse them into the DX7 fixed-function
   backend if that path is ever taken.
4. **DX3: already done.** No action.

Whatever is chosen, it should follow the exact discipline `plan_dx9.md`/`plan_dx3.md` set:

- **Run an existence-gate spike first** (does MinGW ship the header? does DXVK-`d3d8`/Wine-`wined3d`
  render a triangle under Wine on this machine?) — *before* authorizing backend code.
- **Be explicit that these cannot meet the `D3D9` oracle bar.** They are retro/alternative backends
  (peer to `DX3`/`ASCII`/`CANVAS`), validated by their own 2D pixel checks and fixed-function behavior,
  **not** by byte-exact equality with real XNA 4.0.
- **Keep changes backend-local.** As with the WebGPU work, don't perturb the established backends; a
  shared-interface change is justified only if genuinely required and verified across the others.

---

## 8. Honest non-goals and caveats

- **No spikes were run for this document.** MinGW header availability, DXVK-`d3d8` behavior, and Wine
  `wined3d` DDraw/D3D7 rendering are stated from their known upstream status, not verified here. Treat
  every Route-B feasibility mark as "expected, pending a spike."
- **Custom shaders are the permanent wall below DX9.** No amount of backend work makes a `ps_2_0` XNA
  effect run on DX3/5/6/7 (no shader stage) or DX8 (SM 1.x only). This is intrinsic to the hardware
  era, not a CNA gap.
- **Audio/input stay out of scope**, exactly as they are for `DX3` — this analysis is graphics-only.
- **Cross-platform reality:** Route B validates under Wine on *this* Linux dev machine; real behavior
  on native Windows (or macOS) is unverified, the same caveat every `D3D9`/`D3D11`/`D3D12` result in
  this repo already carries.

---

## 9. See also

- `docs/dx3-backend.md` — the shipping DX3 (DirectDraw) 2D backend; the concrete model for "2D-only,
  throw on 3D."
- `docs/d3d9-backend.md`, `docs/d3d9-divergence-report.md` — the D3D9 backend and the byte-exact XNA
  oracle bar that legacy backends cannot meet.
- `docs/graphics-backend-feature-matrix.md` — cross-backend feature matrix and the Wine+DXVK/vkd3d
  verification caveat.
- `plan_dx3.md`, `plan_dx9.md`, `plan_dx.md` — the existing DirectX-family plans and their
  existence-gate discipline any legacy plan should copy.
- `cmake/BackendSelection.cmake`, `cmake/BackendLibraries.cmake`, `cmake/toolchains/mingw-w64.cmake` —
  the backend-selection, link-set, and cross-compile wiring a new backend would extend.
</content>
</invoke>
