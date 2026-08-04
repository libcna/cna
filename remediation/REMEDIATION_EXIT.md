# REMEDIATION_EXIT.md — post-audit remediation exit reconciliation

> ## STATUS: **EXIT BLOCKED — THIS IS NOT A CHECKPOINT**
>
> One checkpoint blocker remains: **`WEBGPU-115`**. No checkpoint tag was created, and none may be
> created until it is resolved. Everything below is the exit-candidate record, not a certification.

---

## 1. Checkpoint identity

| Field | Value |
|---|---|
| Repository | `cnaaudit` worktree of the CNA repository |
| Branch | `feature/audit` |
| Reconciled at commit | `099b03c0` (`docs(remediation): complete GFX-209 baseline cleanup`) |
| Candidate commit | **none — blocked**; this document's own commit is a reconciliation record, not a checkpoint |
| Checkpoint tag | **not created** (see §8) |
| Campaign framing | Repository-wide audit frozen 2026-07 (6 passes, 2297 files, 14 backends) → remediation campaign 2026-07-19 … 2026-08-04 |
| Audit scope of record | 686 raw per-file findings + 6 synthesis documents → **105 remediation tasks + 15 accepted no-action items** |
| Executed scope | The 105 above, plus **141 findings discovered during remediation** (`REMEDIATION_PROGRESS.md` §"Discovered during remediation") — **100 of the 141 are DONE** |
| Backend scope of this checkpoint | **10 measured** — EasyGL, Software, Vulkan, bgfx, WebGPU, SDL_GPU, Headless (native, `:101`); D3D9 + D3D11 (Wine/DXVK, `:99`); **D3D12 cross-builds only, no runtime** (`REMED-BUILD-012`) |
| Explicitly NOT in scope | The 19 pending integration branches and their backends; the 43-backend long-term roadmap |

`audit/` is untouched by this reconciliation and by the whole GFX-2xx cluster; its last commit
remains `74ebf356`.

---

## 2. Exit criteria

**Completion here is milestone-bounded, not zero-backlog bounded.** This checkpoint certifies that
the audit-driven remediation campaign reached a safe, traceable resting point — it does **not**
claim the CNA backlog is empty, that all 43 planned backends exist, or that every deferred ticket
is unimportant.

| # | Criterion | State |
|---|---|---|
| E1 | No known unblocked CRITICAL | ✅ The audit's single CRITICAL (`REMED-CONTENT-001`) is DONE |
| E2 | No known supported-path crash, memory corruption or silent data loss | ✅ See §5 for the two aborts that remain, both test-owned and classified |
| E3 | **No unresolved supported-path silent wrong result classified as a blocker** | ❌ **FAILS — `WEBGPU-115`** (§3) |
| E4 | Principal backend baselines classified | ✅ 7 native + 2 Wine, §6 |
| E5 | Deferred work safely bounded and traceable | ✅ Every deferred ticket has an ID, a reason and a target plan (§4) |
| E6 | `plan_postaudit.md` authoritative for deferred work | ✅ |
| E7 | Branch inventory recorded dynamically | ✅ `INTEGRATION_BRANCH_INVENTORY.md`, derived from refs at `099b03c0` |

**E3 is the only failing criterion.** It is sufficient on its own to block.

---

## 3. `WEBGPU-115` — checkpoint blocker **YES**

### 3.1 The evidence, measured live at `099b03c0`

```
[ GFX-209 ] WebGPU solid:     total=18176 interior=1089/1089 AB=298 BC=310 CA=329
[ GFX-209 ] WebGPU wireframe: total=18176 interior=1089/1089 AB=298 BC=310 CA=329
```

Not read from a document — re-run this session against the current build of
`cmake-build-webgpu`. Every field is identical: the WireFrame frame is **byte-identical** to the
Solid one.

### 3.2 The decision rule, applied term by term

| Rule term | Finding | Evidence |
|---|---|---|
| Capability reports support | **YES — `true`** | `GraphicsDevice::SupportsCapability` (`GraphicsDevice.cpp:2000`) forwards to `IGraphicsBackend::SupportsCapability`, whose default (`IGraphicsBackend.hpp:1580-1589`) returns `true` for everything except `MultiStreamVertexInput`. **`WebGPUGraphicsBackend` does not override it at all** — the `true` is inherited, not asserted |
| Public operation accepted | **YES** | `WebGPUGraphicsBackend::ApplyRasterizerState` (`WebGPUGraphicsBackend.cpp:6056-6066`) stores `fillModeWireframe_ = (fillMode == 1)` and returns. No throw, no warning, no log |
| Command queued | **YES** | `command.wireframe = fillModeWireframe_` at **11 queue sites** |
| Native submission occurs | **YES** | `wireframe` is folded into `Make3DPipelineKey` (`WebGPUGraphicsBackend.cpp:632`), so a **distinct `WGPURenderPipeline` is created and submitted**. It then reaches no `WGPUPrimitiveState` field, because WebGPU has no polygon-mode API |
| Silently produces Solid | **YES** | §3.1 |
| A truthful capability boundary or exception exists | **NO** | See §3.3 |

### 3.3 Why "documented deviation" does not hold

Three artifacts were checked, and none is a truthful public boundary:

1. **`plan_webgpu.md:504`** — `| WEBGPU-115 | FillMode::WireFrame: document as unsupported in WebGPU (no polygon mode); add to deviations doc | ⬜ | |`. The ticket is **`⬜` — NOT DONE**. Its entire content is the documentation task itself, and that task has never been performed. Describing it as "already recorded and accepted" overstates it.
2. **`docs/webgpu-backend.md:488`** — wireframe appears in a prose bullet under `## Important limitations`. Prose in a backend document is not reachable through the public API, and it is **directly contradicted** by `SupportsCapability(WireFrame) == true`.
3. **Internal C++ comments** (`WebGPUGraphicsBackend.hpp:899`, `:1598`) — not public contract.

The public capability query is the public contract for capability, and it says the opposite of all
three. The runtime therefore makes an **affirmative false claim** and then silently renders the
wrong geometry.

### 3.4 Decision

**`WEBGPU-115` is a P1 checkpoint blocker.** It is an accepted supported-path operation that
silently returns wrong output with no truthful capability boundary and no exception — precisely the
class the exit rules reserve for P1.

### 3.5 Smallest safe correction

**Preferred (bounded, truthful-capability):** override `SupportsCapability` in
`WebGPUGraphicsBackend` to return `false` for `GraphicsCapability::WireFrame`, and reject a
WireFrame draw deterministically **at draw time** — not at `ApplyRasterizerState`, following
`REMED-GFX-DECL-GUARD`'s precedent that a state setter cannot know what the draw will be. This is
also the repository's established pattern for an unrepresentable request
(`MultiStreamVertexInput`, `REMED-GFX-DECL-GUARD`).

**Alternative (large):** implement real WebGPU wireframe by emitting line topology. wgpu-native
v29.0.1.1 has no polygon mode, so this means index-expanding triangles to lines — a genuine
implementation task, not a boundary correction.

**Two consequences the fix must carry**, both of which are by design and must be updated in the
same task, not worked around:

- `GraphicsDeviceCapabilityTest.WireFrameSilentlyRendersSolidGeometryOnThisBackend` is written to
  **fail the day this is fixed** (`GraphicsDeviceCapabilityTests.cpp:657` says so in its own failure
  message). That is the oracle working correctly.
- `examples/webgpu_graphicsstate_test.cpp` Check G asserts the WireFrame draw "does not crash"; a
  deterministic rejection changes that contract.

---

## 4. Remaining deferred work

Every row below is traceable to an ID, a reason and a target plan. **None is silently dropped.**

### 4.1 Deferred, checkpoint blocker NO — confirmed this session

| Ticket | Class | Why it does not block | Target |
|---|---|---|---|
| `REMED-GFX-203` … `-208` | Backend capability completion (multi-stream vertex input: Vulkan, bgfx, WebGPU, SDL_GPU, D3D11+D3D12, D3D9) | **Safe declared boundary.** `MultiStreamVertexInput` defaults to `false`; an over-wide stream set is rejected deterministically **before native submission**. No silent truncation | `plan_postaudit.md` §5, during modularization |
| `REMED-GFX-210` | No queryable capability for hardware instancing | Throws a `std::runtime_error` from the interface default. No false success — loud, not silent | `plan_postaudit.md` §6 |
| `REMED-GFX-214` | WebGPU stride 20/24 with `TextureEnabled = false` | **Loud deterministic pre-native rejection.** `QueueColoredDraw` throws `std::invalid_argument` before anything is queued, written or submitted. This is the exact opposite of `WEBGPU-115` | `plan_postaudit.md` §4.4.6 |
| `REMED-GFX-217` | Seven rasterizing backends — native declaration translators | Blocker **RESOLVED 2026-08-04** by `REMED-GFX-DECL-GUARD`: an unrepresentable declaration now raises `System::NotSupportedException` before any native layout exists. Verified this session at **24/24** | Translators during modularization |
| `REMED-GFX-218` | EasyGL — attribute location from element index | Blocker **RESOLVED 2026-08-04** by the same guard, on the stock path only; the custom `ShaderEffect` element-index convention is a documented contract and is untouched | Per-family semantic placement, post-checkpoint |
| `REMED-GFX-219` | EasyGL **under-reports** `WireFrame` | See §4.2 | `plan_postaudit.md` §9 |

### 4.2 `REMED-GFX-219` — disposition

**Mechanism: false-negative capability reporting.** Not a documentation mismatch — measured live at
`099b03c0`:

```
[ GFX-209 ] EasyGL wireframe: total=559 interior=0/1089 AB=25 BC=25 CA=25
[ GFX-209 ] EasyGL solid:     total=18176 interior=1089/1089 AB=298 BC=310 CA=329
```

EasyGL's `GL_LINES` emulation renders a **correct** wireframe, while
`EasyGLGraphicsBackend::SupportsCapability(WireFrame)` returns `false`. The `GL_LINES` emulation
landed in `a55397f7` (2026-06-30); the query asserting GLES3 has no wireframe was written in
`33d6540b` (2026-07-17), two and a half weeks later.

| Field | Value |
|---|---|
| Severity / priority | LOW / **P3** |
| Checkpoint blocker | **NO** |
| Integration blocker | **NO** |
| Disposition | **DEFERRED** to `plan_postaudit.md` |
| Suggested trigger | EasyGL capability/query cleanup, or any EasyGL module work |
| Also fix | `plan_graphics.md`'s `ℹ️ EasyGL N/A (GLES3)` coverage row records the same non-existent boundary |

**`REMED-GFX-219` must not be bundled with `WEBGPU-115`.** They both concern WireFrame and their
safety directions are **opposite**: GFX-219 under-reports a capability it actually has (a caller
that gates on the query loses a working feature — conservative, no wrong pixels, no false success);
WEBGPU-115 over-reports a capability it does not have (a caller gets silently wrong geometry). Only
the second is a blocker.

### 4.3 Older graphics tickets — classified here for the first time

`plan_postaudit.md` §8 explicitly states these are "**not** transferred, not re-prioritized and not
re-classified by this document." That left them **unclassified against the checkpoint-blocker rule**.
This reconciliation closes that gap:

| Ticket | Class | Blocker | Reason |
|---|---|---|---|
| `REMED-GFX-121` | bgfx non-GLSL renderers transpose the per-instance world matrix; instance 0 correct, later instances projected unpredictably | **REVIEW → NO for this checkpoint's declared scope** | Genuinely the same *shape* as `WEBGPU-115` (accepted public op, silently wrong, no runtime boundary), and the closest neighbour to it in the whole backlog. It does **not** block **this** checkpoint because the affected route — bgfx's SPIR-V/HLSL/Metal/WGSL renderers — is **not** the declared bgfx baseline; CNA's bgfx principal suite runs the GLSL/OpenGL renderer, where output is correct. The split is pinned by `BgfxPerInstanceWorldMatrixIsAppliedOnGlslRenderersOnly`, and `InstanceCountIsIndependentOfTheGeometryRange` **skips with a named reason** on the affected renderer. **It becomes a blocker the moment bgfx's Vulkan renderer enters declared checkpoint scope.** Fix needs a `.sc` shader change + `bgfx_shaders.hpp` regeneration across all profiles |
| `REMED-GFX-114` / `-111` | `PointListEXT` routed through the triangle-list default on Vulkan, D3D9, D3D11, D3D12 (`-114`) and bgfx (`-111`); 0/13 measured | **NO** | Silently wrong, but on a **`NOXNA` extension** topology, not XNA 4.0 public API. Measured and pinned at 0/13 — a declared, printed red, not a hidden one |
| `REMED-GFX-115` / `-120` | bgfx-Vulkan and SDL_GPU point pipelines emit `VUID-…-topology-08773` (no `PointSize` written) | **NO** | Validation-layer diagnostic; pixels correct on the tested driver. Recorded as a residual in §6 |
| `REMED-GFX-126` | `GetMultiSampleCount` contradicts its own "0 if not supported" contract | **NO** | Reporting defect; readback and sampling unaffected. Value pinned by `Software_RenderTargetReadback` H3 |
| `REMED-GFX-132` | `cna_reference_dump` fails to link under the ASCII backend | **NO** | One **tool** target. Every backend library, every ASCII test and `CnaTests` build |
| `REMED-GFX-133` | `headless_smoke_test` Check F aborts on a stale catch clause | **NO** | Test-owned, A/B-proven pre-existing, LOW/P3. Recorded as a residual in §6 |
| `REMED-GFX-171` / `-172` / `-178` / `-199` | D3D9 half-pixel sampling; WebGPU DualTexture3D single sampler; D3D12 filter ordinal 7; D3D12 instanced PSO format key | **NO** | LOW/P3 measured fidelity gaps, each pinned by a committed oracle |
| `REMED-GFX-052` / `-053` / `-055` / `-056` / `-085` / `-086` / `-163` / `-165` | Deferred capability boundaries, duplicates and upstream limits | **NO** | `-055` duplicate of `-054`; `-085`/`-086` upstream (bgfx global write mask; SDL 3.5.0 reserved sample mask) |

### 4.4 Non-graphics not-started items

`REMED-BUILD-011`, `-016`, `REMED-CONTENT-007`, `-008`, `REMED-NET-008`, `REMED-TEST-008`,
`REMED-MEDIA-005`, `REMED-DEVICES-004`, `REMED-NA-016`. All **NOT STARTED — recorded, not fixed**,
each out of the scope of the task that found it, each with a named owning lane. `REMED-NA-016` is a
vendored third-party header (`third_party/cgltf/`), out of scope by policy. **None is a checkpoint
blocker**; none is a supported-path silent wrong result.

---

## 5. Blocked / platform work

| Ticket | Severity | Nature | Consequence |
|---|---|---|---|
| `REMED-BUILD-012` | HIGH, P1, NOT STARTED | **Dev-environment limitation, not a CNA code defect.** Any D3D12 test constructing a real window via `Game` + `GraphicsDeviceManager` faults inside vanilla Wine's own `dxgi.dll` (`vkd3d_instance_get_vk_instance(instance=nullptr)` under `d3d12_swapchain_init`) | Blocks `REMED-BUILD-008`, `REMED-GFX-014`, `-015`, `-199` from public-API verification and gates `REMED-GFX-207`'s D3D12 runtime proof. **D3D12 is cross-build-only in this checkpoint and is deliberately not called clean** |

---

## 6. Known principal-suite residuals

Baselines from `REMED-GFX-209` (2026-08-04, `:101`). **They remain valid for `099b03c0`, which is
documentation-only** (`plan_postaudit.md`, `REMEDIATION_INDEX.md`, `REMEDIATION_PROGRESS.md`) — the
incremental builds this session confirmed zero compilation work on all ten build directories.

| Backend | Ran | Passed | Skipped | Failed |
|---|---|---|---|---|
| EasyGL | 5894 | 5887 | 6 | 1 |
| Software | 5822 | 5775 | 45 | 2 |
| Vulkan | 5877 | 5847 | 27 | 3 |
| bgfx | 5921 | 5891 | 27 | 3 |
| WebGPU | 5900 | 5867 | 28 | 5 |
| SDL_GPU | 5808 | 5784 | 20 | 4 |
| Headless | 5735 | 5688 | 44 | 3 |

| Residual | Backends | Cause / ticket | Class | Blocks? |
|---|---|---|---|---|
| `XnbContainerFuzzTest` | several | known-cause, named by `REMED-GFX-DECL-GUARD` | deterministic | NO |
| `CnjEffectTest` / `CnjStockEffectTest` | non-GL backends | GLSL fed to a non-GL backend | deterministic, by construction | NO |
| `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` | several | known-cause | deterministic | NO |
| Two-process networking test (30 s) | Software and others | timing | **flaky** — passed in the full run, failed in the A/B run | NO |
| `headless_smoke_test` Check F abort | Headless | `REMED-GFX-133` | deterministic, test-owned | NO |
| bgfx-Vulkan / SDL_GPU point-pipeline `VUID-…-08773` | bgfx, SDL_GPU | `REMED-GFX-115`, `-120` | deterministic validation diagnostic | NO |
| D3D12 device tests abort under Wine | D3D12 | `REMED-BUILD-012` | platform | NO — scope excluded |

`DoesNotSupportWireFrame` appears in **none** of these suites: `REMED-GFX-209` removed it.

---

## 7. Verification gate performed this session

All native runs on `:101`, `SDL_VIDEODRIVER=x11`. GPU work serial. Never `:0`.

| Gate | Result |
|---|---|
| `git diff --check` | **clean** |
| Working tree | **clean** before and after |
| `audit/` | **untouched** |
| Incremental build, 7 principal backends (EasyGL, Software, Vulkan, bgfx, WebGPU, SDL_GPU, Headless) | **all up to date, zero compilation work** |
| Incremental cross-build, D3D9 / D3D11 / D3D12 (mingw) | **all up to date** |
| `GraphicsDeviceCapabilityTest.*` — WebGPU | **13/13** (11 passed, 2 skipped); `WireFrameSilentlyRendersSolidGeometryOnThisBackend` **passes**, live-confirming `WEBGPU-115` |
| `GraphicsDeviceCapabilityTest.*` — EasyGL | **13/13** (12 passed, 1 skipped); live-confirms `REMED-GFX-219` |
| `VertexDeclarationFidelityTest` (`REMED-GFX-DECL-GUARD`) — WebGPU | **24/24** |
| `REMED-GFX-211`/`-212`/`-213`/`-215`/`-216` regression oracles (`InstancedDrawMultiStream`, `InstancedDrawRange`, `NonIndexedDrawRange`, `OrdinaryDrawBindingOffset`, `OrdinaryDrawMultiStream`, `VertexDeclarationFidelity`) | Vulkan **116/116** · bgfx **127/127** · WebGPU **118/118** |
| Principal suites | reused from `REMED-GFX-209` (2026-08-04) — valid, HEAD is documentation-only |
| ASan | reused from `REMED-GFX-209`: `cmake-build-software-asan`, `libasan.so.8` proved linked by `ldd`, **zero AddressSanitizer reports** |

**Not claimed clean:** D3D12 runtime (`REMED-BUILD-012`); any of the 43 planned backends outside the
10 measured; a fresh full-suite re-run (not warranted — HEAD is documentation-only against the
commit those suites measured).

---

## 8. Checkpoint decision — **OUTCOME B, EXIT BLOCKED**

- **No checkpoint tag was created.**
- The single blocking condition is **`WEBGPU-115`** (§3).
- **The next single bounded task is `WEBGPU-115`**, and nothing else. It must not be bundled with
  `REMED-GFX-219`.

**Smallest condition required to reach READY:** WebGPU must stop making an affirmative false
capability claim about `FillMode::WireFrame`. Either report the capability truthfully and reject the
draw deterministically before native submission (preferred, bounded), or implement real wireframe.
Once that lands — with its two dependent test contracts updated (§3.5) — every other exit criterion
in §2 is already satisfied and the checkpoint can be taken.

---

## 9. Integration handoff

- **Current inventory as of `099b03c0` (2026-08-04): 19 logical pending integration branches/lanes.**
  The count is *coincidentally* still 19, but **four refs in the previously recorded list no longer
  exist**. Derived from Git refs, not carried forward. Full detail, methodology and per-branch data:
  **`remediation/INTEGRATION_BRANCH_INVENTORY.md`**. This number is a snapshot and will change.
- **`feature/gl` is a cross-repository lane** and cannot be integrated on its own schedule — the
  MetaGL → EasyGL → CNA order is mandatory. See the inventory document §4.
- **EasyGL and MetaGL are development-complete.** Their completed branches are simply unmerged into
  their respective `develop` branches. They are **not** unfinished feature developments.
- **Magnum and Wicked Engine:** no branch, ref, worktree or planning entry exists for either in this
  repository or the surrounding workspace as of `099b03c0`. Recorded as **not evidenced** — see the
  inventory document §5. They are deliberately **not** counted in the 19.
- **Commit-history policy for all future CNA/EasyGL/MetaGL integration** is mandatory and recorded in
  the inventory document §6: archive tags preserve original heads, adapted commits are GPG-signed,
  and **no AI attribution may appear** in the final adapted history.
- **Integration entry conditions:** `WEBGPU-115` resolved and the checkpoint tag taken; then the
  per-branch adaptation checklist in `plan_postaudit.md` §10.

---

## 10. Modularization and NoXNA handoff

The deferred translators and capability work should be addressed **during backend/module work, not
before the checkpoint**:

- `REMED-GFX-203` … `-208` (multi-stream vertex input, six lanes) — each while touching its own
  backend module. Vulkan first; it sets the pattern.
- `REMED-GFX-217`'s seven native declaration translators and `REMED-GFX-218`'s per-family semantic
  placement — both **after** their Strategy-C guard, which has shipped.
- `REMED-GFX-210` and `REMED-GFX-219` — capability-reporting cleanups, naturally grouped with the
  modules that own them.

Doing any of these before the checkpoint would enlarge the checkpoint's blast radius without making
the runtime any more truthful — the guard already does that. **Nothing in `plan_postaudit.md` is a
prerequisite for NoXNA graphics-extension work.**

**This document does not claim the long-term CNA backlog is complete.** It claims the audit-driven
remediation campaign has one remaining blocker, and names it.
