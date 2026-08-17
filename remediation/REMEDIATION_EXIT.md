# REMEDIATION_EXIT.md — post-audit remediation phase-1 exit record

> ## STATUS: **AUTHORITATIVE — OUTCOME A, READY. Phase-1 checkpoint taken.**
>
> This document is the exit record of the audit-driven remediation campaign, reconciled from refs,
> files and committed behaviour **as they stand on 2026-08-04 after
> `git fetch --all --prune --tags`**. It supersedes the `099b03c0` reconciliation, which returned
> OUTCOME B on `WEBGPU-115` and explicitly refused to amend itself to READY because its inventories
> predated a fetch.
>
> **This is phase 1. It is not the completion of all CNA work.** It certifies that the campaign
> reached a safe, traceable resting point — not that the backlog is empty, not that all 43 planned
> backends exist, and not that every deferred ticket is unimportant. §4 names what remains,
> including **two open HIGH path-containment findings** (`REMED-CONTENT-007`/`-008`) that were
> re-verified as still present in current source by this session.
>
> **Post-exit addendum — 2026-08-08:** those two findings are now **DONE**. Red-first synthetic
> fixtures proved the Song/Video and ContentManager escape routes; signed production commits route
> every affected caller through a shared component-aware containment primitive; focused and broad
> Content shards pass with ASan, UBSan, and LeakSanitizer. The bounded same-pattern audit also
> created and closed `REMED-CONTENT-011`. This does not rewrite the dated 2026-08-04 judgement:
> every statement below describing `-007`/`-008` as open is retained as historical phase-1 exit
> evidence. Current technical detail is in `REMEDIATION_PROGRESS.md`; the separate Batch 5
> checkpoint retake is recorded in `integration/BATCH_5_STABILIZATION.md`.

---

## 1. Checkpoint identity

| Field | Value |
|---|---|
| Repository | `cnaaudit` worktree of the CNA repository |
| Candidate branch | **`feature/audit`** |
| Candidate commit | the final documentation commit of this reconciliation — the commit named by the checkpoint tag (§8) |
| Checkpoint tag | **`cna-post-audit-remediation-phase1`**, GPG-signed annotated, **local only, not pushed** |
| Date | **2026-08-04** |
| Campaign framing | Repository-wide audit frozen 2026-07 (6 passes, 2297 files, 14 backends) → remediation campaign 2026-07-19 … 2026-08-04 |
| Audit scope of record | 686 raw per-file findings + 6 synthesis documents → **105 remediation tasks + 15 accepted no-action items** |
| Executed scope | The 105 above, plus **141 findings discovered during remediation** |
| Backend scope of this checkpoint | **10 measured** — EasyGL, Software, Vulkan, bgfx, WebGPU, SDL_GPU, Headless (native, `:101`); D3D9 + D3D11 (Wine/DXVK, `:99`); **D3D12 cross-build only, no runtime** (`REMED-BUILD-012`) |
| Explicitly NOT in scope | The **21** pending integration lanes and their backends; the 43-backend long-term roadmap |

`audit/` is untouched by this reconciliation and by the whole GFX-2xx cluster; its last commit
remains `74ebf356`.

---

## 2. Exit criteria

**Completion here is milestone-bounded, not zero-backlog bounded.**

| # | Criterion | State |
|---|---|---|
| E1 | No known unblocked CRITICAL | ✅ The audit's single CRITICAL (`REMED-CONTENT-001`) is DONE |
| E2 | No known supported-path crash, memory corruption or silent data loss blocker | ✅ The two remaining aborts are test-owned and classified (§6) |
| E3 | No unresolved supported-path silent wrong result classified as a blocker | ✅ **`WEBGPU-115` closed and re-verified in source this session** (§3) |
| E4 | Capability gaps either reject truthfully or are explicitly deferred | ✅ §4.1; verified live at 24/24 (declaration guard) and on the WireFrame matrix |
| E5 | Principal backend baselines classified | ✅ 7 native + 2 Wine, §6 |
| E6 | Deferred work safely bounded and traceable to `plan_postaudit.md` | ✅ Every deferred ticket has an ID, a reason and a target plan (§4) |
| E7 | Dynamic branch inventory available, derived after a fetch | ✅ `INTEGRATION_BRANCH_INVENTORY.md` — **21 logical lanes** |

**Every criterion passes. The checkpoint-blocker set is EMPTY.**

### 2.1 The judgement E1–E7 do *not* make

`REMED-CONTENT-007` and `REMED-CONTENT-008` are **open HIGH path-containment findings**, verified
still present in current source by this session (§4.4). They are not dismissed and not called
harmless. They fall outside E1–E3 as written — they are not a CRITICAL, not a crash, not memory
corruption, not silent data loss, and not a silent wrong render result — so under the campaign's
stated rules they do not block this checkpoint. **They are nevertheless the highest-severity open
items in the whole inventory**, and §4.4 names them as the recommended first substantive
post-checkpoint work. Anyone reading this document as "security is clean" is reading it wrong.

---

## 3. `WEBGPU-115` — RESOLVED, re-verified in source and at runtime

Verified this session from the committed tree, not carried over from the prior document.

| Term of the blocker rule | Before | Now | Evidence re-checked this session |
|---|---|---|---|
| Capability reports support | `true`, inherited from `IGraphicsBackend` | **`false`** | `WebGPUGraphicsBackend::SupportsCapability` returns `false` for `GraphicsCapability::WireFrame` (`WebGPUGraphicsBackend.cpp:6065-6075`) |
| Public operation accepted | yes, silently | `ApplyRasterizerState` still accepts — a state operation stays one | `:6097-6107`; comment records the `REMED-GFX-DECL-GUARD` precedent |
| Command queued / pipeline created / submitted | +1 each | **0** | `RequireSupportedFillModeEXT` is the **first statement** of all five public 3D draw entry points — `:7281`, `:7290`, `:7303`, `:7397`, `:7471` |
| Silently produces Solid | `total=18176 interior=1089/1089` | **target unchanged** | `WebGpuWireFrameContract.*` **9/9 PASS** |
| A truthful boundary exists | **NO** | **YES** | `System::NotSupportedException`; message names the mode, the backend and the capability query (`:6088-6094`) |

**Only polygon topologies are refused.** `RequireSupportedFillModeEXT` returns early for anything
that is not `TriangleList` or `TriangleStrip` (`:6086`). Line and point topologies have no polygon
interior for a fill mode to select, were measured byte-identical under both modes, and this backend
substitutes nothing there — refusing them would delete a correct draw rather than prevent a wrong
one. Live this session: `[WEBGPU-115] PointListEXT solid: ACCEPTED … nativeDraws=1` and
`[WEBGPU-115] PointListEXT wireframe: ACCEPTED … nativeDraws=1`.

`plan_webgpu.md`'s `WEBGPU-115` row is **`✅`**. **No new ticket was created for it, and no other
ticket was bundled into it** — `REMED-GFX-219` in particular is untouched (§4.2).

---

## 4. Remaining deferred work

Every row is traceable to an ID, a reason and a target plan. **None is silently dropped.**

### 4.1 Deferred, checkpoint blocker NO — capability boundaries that reject truthfully

| Ticket | Class | Why it does not block | Target |
|---|---|---|---|
| `REMED-GFX-203` … `-208` | Backend capability completion (multi-stream vertex input: Vulkan, bgfx, WebGPU, SDL_GPU, D3D11+D3D12, D3D9) | **Safe declared boundary.** `MultiStreamVertexInput` defaults to `false`; an over-wide stream set is rejected deterministically **before native submission**. Verified live: the 21 multi-stream oracles **skip with a named reason** on Vulkan, bgfx and WebGPU rather than rendering something wrong | `plan_postaudit.md` §5, during modularization |
| `REMED-GFX-210` | No queryable capability for hardware instancing | Throws `std::runtime_error` from the interface default. No false success — loud, not silent | `plan_postaudit.md` §6 |
| `REMED-GFX-214` | WebGPU stride 20/24 with `TextureEnabled = false` | **Loud deterministic pre-native rejection.** `QueueColoredDraw` throws `std::invalid_argument` (`WebGPUGraphicsBackend.cpp:7213`) **before** `ColoredDrawCommand` is constructed — nothing queued, written or submitted. The exact opposite of `WEBGPU-115` | `plan_postaudit.md` §4.4.6 |
| `REMED-GFX-217` | Seven rasterizing backends — native declaration translators | Blocker **RESOLVED** by `REMED-GFX-DECL-GUARD`: an unrepresentable declaration raises `System::NotSupportedException` before any native layout exists. `RequireFaithfulDeclarationEXT` present in **Vulkan, WebGPU, Software, SDL_GPU, D3D9, D3D11, D3D12**; **Headless correctly excluded** (no native layout to be unfaithful to). Verified **24/24** on Vulkan, bgfx, WebGPU and EasyGL | Translators during modularization |
| `REMED-GFX-218` | EasyGL — attribute location from element index | Blocker **RESOLVED** by the same guard, on the **stock path only** (`EasyGLGraphicsBackend.cpp:5932`, `:6002`, `:6095` — before the VAO is touched and before a program is selected). The custom `ShaderEffect` element-index convention is a documented contract and is untouched | Per-family semantic placement, post-checkpoint |
| `REMED-GFX-219` | EasyGL **under**-reports `WireFrame` | See §4.2 | `plan_postaudit.md` §9 |

### 4.2 `REMED-GFX-219` — disposition confirmed

**Mechanism: false-negative capability reporting.** `EasyGLGraphicsBackend::SupportsCapability`
returns `false` for `GraphicsCapability::WireFrame` (verified this session at
`EasyGLGraphicsBackend.cpp:2187-2190`, whose comment cites "GLES3 has no wireframe fill mode at
all"), while EasyGL's own `GL_LINES` emulation renders a **correct** wireframe
(`total=559 interior=0/1089`, against Solid's `total=18176 interior=1089/1089`).

| Field | Value |
|---|---|
| Severity / priority | **LOW / P3** |
| Checkpoint blocker | **NO** |
| Integration blocker | **NO** |
| Disposition | **OPEN / DEFERRED** to `plan_postaudit.md` §9 |
| Suggested trigger | EasyGL capability/query cleanup, or any EasyGL module work |
| Also fix | `plan_graphics.md`'s `ℹ️ EasyGL N/A (GLES3)` coverage row records the same non-existent boundary |

**Not bundled with `WEBGPU-115`, deliberately.** Their safety directions are **opposite**: GFX-219
under-reports a capability it has (a caller that gates on the query loses a working feature —
conservative, no wrong pixels, no false success); WEBGPU-115 over-reported one it does not have (a
caller got silently wrong geometry). Only the second was ever a blocker.

### 4.3 Older graphics tickets — classified against the blocker rule

| Ticket | Class | Blocker | Reason |
|---|---|---|---|
| `REMED-GFX-121` | bgfx non-GLSL renderers transpose the per-instance world matrix | **NO for this checkpoint's declared scope** | The same *shape* as `WEBGPU-115` and its closest neighbour in the backlog. It does not block **this** checkpoint because the affected route — bgfx's SPIR-V/HLSL/Metal/WGSL renderers — is **not** the declared bgfx baseline; CNA's bgfx principal suite runs the GLSL/OpenGL renderer, where output is correct. Pinned by `BgfxPerInstanceWorldMatrixIsAppliedOnGlslRenderersOnly`; `InstanceCountIsIndependentOfTheGeometryRange` **skips with a named reason** on the affected renderer. **It becomes a blocker the moment bgfx's Vulkan renderer enters declared checkpoint scope** |
| `REMED-GFX-114` / `-111` | Historical `PointListEXT` triangle-list defaults on Vulkan/Direct3D and bgfx | **NO — both DONE** | Closed by native point mappings everywhere the current pipeline can represent them; D3D12's triangle-typed PSO cache instead has a named early refusal. The shared framebuffer/source gates prevent a silent default from returning |
| **`REMED-GFX-137`** | **EasyGL stores a RENDERED cube face and an UPLOADED one in opposite row orders** | **NO** | **Classified here for the first time** — see §4.3.1 |
| **`REMED-GFX-139`** | **D3D9 `RenderTargetCube` reports a mip chain it never allocates** | **NO** | **Classified here for the first time** — a *reporting* defect of the same class as `REMED-GFX-126`. `Recreate()` passes `Levels=1` whatever `mipMap` asked for, while `LevelCount` computes the full chain from `mipMap`. `REMED-GFX-134` made `GetData` **refuse** the levels that were never allocated instead of answering them from level 0, so the **data path is honest** and no wrong pixels are returned; only the count is wrong. LOW, D3D9 (Wine/DXVK scope) |
| `REMED-GFX-115` / `-120` | bgfx-Vulkan and SDL_GPU point pipelines emit `VUID-…-topology-08773` | **NO** | Validation-layer diagnostic; pixels correct on the tested driver. Residual in §6 |
| `REMED-GFX-126` | `GetMultiSampleCount` contradicts its own "0 if not supported" contract | **NO** | Reporting defect; readback and sampling unaffected. Pinned by `Software_RenderTargetReadback` H3 |
| **`REMED-GFX-132`** | `cna_reference_dump` fails to link | **NO** | **Scope corrected this session — see §4.3.2** |
| `REMED-GFX-133` | `headless_smoke_test` Check F aborts on a stale catch clause | **NO** | Test-owned, A/B-proven pre-existing, LOW/P3. Residual in §6 |
| `REMED-GFX-171` / `-178` / `-199` | D3D9 half-pixel sampling; D3D12 filter ordinal 7; D3D12 instanced PSO format key | **NO** | LOW/P3 measured fidelity gaps, each pinned by a committed oracle |
| `REMED-GFX-053` / `-055` / `-056` / `-085` / `-086` / `-163` / `-165` / `-184` | Deferred capability boundaries, duplicates and upstream limits | **NO** | `-055` duplicate of `-054`; `-184` duplicate of `-163`; `-085`/`-086` upstream (bgfx global write mask; SDL 3.5.0 reserved sample mask) |

**Corrected: `REMED-GFX-172` is DONE, not open.** The previous exit record listed it among the open
LOW/P3 tickets and `REMEDIATION_PROGRESS.md`'s discovered-findings table still carried it as `OPEN`.
It closed on 2026-07-31 — reproducer `c8c35032`, fix `25bb5ecc`, closure `92546670`, and a full
narrative record in `REMEDIATION_PROGRESS.md` §"REMED-GFX-172 — WebGPU bound ONE sampler to the TWO
texture views of both multi-texture families". Both stale records are fixed in this commit. The
error was in the safe direction (an open count that was too high), but a fast-lookup table that
disagrees with the narrative is exactly the defect this reconciliation exists to catch.

#### 4.3.1 `REMED-GFX-137` — why an unfixed cross-backend divergence does not block

EasyGL's rasterizer fills a cube face bottom-up while `glTexSubImage2D` writes source row 0 into
texel row 0. `GetData` normalizes the **rendered** case (that was `REMED-GFX-134`'s fix), which
leaves the two writers of one face disagreeing: a rendered face and a `SetData` face sample mirrored
relative to each other, and a rendered EasyGL face samples mirrored relative to D3D/Vulkan.

It does not block because **the divergence is declared and printed, not silent**. Check **W1** of
`examples/rendertargetcube_getdata_contract_test.cpp` asserts it against a per-backend contract flag
(`kContract.rtCubeUploadMirrored`) and prints its measurement on every run —
`[sameOrder=…/… mirrored=…/… refused=…]`. There is no capability query making a contrary claim, both
write paths succeed, and both readbacks are honest. This is the same disposition
`REMED-GFX-114` had at this checkpoint: a declared, printed red rather than a hidden one. That
point-topology ticket was subsequently fixed by `GLTF-393`/`394`; this cube convention remains.

The ticket also records **why the obvious fix is wrong**: flipping the upload would only move the
divergence onto the cube **sampling** path, where the real convention difference lives. Correcting it
belongs with cube sampling/camera conventions, post-checkpoint. MEDIUM, `plan_postaudit.md`.

#### 4.3.2 `REMED-GFX-132` — recorded scope was too narrow

Recorded as "cannot link **under the ASCII backend**". Measured this session: `cna_reference_dump`
also fails to link under **bgfx**, with `undefined reference to vtable for
Microsoft::Xna::Framework::Graphics::VertexDeclaration`. The binary is present in 6 of 8 build
directories and **absent in exactly `cmake-build-bgfx` and `cmake-build-ascii`**.

- **Same root cause**, unchanged: `cmake/Examples.cmake:271` links it with a plain
  `target_link_libraries(cna_reference_dump PRIVATE CNA)` — no backend target, no
  `-Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group`.
- **Pre-existing, not a regression.** `cmake/Examples.cmake` was last touched on **2026-07-20**
  (`b60bbab9`), long before the WEBGPU-115 work; the tool binary has never existed in
  `cmake-build-bgfx`.
- **Blocker NO, unchanged.** It is **one tool target**. Under bgfx, `CNA`,
  `cna_backend_graphics_bgfx` and `CnaTests` all build and link cleanly (verified this session, exit
  0 for each).

The ticket text in `REMEDIATION_INDEX.md` is corrected to name both backends.

### 4.4 Non-graphics not-started items — including the two open HIGH findings

| Ticket | Sev / Pri | State verified this session |
|---|---|---|
| **`REMED-CONTENT-007`** | **HIGH / P1** | **STILL OPEN — re-verified in source.** `SongContentTypeReader.cpp` and `VideoContentTypeReader.cpp` each still define a private `ResolveRelativeFilePath()` (`:30`) with **no containment check**, fed directly by the `.xnb`'s own embedded filename (`input.ReadString()`, `:78`). `include/CNA/Internal/PathContainment.hpp` exists and is **not used** by either reader |
| **`REMED-CONTENT-008`** | **HIGH / P1** | **STILL OPEN — re-verified in source.** `ContentManager.cpp` contains **zero** calls to `IsDisallowedAbsolutePath` / `ResolveContainedPath` / `PathContainment`, while joining manifest-supplied fields onto the content root raw — e.g. `fs::path(cm.getRootDirectoryProperty()) / dataField->stringValue` (`:560`), `fs::path(root) / vertRel` and `/ fragRel` (`:779-780`). The neighbouring `sourceFile` field *is* hardened via `CnjSourceFile.hpp` (7 uses), which is what makes the other fields' omission visible |
| `REMED-BUILD-011`, `-016`, `REMED-NET-008`, `REMED-TEST-008`, `REMED-MEDIA-005`, `REMED-DEVICES-004` | MEDIUM/LOW | **NOT STARTED — recorded, not fixed.** Each out of the scope of the task that found it, each with a named owning lane |
| `REMED-NA-016` | LOW | **Out of scope by policy** — vendored third-party header (`third_party/cgltf/`), not CNA-authored |

**Why `REMED-CONTENT-007`/`-008` do not block this checkpoint**, stated plainly rather than waved
through: both are **path-containment bypasses**, in the same class and with the same fix shape as
`REMED-CONTENT-002` (DONE, 3 sites). They are recorded, reproduced-by-inspection, owned, and their
fix is known. They are not a CRITICAL, not a crash, not memory corruption, not silent data loss, and
not a silent wrong render result, so they fall outside E1–E3. They have been classified non-blocking
consistently since Wave 2 (2026-07-20), including by the reconciliation that **did** return BLOCKED
on `WEBGPU-115` — so this is the same rule applied consistently, not a rule relaxed to reach READY.

**They are the recommended first substantive post-checkpoint task.** Reach for them before any
deferred graphics capability work: they are HIGH, they are security-class, the helper they need
already exists, and the fix is mechanical.

---

## 5. Platform / toolchain-blocked work

| Ticket | Severity | Nature | Consequence |
|---|---|---|---|
| `REMED-BUILD-012` | HIGH, P1, NOT STARTED | **Dev-environment limitation, not a CNA code defect.** Any D3D12 test constructing a real window via `Game` + `GraphicsDeviceManager` faults inside vanilla Wine's own `dxgi.dll` (`vkd3d_instance_get_vk_instance(instance=nullptr)` under `d3d12_swapchain_init`) | Blocks `REMED-BUILD-008`, `REMED-GFX-014`, `-015`, `-199` from public-API verification and gates `REMED-GFX-207`'s D3D12 runtime proof. **D3D12 is cross-build-only in this checkpoint and is deliberately not called clean** |

---

## 6. Known principal-suite residuals

Baselines from `REMED-GFX-209` (2026-08-04, `:101`) and the `WEBGPU-115` session, both executed on
the production tree this checkpoint names. **No production code changed after `WEBGPU-115`** — this
reconciliation is documentation-only — so they remain valid, and §7 records what was re-run rather
than assumed.

| Backend | Ran | Passed | Skipped | Failed |
|---|---|---|---|---|
| EasyGL | 5894 | 5887 | 6 | 1 |
| Software | 5822 | 5775 | 45 | 2 |
| Vulkan | 5877 | 5847 | 27 | 3 |
| bgfx | 5921 | 5891 | 27 | 3 |
| WebGPU | 5909 | 5876 | 28 | 5 |
| SDL_GPU | 5808 | 5784 | 20 | 4 |
| Headless | 5735 | 5688 | 44 | 3 |

| Residual | Backends | Cause / ticket | Class | Blocks? |
|---|---|---|---|---|
| `XnbContainerFuzzTest` | several | known-cause, named by `REMED-GFX-DECL-GUARD` | deterministic | NO |
| `CnjEffectTest` / `CnjStockEffectTest` | non-GL backends | GLSL fed to a non-GL backend | deterministic, by construction | NO |
| `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` | several | known-cause | deterministic | NO |
| Two-process networking test (30 s) | Software and others | timing | **flaky** | NO |
| `headless_smoke_test` Check F abort | Headless | `REMED-GFX-133` | deterministic, test-owned | NO |
| bgfx-Vulkan / SDL_GPU point-pipeline `VUID-…-08773` | bgfx, SDL_GPU | `REMED-GFX-115`, `-120` | deterministic validation diagnostic | NO |
| **`cna_reference_dump` link failure** | **bgfx, ASCII** | **`REMED-GFX-132`** (scope corrected, §4.3.2) | **deterministic, tool-only, pre-existing** | **NO** |
| D3D12 device tests abort under Wine | D3D12 | `REMED-BUILD-012` | platform | NO — scope excluded |

`DoesNotSupportWireFrame` appears in **none** of these suites: `REMED-GFX-209` removed it.

---

## 7. Verification gate performed this session

All native runs on `:101`, `SDL_VIDEODRIVER=x11`, `WAYLAND_DISPLAY` unset. GPU work **serial**.
**Never `:0`.** Maximum compilation parallelism **`-j8`** (reached once, then reduced to `-j4`/`-j3`
on temperature); the session never exceeded eight.

| Gate | Result |
|---|---|
| `git fetch --all --prune --tags` (CNA) | exit 0; **no refs added, updated or pruned**; no tags added |
| `git fetch --all --prune --tags` (`easy-gl`, `meta-gl`, `meta-gl-followup-audit`) | exit 0 each; **no ref changes**; `easy-glrvc` deliberately not fetched (dirty tree) |
| Working tree | **clean** before and after |
| `git diff --check` | **clean** |
| `audit/` | **untouched** |
| Five `WEBGPU-115` commits GPG-signed | ✅ `679cbed2`, `636b43de`, `40a5c46c`, `0be30127`, `765335f5` — all report a good signature |
| GPG preflight (clearsign + detached tag-object sign) | ✅ both exit 0, non-interactive |
| Incremental build, 7 principal backends | **exit 0 each.** Several required real compilation to reach current HEAD (SDL_GPU 95 TUs, Headless 66) — the `WEBGPU-115` shared test change propagated. Re-run to convergence: **0 compiled, 0 errors** |
| Incremental cross-build, D3D9 / D3D11 / D3D12 (mingw) | **exit 0 each** (48 / 40 / 21 TUs compiled, 0 errors) |
| **bgfx `cna_reference_dump`** | **FAILS to link** — `REMED-GFX-132`, pre-existing, tool-only (§4.3.2). `CNA`, `cna_backend_graphics_bgfx`, `CnaTests` each build **exit 0** under bgfx |
| `WebGpuWireFrameContract.*` + `GraphicsDeviceCapabilityTest.*` — WebGPU | **22 ran, 20 passed, 2 skipped, 0 failed.** The 2 skips are exactly the WireFrame tests that must now skip |
| `GraphicsDeviceCapabilityTest.*` — EasyGL / Software / Vulkan / bgfx / SDL_GPU | **13 ran, 12 passed, 1 skipped, 0 failed** on each |
| `GraphicsDeviceCapabilityTest.*` — Headless | **11 ran, 10 passed, 1 skipped, 0 failed** (`WireFrameHasNoPixelRouteOnThisBackend` — its own honest skip) |
| `VertexDeclarationFidelityTest.*` (`REMED-GFX-DECL-GUARD`) | **24/24** on Vulkan, bgfx, WebGPU **and** EasyGL |
| `REMED-GFX-211`/`-212`/`-213`/`-215`/`-216` oracles (`InstancedDrawMultiStream`, `InstancedDrawRange`, `NonIndexedDrawRange`, `OrdinaryDrawBindingOffset`, `OrdinaryDrawMultiStream`, `InstancedVertexColor`, `VertexDeclarationFidelity`) | Vulkan **125 ran / 104 passed / 21 skipped / 0 failed** · bgfx **136 / 115 / 21 / 0** · WebGPU **127 / 106 / 21 / 0**. The 21 skips are the declared `MultiStreamVertexInput` boundary (`REMED-GFX-203`…`-208`) |
| Principal suites | **reused** from `REMED-GFX-209` / `WEBGPU-115` — valid, HEAD is documentation-only against the commit those suites measured. Not re-run: no concrete reason to |
| ASan / UBSan | **reused** from `WEBGPU-115` (runtimes proved linked by `ldd`; zero AddressSanitizer reports). Not re-run: no production change since |

**Not claimed clean:** D3D12 runtime (`REMED-BUILD-012`); any of the 43 planned backends outside the
10 measured; `cna_reference_dump` under bgfx and ASCII; a fresh full-suite re-run.

---

## 8. Checkpoint decision — **OUTCOME A, READY**

- Complete reconciliation finds **zero checkpoint blockers**.
- Every deferred ticket has an ID, a truthful safe boundary and a target plan (§4).
- Three stale records were found and corrected in this commit: `REMED-GFX-172` counted open while
  DONE (§4.3), `REMED-GFX-137`/`-139` never actually classified despite the index claiming they
  were, and `REMED-GFX-132`'s scope understated (§4.3.2).
- Final gate acceptable (§7); working tree clean.

**Signed annotated tag `cna-post-audit-remediation-phase1` created, local only. Not pushed.**

**`feature/direct2d` is still committing** (latest commit ~2 minutes before the inventory was
derived). **That does not invalidate this checkpoint**, which is the *base onto which branches are
later adapted*, not a claim that every lane has stopped.

---

## 9. Integration handoff

- **Current inventory as of the 2026-08-04 fetch and this checkpoint candidate: 21 logical pending
  integration lanes.** **N may change before integration begins.** Full per-lane data, methodology
  and conflict classes: **`remediation/INTEGRATION_BRANCH_INVENTORY.md`**. **Do not restate 19** —
  that figure predates a fetch and is retained there only as a labelled historical snapshot.
- **`feature/direct2d` is the final actively-developed lane** (`6cd6ad06`, 752 ahead, local 34 ahead
  of `origin`, last commit `2026-08-04T11:27:08Z`). **Freeze it at a known head before integration
  begins.** Inventory §5.
- **Magnum and Wicked are audit-stacked, not develop-forked.** Both fork from `feature/audit` @
  `2338b44f7`, carry 755 remediation commits, and add **13** and **10** of their own. Each needs a
  22-commit rebase onto the checkpoint base; after that Wicked touches **none** of the three shared
  interfaces and Magnum touches two. **Neither is established as integration-ready.** Inventory §4.
- **`feature/gl` is a cross-repository lane.** The **MetaGL → EasyGL → CNA** order is mandatory and
  cannot be reordered. **EasyGL and MetaGL development is complete** — their completed branches
  (`rvc` @ `b52f671`, `feature/followup-audit` @ `d5bc155`) are simply unmerged into their
  respective `develop` branches. Inventory §6, including the correction that the MetaGL redirect
  lives in an **uncommitted** working-tree edit, not in committed history.
- **Commit-history policy is mandatory** and recorded in the inventory §8: archive tags preserve
  original heads, adapted commits are GPG-signed, and **no AI attribution may appear** in the final
  adapted history. Literal ref names containing `claude` remain valid provenance identifiers.
- **Entry conditions for the first adaptation branch:** inventory §8.4.

---

## 10. Modularization and NoXNA handoff

The deferred translators and capability work proceed **after integration and stabilization**, over
clearer module boundaries:

- `REMED-GFX-203` … `-208` (multi-stream vertex input, six lanes) — each while touching its own
  backend module. Vulkan first; it sets the pattern.
- `REMED-GFX-217`'s seven native declaration translators and `REMED-GFX-218`'s per-family semantic
  placement — both **after** their Strategy-C guard, which has shipped.
- `REMED-GFX-210`, `REMED-GFX-219`, `REMED-GFX-126`, `REMED-GFX-139` — capability- and
  reporting-truthfulness cleanups, naturally grouped with the modules that own them.
- `REMED-GFX-137` — with cube sampling/camera conventions, not before.

Doing any of these before the checkpoint would enlarge its blast radius without making the runtime
any more truthful — the guards already do that. **Nothing in `plan_postaudit.md` is a prerequisite
for NoXNA graphics-extension work.**

**This document does not claim the long-term CNA backlog is complete.** It claims the audit-driven
remediation campaign has **no remaining checkpoint blocker**, and names everything that remains.
