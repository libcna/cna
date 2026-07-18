# AUDIT_PROGRESS.md — Live Rollup and Resume Point

**If context is lost, resume from here.** Read this file, `AUDIT_MANIFEST.md`, `AUDIT_DECISIONS.md`, and
`AUDIT_CROSS_CUTTING_FINDINGS.md`, then continue the work queue — no need to re-derive scope or re-ask the user
anything (see `AUDIT_DECISIONS.md` D-P1 through D-P4 for the standing preflight decisions the user already gave;
the audit must continue fully autonomously per the original prompt's instructions, no further questions).

## Current phase

**Pass 1 (Inventory and structural reconnaissance) — COMPLETE.**
**Pass 2 (Deep per-file audit) — IN PROGRESS.** Hybrid execution per D-P1: graphics backends, CNA core, and
Microsoft.Xna/Devices public API are audited **directly** by the main agent (judgment-heavy, FNA-parity/
cross-backend work); large mechanical batches (examples, tests, tools) are fanned out via the Workflow tool.

### Direct-audit backend work: 11 of 16 backend shards fully AUDITED

`backend-common` (2/2), `backend-headless` (2/2), `backend-software` (2/2), `backend-sdlrenderer` (2/2, the
backend itself, not the example-test shard), `backend-dx3` (2/2, static-only per D-P4), `backend-easygl` (2/2,
scoped-depth review of the 4733-line file), `backend-webgpu` (2/2, scoped-depth review of the 8805-line file — the
largest in this audit), `backend-ascii` (6/6), `backend-canvas` (8/8), `backend-d3dcommon` (46/46, shared
D3D11/D3D12 infrastructure — every one of the 34 `.hlsl` shader files individually read and reported on, plus all
9 C++ layout/mapping files and the 3 shader-compile-tooling files), **`backend-d3d11` (20/20 — the backend's own
non-shader files; `D3D11GraphicsBackend.cpp` at 1846 lines given a scoped-depth review matching the standard
already applied to EasyGL/WebGPU's giant files)**.

**Cross-cutting `RegisterForWindow` constructor-ordering check is now COMPLETE across all 4 callers**: only
`EasyGL` has the dangling-window-registry-entry bug (that report's F1); `WebGPU`/`Canvas`/`SdlGpu` all correctly
defer registration until construction can no longer fail. `SdlGpu`, however, has a *different*, newly-found
resource-leak risk in the same area (see Findings below) — flagged in the cross-cutting doc but **not yet written
up as a formal per-file finding**, since `backend-sdlgpu`'s own 27-file direct audit has not started yet.

### Remaining backend shards — NOT YET STARTED (5 of 16)

`backend-d3d12` (26 files — shares every `backend-d3dcommon`/`backend-d3d11` finding already recorded; check its
own non-shader files for D3D12-specific bugs), `backend-sdlgpu` (27 — priority: formalize the resource-leak
finding already spotted in its constructor), `backend-bgfx` (34 — priority: confirm/refute whether Bgfx's own
skinned shader shares the normal-transform bug, the last backend not yet confirmed at the source level for that
bug; also check whether it shares the Vulkan-specific `SpriteBatch.Begin(transformMatrix)` no-op bug — Bgfx was
already confirmed to correctly implement `SetTransformMatrix`, so likely not, but double-check), `backend-vulkan`
(40 — priority: full source audit of the already-confirmed `SetTransformMatrix` no-op bug's surrounding code, plus
the already-known fog-formula/skinned-normal/ambient-emissive/Y-flip bugs), `backend-d3d9` (50, note D-5
vendored-shader boundary — the 12 vendored `.fx`/`.fxh` files are EXEMPT, only the C++ consumer code is in-scope).

**For each of these, specifically check**: (1) does its SkinnedEffect/SkinnedPbrEffect shader share the
world-space-normal-transform bug (now confirmed at the shader-source level in 5 of 14 backends: EasyGL, WebGPU,
Vulkan, SdlGpu, D3D11+D3D12 — only Bgfx's own shader source remains unconfirmed); (2) does it share the
fog-formula bug (confirmed in Bgfx/Vulkan/D3D11+D3D12 — D3D11/D3D12 is now the *widest* single instance, ALL 15
fog-capable D3DCommon shaders affected); (3) if it calls `RegisterForWindow`, does its constructor share either
the EasyGL dangling-pointer bug or the SdlGpu resource-leak bug. **`backend-d3d11`/`backend-d3d12`'s own
non-shader files still need this check for (3)** (D3D11 confirmed NOT to call `RegisterForWindow` at all, per
the constructor spot-check already done; D3D12 not yet checked).

### Mechanical-batch Workflow status (examples-tests-* shards)

**COMPLETE**: `examples-tests-easygl` (218, run `wf_0b3830f6-648`), `examples-tests-sdlrenderer` (67, run
`wf_afb2b5fa-e2b`), `examples-tests-bgfx` (98, run `wf_bcaa2d48-c2c`), `examples-tests-vulkan` (70, run
`wf_97caa64c-71d`), `examples-tests-webgpu` (22, run `wf_3e108598-937`), `examples-tests-d3d9` (14, run
`wf_95244dcf-c63`), `examples-tests-sdlgpu` (22, run `wf_bce2a701-d32`), `examples-tests-generic` (24, run
`wf_b52cd363-065`) — all landed, verified on disk, marked AUDITED. sdlgpu/generic committed as part of this
update.

**NOT YET LAUNCHED**: remaining `examples-tests-*` shards (`ascii` 6, `canvas` 2, `d3d11` 3, `d3d12` 2, `dx3` 9,
`headless` 7, `software` 6 — these 7 small shards could be combined into one or two Workflow calls rather than 7
separate ones, given how small they are), and every `examples-demo_*` shard (~30 shards, ~227 files total — demo
applications, not backend integration tests; likely need a different prompt template since they're full sample
games, not single-feature pixel tests). Also **all `tests-*` shards** (~350 files: `tests-xna-*` × 8,
`tests-cna-*` × 4, `tests-microsoft-devices`, `tests-misc`) and **all `tools-*` shards** (~124 files, 10 shards)
have not been touched at all yet — same mechanical-batch pattern applies.

### Reusable Workflow script pattern (for every future mechanical batch)

Copy the structure used in every batch above: `meta` block, `FILES` as an inlined literal JS array (**never pass
file lists via the `args` parameter — this failed instantly with `args.files` undefined on the very first
attempt, run `wf_8c4ac6b8-702`**), a `RESULT_SCHEMA` requiring `{path, report_written, verdict,
high_or_above_findings[]}` per file, a `buildPrompt(batch)` function instructing agents to read
`AUDIT_CHECKLIST.md`/`AUDIT_SCOPE.md`/`AUDIT_DECISIONS.md`/`AUDIT_CROSS_CUTTING_FINDINGS.md` first (the last one
is important — it tells agents what cross-backend bugs to specifically check for) plus one strong example report
as a template, the anti-boilerplate rule, and explicit backend/production-source paths to cross-check against;
`pipeline()` over batches of 5-8 files each; `phase('Audit')`. After completion: read the full `journal.jsonl` (not
just the truncated notification text) via a Python script to get every file's complete result, verify every
report file exists on disk before trusting the count, mark manifest rows AUDITED via
`/tmp/.../scratchpad/mark_audited.py <paths...>` (this script and the shard-key→path mapping in
`shards.json`/`classified.json` live in the scratchpad directory from Pass 1 — if that scratchpad is gone,
regenerate from `AUDIT_MANIFEST.md`'s shard files, which list every path per shard already).

## Counts (as of this update, 2026-07-18, mid-session)

- Total tracked files: **2634**
- AUDIT-eligible: **2297** (105 manifest shards)
- EXEMPT: **337** (8 reason categories)
- AUDITED so far: **629** (backend-common ×2, backend-headless ×2, backend-software ×2, backend-sdlrenderer(backend) ×2,
  backend-dx3 ×2, backend-easygl ×2, backend-webgpu ×2, backend-ascii ×6, backend-canvas ×8, backend-d3dcommon ×46,
  backend-d3d11 ×20, examples-tests-easygl ×218, examples-tests-sdlrenderer ×67, examples-tests-bgfx ×98,
  examples-tests-vulkan ×70, examples-tests-webgpu ×22, examples-tests-d3d9 ×14, examples-tests-sdlgpu ×22,
  examples-tests-generic ×24)
- PENDING: **1668**
- IN_PROGRESS: **0** manifest-tracked
- BLOCKED: **0**

**~27.4% AUDITED so far** (629/2297).

`backend-d3d11` (20 files — the backend's own non-shader files) is now fully audited. Key results: (1)
independently confirmed, at the C++ constant-buffer-fill level, that `SkinnedEffect` genuinely never sends
`EmissiveColor` (not just a shader-side omission — there's nowhere in the wire format to put it), while
`AmbientColor` IS correctly forwarded, and PBR/unskinned-lit paths both correctly forward `EmissiveColor`; (2) a
**major new cross-cutting discovery, found incidentally**: `VulkanSpriteBatchBackend` never overrides
`SetTransformMatrix()` at all (confirmed via exhaustive grep — zero matches anywhere in the Vulkan backend), so
`SpriteBatch.Begin(transformMatrix)` is silently a no-op on Vulkan specifically — every other backend checked
(EasyGL, Bgfx, D3D9, D3D11, WebGPU, SdlGpu, SdlRenderer, Canvas, Dx3, Software, Headless, Ascii-via-delegation)
correctly applies it; (3) a 3rd architecture-level finding: `IGraphicsBackend`'s `Apply*State()` methods
consistently omit several real XNA state fields (`SamplerState.AddressW`, `BlendState.ColorWriteChannels`,
`RasterizerState.MultiSampleAntiAlias`) across every backend, not just D3D11; (4) a plausible (unconfirmed)
`SetDataOptions::NoOverwrite` synchronization risk shared with EasyGL (`SetDataWithOptions` has no destination-offset
parameter anywhere in the whole call chain, so `NoOverwrite` can't provide real streaming semantics); (5) a 2nd
confirmed instance (after SdlGpu) of "mip regeneration touches all 6 cube faces even when only one changed," this
time in `RenderTargetCube` rather than `TextureCube`. Two genuine positive findings: D3D11's `SpriteBatch` is
correctly render-target-relative (unlike WebGPU's confirmed bug) and correctly implements the transform matrix
(unlike Vulkan's newly-confirmed bug). `D3D11GraphicsBackend.cpp` (1846 lines) given a scoped-depth review
matching the EasyGL/WebGPU standard; the untraced portion (full non-skinned draw-variant dispatch, device-lost
recovery, resize handling) is a known gap for a future deeper pass.

`backend-d3dcommon` (46 files — shared D3D11/D3D12 shader source + layout/mapping infrastructure) is now fully,
directly audited: every one of the 34 `.hlsl` files individually read line-by-line (not inferred from test
behavior), confirming this shard shares 4 cross-cutting defects with maximum severity/breadth: (1) **ALL 15**
fog-capable vertex shaders share the mirrored Task-1111 formula — the single widest instance of this bug in the
whole audit; (2) **ALL 5** skinned vertex shaders share the world-space-normal-transform omission (4 complete
omissions + 1 raw-World-not-inverse-transpose), while the 3 unskinned lit shaders in the same directory get it
correctly right, proving the bug is a skinning-specific oversight; (3) `env_map3d.frag.hlsl` shares the
`EnvironmentMapEffect` emissive-remultiply bug (5th confirmed backend-group overall); (4) all 4 `SkinnedEffect`
fragment shaders lack an `EmissiveColor` cbuffer field entirely (narrower than but related to the already-confirmed
Vulkan-specific ambient/emissive gap). Also found: 2 stale "NOT YET WIRED" doc comments in
`D3DConstantBuffers.hpp` contradicted by actual, current backend usage. Positive findings: D3D11/D3D12 correctly
and deliberately do NOT share Vulkan's `EnvironmentMapEffect` Y-flip bug (a genuine, well-documented backend
difference, not an oversight); `D3DStateMapping.cpp`'s `TextureFilter` table is the most complete found in this
audit (no collapsed/simplified compound-filter cases, unlike SdlGpu's disclosed gap in the same area).

`examples-tests-webgpu` (22 files) and `examples-tests-d3d9` (14 files) batches both complete, 0 errors. WebGPU
batch added a THIRD backend to the EnvironmentMapEffect emissive bug (Bgfx, now WebGPU too) and a new
WebGPU-specific bug (SpriteBatch's clip-space mapping is always backbuffer-relative, breaking sprite placement
when drawing into a differently-sized render target). D3D9 batch produced a valuable **nuanced** result: D3D9's
*vendored* stock-effect shaders share NEITHER the fog-formula NOR the skinned-normal-transform bug (clean) — but
its own CNA-original custom PBR/skinned HLSL shaders (`PbrSkinned3D.hlsl`, `Pbr3D.hlsl`,
`SkinnedVertexColor3D.hlsl`) share both the skinned-normal-transform bug (4th confirmed instance) AND a *second*,
distinct "object-space-only fog" defect (ignores World/View for the Z used in fog, separate from the Task-1111
mirrored-formula bug) that matches a previously-recorded EasyGL memory note about the same mistake pattern.

`examples-tests-sdlgpu` (22 files) and `examples-tests-generic` (24 files) batches both complete, 0 errors. SdlGpu
batch confirmed a 4th backend for the EnvironmentMapEffect emissive bug and a 4th backend (SdlGpu, plus D3D11/D3D12
via direct D3DCommon source reading done in parallel — see below) for the skinned-normal-transform bug. **The
generic batch's most important result: `EasyGL_AvatarRenderer_TintRouting` is a real, currently-failing CTest**
(independently re-confirmed by direct build+execution during this update, not just relayed from the subagent —
`ctest -R EasyGL_AvatarRenderer_TintRouting` → `Failed`), plus a genuine, non-backend-specific production defect
(`SpriteFont::MeasureString`/`SpriteBatch::DrawString` unordered_map UB on a bad `DefaultCharacter`), and a 3rd
confirmation of the `VertexColorEnabled` bare-public-field issue.

**Also landed this update (direct source reading, not a mechanical batch): the shared `D3DCommon` skinned shaders**
(`skinned3d.vert.hlsl`, `pbr_skinned3d.vert.hlsl` — compiled into BOTH `D3D11` and `D3D12`) confirmed to share both
the skinned-normal-transform bug and the fog-formula bug, found while spot-checking `D3D11GraphicsBackend.cpp`
ahead of that shard's own full audit. `skinned3d.vert.hlsl`'s own header comment explicitly states it was "Ported
line-by-line from `.../Vulkan/shaders/skinned3d.vert.glsl`" — the clearest first-hand proof yet of the
cross-backend porting chain. This raises the skinned-normal-transform bug's shader-source-confirmed count to 5 of
14 backends (EasyGL, WebGPU, Vulkan, SdlGpu, D3D11+D3D12) and the fog-formula bug's to 3 backend-groups (Bgfx,
Vulkan, D3D11+D3D12). `skinned3d_vertexlit.vert.hlsl` and both `.frag.hlsl` siblings in the same D3DCommon
directory still need a full read (queued for the `backend-d3dcommon`/`backend-d3d11`/`backend-d3d12` shard
audits) to confirm whether they share the same pattern.

## Major discoveries so far (see AUDIT_FINDINGS_INDEX.md and AUDIT_CROSS_CUTTING_FINDINGS.md for full detail)

1. **Fog formula bug — this audit's single most widely-confirmed defect.** The pre-Task-1111 fog formula (proven
   wrong by this project's own XNA-oracle diff, commit `74ad3bae`) was fixed in EasyGL but never ported to Bgfx,
   Vulkan, or D3D11/D3D12 (shared `D3DCommon` shader source, confirmed by direct read). Confirmed in 3
   backend-groups now. Priority: check remaining backends (D3D9/SdlGpu/Software/SdlRenderer/Dx3/Canvas/Ascii/
   Headless) for the same formula.
2. **Skinned-effect world-space-normal-transform bug — confirmed at the shader-source level in 5 of 14 backends**
   (EasyGL, WebGPU, Vulkan, SdlGpu, D3D11+D3D12 via shared `D3DCommon`). WebGPU's and D3D11/D3D12's own shader
   comments each explicitly admit a deliberate line-for-line port from an earlier (buggy) instance, including the
   bug — direct proof of two separate porting chains (EasyGL→WebGPU, Vulkan→D3DCommon). Only Bgfx's own shader
   source remains unconfirmed (only inferred so far from masked test behavior). The narrower "raw World instead of
   inverse-transpose" variant is separately confirmed in EasyGL, SdlGpu, D3D9, and D3D11/D3D12's PBR-skinned
   shaders specifically.
3. **`EnvironmentMapEffect` emissive/diffuse re-multiply bug — confirmed in 4 backends** (Bgfx, WebGPU, Vulkan,
   SdlGpu). Remaining unchecked: D3D9/D3D11/D3D12/Software/SdlRenderer/Dx3/Canvas/Ascii/Headless.
4. **`EasyGL_AvatarRenderer_TintRouting` is a real, currently-failing CTest**, registered with no `WILL_FAIL`
   annotation — independently re-confirmed by direct build+execution (not just relayed from a subagent). The
   sibling Vulkan variant passes only because a separate, independently-confirmed defect cancels out the same
   miscalibration. Raises the priority of a full CTest-registration sweep (Pass 6).
5. **`SpriteFont::MeasureString`/`SpriteBatch::DrawString` have a reachable `unordered_map::end()` dereference
   (undefined behavior)** when `DefaultCharacter` is set (unvalidated) to a character absent from the font's own
   map — a genuine, non-backend-specific FNA-parity gap (FNA throws `KeyNotFoundException`).
6. **EasyGL F1 (HIGH): dangling window-registry pointer on constructor failure** — the single most severe finding
   of the audit so far (a real use-after-free path via `SdlInputBridge`/`Mouse`). Confirmed NOT present in
   WebGPU/Canvas/SdlGpu (all three defer registration correctly).
7. **SdlGpu: constructor resource leak** (new, distinct from #6) if any of 10 sequential shader/pipeline-creation
   calls throws — no try/catch, unlike WebGPU's model-example pattern. Needs formal write-up when `backend-sdlgpu`
   is directly audited.
8. Several Vulkan-specific bugs (ambient/emissive dropped for skinned models, missing Y-flip in
   EnvironmentMapEffect causing vertical mirroring, scissor ignored on render-target passes) and Bgfx-specific bugs
   (`Clear()` ignores `ClearOptions` and always wipes color+depth+stencil; a vertex-format test whose entire
   subject function is dead code in production).
9. Two known-failing CTest targets registered with no `WILL_FAIL` annotation (Bgfx) — plus item #4 above (EasyGL).
10. `BasicEffect::VertexColorEnabled` is a bare public field with no property wrapper, violating the project's own
    C# property convention — confirmed independently 3 times now (Bgfx, Vulkan, generic-tests batches).
11. `SpriteBatch::Begin()` and `GraphicsDevice::SetRenderTargets` both mutate tracked state before a backend call
    that can throw/reject — a recurring "mutate before the fallible call" shape, 3 confirmed instances now.
12. **Recurring documentation rot**: header comments describing "known bugs"/stale expected-throw behavior,
    confirmed across 4 independent mechanical batches (EasyGL, SdlRenderer, Bgfx, Vulkan) — not incidental to one
    subsystem.
13. Backend-specific, lower-severity findings: Headless statistics undercount instanced draws; Software backend
    ignores `DepthBufferWriteEnable`/`DepthBufferFunction`; Dx3 resize failure leaves the backend unusable; Ascii's
    forced blend state isn't restored after `Present()`.
14. **Vulkan-specific (HIGH, newly confirmed): `SpriteBatch.Begin(transformMatrix)` is silently dropped** —
    `VulkanSpriteBatchBackend` never overrides `SetTransformMatrix()` (confirmed via exhaustive grep, zero
    matches). Found incidentally while auditing D3D11's own SpriteBatch, whose header comment made this exact,
    independently-verified-true claim. Every other backend checked correctly applies it.
15. **Architecture-level: `IGraphicsBackend`'s `Apply*State()` methods omit several real XNA state fields**
    across every backend (`SamplerState.AddressW`, `BlendState.ColorWriteChannels`,
    `RasterizerState.MultiSampleAntiAlias`) — 3 confirmed instances, all honestly self-disclosed in D3D11's own
    source comments as pre-existing, not backend-introduced.
16. A plausible (not reproduced) `SetDataOptions::NoOverwrite` synchronization risk shared by D3D11 and EasyGL:
    `SetDataWithOptions` has no destination-offset parameter anywhere in the call chain, so `NoOverwrite` cannot
    provide genuine streaming semantics — every write touches the same bytes a prior write did.
17. A 2nd confirmed instance (after SdlGpu's `TextureCube`) of "cube mip regeneration touches all 6 faces even
    when only one changed" — this time in D3D11's `RenderTargetCube`.

## Last completed file

`backend-d3d11` shard — all 20 files (the backend's own non-shader `.cpp`/`.hpp` files) fully audited and written
up, marked AUDITED. `D3D11GraphicsBackend.cpp` (1846 lines) given a scoped-depth review (constructor/destructor,
`RegisterForWindow` absence, MRT finalization, skinned/PBR ambient-vs-emissive constant-buffer fill all verified
in full; the remaining non-skinned draw-dispatch variants, device-lost recovery, and resize handling were not
exhaustively traced). This closes out `backend-d3d11` entirely.

## Next exact action

1. **Commit this update** (`AUDIT_CROSS_CUTTING_FINDINGS.md`, `AUDIT_FINDINGS_INDEX.md`, `AUDIT_PROGRESS.md`, the
   20 new `.audit.md` reports under `audit/include/.../D3D11/` and `audit/src/.../D3D11/`, and the updated
   `backend-d3d11` manifest shard file) as one logical batch, verifying staged paths are `audit/`-only first.
2. Move to `backend-d3d12` (26 files). Since `backend-d3dcommon` (shared shaders) and `backend-d3d11` (parallel
   non-shader design) are both now fully audited, D3D12's own files should go faster — check specifically for:
   (a) does D3D12 share every `backend-d3dcommon` finding (yes, by construction, since it compiles the same
   shaders); (b) does its own C++ layer share D3D11's `SkinnedEffect`-missing-`EmissiveColor` gap (very likely,
   same shared `D3DSkinnedExtraConstants` struct) and its correct `SpriteBatch` transform/render-target-relative
   behavior (worth confirming, not just assuming); (c) does it call `RegisterForWindow` and if so does its
   constructor share either the EasyGL or SdlGpu-class ordering risk (D3D11 confirmed it never calls
   `RegisterForWindow` at all — check whether D3D12 follows the same pattern).
3. Then `backend-sdlgpu` (27 — formalize the constructor resource-leak finding as a proper per-file report),
   `backend-bgfx` (34, backend source itself — priority: confirm/refute whether Bgfx's own skinned shader shares
   the normal-transform bug, the one backend not yet confirmed at the source level), `backend-vulkan` (40,
   priority: full source audit of the already-confirmed `SetTransformMatrix` no-op bug's surrounding code),
   `backend-d3d9` (50, note D-5 vendored shader boundary).
4. After all remaining backend shards: Task #3 (CNA core), Task #4 (Microsoft.Xna areas — start with
   `xna-framework-core` 78, then `xna-graphics` 191 the largest, prioritizing `SpriteFont.cpp`/`SpriteBatch.cpp`
   given the UB finding above), Task #5 (Microsoft.Devices), then tests/tools/examples/docs/build (Tasks #6-9),
   matching each production-code shard with its paired test shard where possible so findings reinforce each other.

## Graphics backend progress

| Backend | Shard(s) | Status |
|---|---|---|
| Ascii | backend-ascii | **AUDITED** |
| Bgfx | backend-bgfx | PENDING (examples audited; backend source not yet) |
| Canvas | backend-canvas | **AUDITED** |
| D3D11 | backend-d3d11 | **AUDITED** (20/20 own files + shared D3DCommon shaders it compiles, 46/46; no RegisterForWindow; confirmed correct SpriteBatch transform/render-target-relative behavior) |
| D3D12 | backend-d3d12 | PENDING (examples audited + shared D3DCommon shaders it compiles all fully audited; own 26 non-shader files not yet) |
| D3D9 | backend-d3d9 | PENDING (examples audited; backend source not yet) |
| Dx3 | backend-dx3 | **AUDITED** (static-only, D-P4) |
| EasyGL | backend-easygl | **AUDITED** |
| Headless | backend-headless | **AUDITED** |
| SdlGpu | backend-sdlgpu | PENDING (examples audited, confirmed env-map + skinned-normal bugs; constructor spot-checked — see Findings #7) |
| SdlRenderer | backend-sdlrenderer | **AUDITED** |
| Software | backend-software | **AUDITED** |
| Vulkan | backend-vulkan | PENDING (examples audited; backend source not yet) |
| WebGPU | backend-webgpu | **AUDITED** |
| D3DCommon (shared) | backend-d3dcommon | **AUDITED** (46/46: all 34 `.hlsl` shaders + 9 C++ files + 3 tooling files) |
| Common (shared) | backend-common | **AUDITED** |

`AUDIT_GRAPHICS_BACKEND_MATRIX.md`: still skeleton only — do not populate until all 16 backend shards are
directly audited (Pass 4 depends on this).

## FNA parity progress

Not started as a dedicated pass yet (Pass 3, Task #10), but two major systemic FNA-parity gaps (fog formula,
skinned-normal-transform) have already been found incidentally via backend audits + mechanical test batches. This
is ahead of where a dedicated Pass 3 would have started from scratch — when Pass 3 formally begins, start by
consolidating what's already known rather than re-deriving it.

## Cross-cutting investigations open

- `known_bugs.md`'s SpriteBatch Begin/End defect — needs corroboration once `xna-graphics`/`tests-xna-graphics`
  are audited. **Possibly already partially corroborated**: the SdlRenderer batch found a *different* SpriteBatch
  exception-safety bug (`Begin()` wedging on a throwing backend call) — confirm whether these are the same issue
  or two distinct ones when `SpriteBatch.cpp` itself is audited.
- External sibling-repo boundary (`easy-gl`, `free-direct`, D-6) — track which findings bottom out at "this lives
  in a different repository" for the final report.
- Full CTest-registration sweep (Pass 6) to find every currently-failing/expected-to-fail test, given 2 were
  found by accident in the Bgfx batch and a 3rd (`EasyGL_AvatarRenderer_TintRouting`, independently re-confirmed
  by direct build+execution — see Major discoveries #4) in the generic batch, all with no `WILL_FAIL` annotation
  — there is a real, demonstrated pattern here, not just a theoretical risk.
- `BasicEffect::VertexColorEnabled`'s bare-public-field issue — check whether it's the only such lapse when
  `xna-graphics`/`BasicEffect.hpp` is directly audited.
- `IGraphicsBackend.hpp`'s own audit should confirm/expand the 3 confirmed `Apply*State()` missing-field gaps
  (`AddressW`, color-write-mask, `MultiSampleAntiAlias`) and check whether `SamplerState.hpp`/`BlendState.hpp`/
  `RasterizerState.hpp` in `xna-graphics` have any C++-side plumbing for these fields that this audit hasn't yet
  traced, or whether they're genuinely dead properties project-wide.
- Vulkan's own full backend audit (still PENDING) should confirm the `SetTransformMatrix` no-op bug is the only
  instance of its kind in that backend, and check `VulkanSpriteBatchBackend`'s `Draw()` overloads for whether a
  render-target-relative viewport bug (WebGPU's confirmed defect) also applies there.

## Commit batches so far (chronological)

1. `audit: add initial repository inventory, scope, and manifest` — Pass 1 infrastructure.
2. `audit: review Common backend contract, Headless and Software backends`
3. `audit: review EasyGL example test shard (218 files, via mechanical batch)`
4. `audit: review SdlRenderer backend`
5. `audit: review Dx3 (DirectDraw) backend`
6. `audit: review EasyGL backend (largest single file, 4733 lines)`
7. `audit: review SdlRenderer example test shard (67 files, via mechanical batch)`
8. `audit: review WebGPU backend (largest file in the audit, 8805 lines)`
9. `audit: review Ascii backend (6 files)`
10. `audit: review Bgfx example test shard (98 files, via mechanical batch)`
11. `audit: review Vulkan example test shard (70 files, via mechanical batch)`
12. `audit: review Canvas backend (8 files)`
13. `audit: resolve RegisterForWindow cross-cutting check, refresh progress`
14. `audit: review WebGPU and D3D9 example test shards (36 files, via mechanical batches)`
15. `audit: review SdlGpu and generic example test shards (46 files) + D3DCommon skinned-shader findings`
16. `audit: review D3DCommon backend (46 files, shared D3D11/D3D12 shader + layout infrastructure)`
17. `audit: review D3D11 backend (20 files, own non-shader implementation)`
18. *(next commit: continue backend-d3d12 direct audit — own non-shader files)*

## Self-check log

- 2026-07-18 (session start): `2297 + 337 == 2634` verified via script (see `AUDIT_SCOPE.md`). Zero
  `NEEDS_REVIEW` after two classifier-fix rounds (D-1, D-2). Zero leftover/uncategorized shards after sharding
  script run (`gen_master.py` printed `leftover shards: []`).
- 2026-07-18 (mid-session): every mechanical-batch result verified against disk (no trusting the notification
  summary alone — each batch's file list was checked with a Python existence loop before marking AUDITED) for all
  4 completed batches (easygl, sdlrenderer, bgfx, vulkan). Zero missing files in any batch.
