# REMEDIATION_INDEX.md — Fast Lookup

**104 remediation tasks + 15 accepted no-action items**, consolidated from 686 raw per-file findings
plus the audit's 6 synthesis documents. Full detail for every ID is in `MASTER_REMEDIATION_PLAN.md`.

## Counts

### By priority

| Priority | Count | Meaning |
|---|---|---|
| **P0** | **11** | Security, memory corruption, UB, and the two build/test blockers |
| **P1** | **21** | Test/CI reliability, HIGH correctness, broad cross-backend defects, major FNA divergences |
| **P2** | **44** | MEDIUM correctness, missing backend features, lifecycle, API surface |
| **P3** | **28** | LOW, performance, maintainability, non-urgent architecture |

### By audit severity

| Severity | Count | Note |
|---|---|---|
| **CRITICAL** | **1** | `REMED-CONTENT-001` — the only CRITICAL in the entire audit |
| **HIGH** | 31 | |
| **MEDIUM** | 55 | |
| **LOW** | 17 | |

Severity and priority deliberately diverge in 9 tasks. The clearest cases: `REMED-BUILD-001` and
`REMED-BUILD-002` are HIGH but scheduled **P0** for leverage; `REMED-CONTENT-006` is MEDIUM but
scheduled **P0** because it is a reachable stack-exhaustion DoS; `REMED-GFX-031`/`-032`/`-048`/`-050`
are MEDIUM but scheduled **P3** because they are isolated and low-blast-radius.

### By owner lane

| Owner | Tasks | Note |
|---|---|---|
| **GRAPHICS** | 51 | Largest lane by far. Internally serialized for shader work — see `REMEDIATION_DEPENDENCIES.md`. |
| **BUILD_TEST_CI** | 19 | `BUILD` (9) + `TEST` (7) + `DOCS` (3) |
| **CORE** | 13 | |
| **NET** | 7 | Includes GamerServices |
| **CONTENT** | 5 | Includes Storage and XNB |
| **MEDIA** | 4 | |
| **DEVICES** | 3 | |
| **AUDIO** | 2 | Lane barely justified — see below |
| **INPUT** | 0 | **Lane not created** — see below |

**On the AUDIO and INPUT lanes.** The audit swept `Microsoft.Xna.Framework.Audio` (31 files, described
as "the most thoroughly self-audited subsystem encountered") and `Input` (44 files, plus a full
member-level xn65 cross-check that found zero discrepancies across `Buttons`' 32 bit values, `Keys`'
160 entries, and every dead-zone/clamp formula). Between them they produced **two LOW findings and one
tagging nit.** Standing up dedicated lanes for these would be ceremony, not capacity. `AUDIO` is kept
as an ID namespace for its two tasks; `INPUT` is not created at all, and its single finding is folded
into `REMED-CORE-012`. This is a real result about the codebase, not an omission.

## P0 — do these first

| ID | Title | Sev | Owner | PS |
|---|---|---|---|---|
| `REMED-BUILD-001` | `gtest_discover_tests` missing `WORKING_DIRECTORY` (~220 tests) | HIGH | BUILD | YES |
| `REMED-BUILD-002` | `cna_demo_xact` POST_BUILD copy aborts every build | HIGH | BUILD | YES |
| `REMED-CONTENT-001` | Malformed Texture2D `.xnb` crashes Vulkan + WebGPU | **CRITICAL** | CONTENT | COND |
| `REMED-CONTENT-002` | `fs::path` containment bypass — 3 sites, 1 root cause | HIGH | CONTENT | COND |
| `REMED-CONTENT-003` | TextureCube reader missing byte-count validation (OOB read) | HIGH | CONTENT | YES |
| `REMED-CONTENT-006` | `XnbTypeName` unbounded recursion + 2 dead `XnbReadLimits` | MEDIUM | CONTENT | YES |
| `REMED-GFX-001` | EasyGL `RegisterForWindow` dangling registry (UAF) | HIGH | GRAPHICS | COND |
| `REMED-GFX-002` | `SpriteFont`/`SpriteBatch` `end()` iterator deref (UB) | HIGH | GRAPHICS | COND |
| `REMED-GFX-003` | `SpriteEffects` undersized table (OOB stack read) | HIGH | GRAPHICS | NO |
| `REMED-NET-001` | ENet host-only broadcasts accepted from any peer | HIGH | NET | COND |
| `REMED-DEVICES-001` | `FileDialog`/`MessageBox` mutex-scoping UAF | HIGH | DEVICES | YES |
| `REMED-MEDIA-001` | `AudioTagParser` integer-overflow bounds checks (32-bit) | HIGH | MEDIA | YES |

## P1

| ID | Title | Sev | Owner | PS |
|---|---|---|---|---|
| `REMED-BUILD-003` | `WILL_FAIL` never adopted project-wide | MEDIUM | BUILD | NO |
| `REMED-BUILD-004` | CI label filters never run the general suite | HIGH | BUILD | YES |
| `REMED-TEST-001` | 3 test files assert confirmed defects as correct | HIGH | TEST | NO |
| `REMED-CORE-001` | `Logger::ToSDLPriority()` mistags Fatal/Error/Warn | HIGH | CORE | YES |
| `REMED-CORE-004` | `Color::PackFromVector4()` unclamped cast (UB) | MEDIUM | CORE | YES |
| `REMED-CORE-006` | `Game::UnloadContent()` dead hook | HIGH | CORE | COND |
| `REMED-CORE-007` | `GraphicsDeviceManager` device-event forwarding gap | HIGH | CORE | COND |
| `REMED-CONTENT-004` | Texture3D reader round-trip returns zeros | MEDIUM | CONTENT | YES |
| `REMED-GFX-004` | `RenderTargetCube` missing `Dispose(bool)` (UAF risk) | MEDIUM | GRAPHICS | YES |
| `REMED-GFX-005` | **Fog formula mirrored — Bgfx, Vulkan, all 15 D3DCommon shaders** | HIGH | GRAPHICS | NO |
| `REMED-GFX-006` | **SkinnedEffect world-normal transform missing on every backend** | HIGH | GRAPHICS | NO |
| `REMED-GFX-007` | EnvironmentMapEffect emissive re-multiply (5 groups) | HIGH | GRAPHICS | NO |
| `REMED-GFX-008` | SkinnedEffect Ambient/Emissive misconsumed (Vulkan, D3D11/12) | HIGH | GRAPHICS | NO |
| `REMED-GFX-009` | SdlGpu fog entirely unimplemented | HIGH | GRAPHICS | NO |
| `REMED-GFX-011` | Vulkan Y-flip missing in 4 effect families | HIGH | GRAPHICS | NO |
| `REMED-GFX-012` | Vulkan `SpriteBatch` transform dropped | HIGH | GRAPHICS | YES |
| `REMED-GFX-058` | Vulkan test shard centre-pixel-only (47 of 71) — structurally blind to mirrors | MEDIUM | GRAPHICS | YES |
| `REMED-GFX-013` | Vulkan scissor inert when RT bound | HIGH | GRAPHICS | COND |
| `REMED-GFX-016` | EasyGL MRT second attachment never drawn | HIGH | GRAPHICS | NO |
| `REMED-GFX-017` | Bgfx default cull mode culls nothing | HIGH | GRAPHICS | YES |
| `REMED-GFX-018` | Bgfx `Clear` ignores `ClearOptions` | HIGH | GRAPHICS | YES |
| `REMED-GFX-019` | WebGPU `SpriteBatch` clip space backbuffer-relative | HIGH | GRAPHICS | YES |
| `REMED-GFX-022` | `EffectParameter` Matrix semantics inverted | HIGH | GRAPHICS | COND |
| `REMED-GFX-043` | `DrawUserPrimitives` declaration never reaches backend | HIGH | GRAPHICS | COND |
| `REMED-NET-002` | `NetworkSessionProperties` unchecked iterator arithmetic | MEDIUM | NET | YES |
| `REMED-MEDIA-002` | `MediaLibrary` object-graph SEGFAULT (6+ backends) | HIGH | MEDIA | YES |

## P2 (44) and P3 (28)

Listed in `MASTER_REMEDIATION_PLAN.md` in ID order under their priority headings. Rather than
duplicating them here, the useful cross-cuts:

### Tasks requiring verification before implementation (11)

These rest on static analysis that was never executed, or on an unresolved question. **Reproduce
first. A finding that fails to reproduce is a valid, recordable outcome — record it, do not force a fix.**

`REMED-CONTENT-004` · `REMED-CONTENT-006` · `REMED-MEDIA-001` · `REMED-MEDIA-003` ·
`REMED-GFX-017` · `REMED-GFX-020` · `REMED-GFX-021` · `REMED-GFX-036` · `REMED-GFX-041` ·
`REMED-GFX-051` · `REMED-CORE-010` · `REMED-TEST-003` · `REMED-TEST-007` · `REMED-GFX-043`

### Tasks needing a project-owner decision before implementation (4)

| ID | Decision required |
|---|---|
| `REMED-GFX-035` | Which GLSL dialect is the contract: auto-upgrade in SdlGpu, document a stricter requirement, or fix only the fixtures? |
| `REMED-CORE-011` | Implement `CNA::Runtime` or delete it? |
| `REMED-BUILD-007` | Licensing question: is `CNA::Internal::Net`'s MIT deliberate? |
| `REMED-BUILD-002` | Does `XactFileGen.hpp` already generate the demo content, making the copy step obsolete? |

### Security-impacting tasks (12)

`REMED-CONTENT-001` (crash-DoS, 2 backends) · `REMED-CONTENT-002` (path traversal: **data loss** +
arbitrary read) · `REMED-CONTENT-003` (OOB heap read / memory disclosure) · `REMED-CONTENT-006`
(stack-exhaustion DoS) · `REMED-NET-001` (remote forgery, no MITM) · `REMED-NET-003` (roster DoS) ·
`REMED-GFX-001` (UAF) · `REMED-GFX-002` (UB) · `REMED-GFX-003` (OOB read) · `REMED-DEVICES-001` (UAF) ·
`REMED-MEDIA-001` (OOB read, 32-bit) · `REMED-GFX-004` (UAF risk)

Indirect: `REMED-BUILD-001` and `REMED-BUILD-004` — both adversarial-input fuzz harnesses
(`XnbContainerFuzzTest`, `LzxDecoderFuzzTest`) are in the currently-unrun set.

### Memory/resource-safety tasks (11)

`REMED-GFX-001` · `REMED-DEVICES-001` · `REMED-CONTENT-001` (confirmed **stack corruption**) ·
`REMED-CONTENT-003` · `REMED-CONTENT-006` · `REMED-MEDIA-001` · `REMED-MEDIA-002` · `REMED-GFX-004` ·
`REMED-GFX-028` · `REMED-GFX-029` · `REMED-DEVICES-002` · `REMED-NET-007`

## By affected backend

A backend's count is not a quality ranking — the most-tested backends surface the most defects. EasyGL
(218 test files) and Vulkan (70) are the two most heavily exercised, and unsurprisingly appear most.

| Backend | Tasks | Notable |
|---|---|---|
| **Vulkan** | 9 | Widest single-backend defect count: Y-flip (4 families), SpriteBatch transform, RT scissor, ambient/emissive, fog, skinned normal, + the Texture2D crash |
| **D3D11 / D3D12** | 8 | Mostly via shared `D3DCommon` — one fix closes both. D3D12 additionally: stencil/scissor inert, occlusion overwrite, 1-CTest coverage gap |
| **EasyGL** | 6 | Default Linux/Emscripten backend. UAF, MRT, skinned normal, PBR normal variant |
| **Bgfx** | 7 | Cull mode, ClearOptions, fog, skinned normal, env-map, 2 unannotated failing tests |
| **SdlGpu** | 7 | Fog absent entirely, GLSL dialect, constructor leak, skinned normal, env-map, cube mips, depth bias |
| **WebGPU** | 4 | Texture2D crash (non-catchable panic), SpriteBatch clip space, skinned normal, env-map |
| **D3D9** | 3 | Object-space fog in custom shaders, PBR skinned normal. Vendored stock effects immune by construction |
| **SdlRenderer** | 3 | Fullscreen crash, stale test expectations, depth-decision ambiguity |
| **Software** | 3 | Depth write/function inert, rotation formula (shared with Dx3), Texture3D |
| **Dx3** | 2 | Rotation defect, resize destroys-before-replace |
| **Headless** | 3 | primitiveCount, Texture3D, WireFrame capability |
| **Ascii / Canvas** | 0 | No MEDIUM+ findings. Both confirmed clean of the `RegisterForWindow` bug |
| **ALL / shared** | 30+ | XNA-facing layer and `IGraphicsBackend` — the highest-leverage fixes |

## By audit source document

| Source | Tasks traced |
|---|---|
| `AUDIT_FINDINGS_INDEX.md` | 71 |
| `AUDIT_CROSS_CUTTING_FINDINGS.md` | 78 |
| `AUDIT_GRAPHICS_BACKEND_MATRIX.md` | 22 |
| `AUDIT_FINAL_REPORT.md` | 19 |
| Per-file `*.audit.md` reports only (**not in any synthesis doc**) | **3** |

Those last three matter: `REMED-CONTENT-006` (stack-exhaustion DoS + two dead security controls),
`REMED-GFX-043` (`DrawUserPrimitives` declaration dropped), and part of `REMED-TEST-005` were
recovered **only** by the exhaustive per-file sweep run while building this plan. They are a gap in
the audit's synthesis layer, not in its per-file work — the evidence was correctly recorded, it just
never propagated upward. See `REMEDIATION_TRACEABILITY.md` § Synthesis gap.
