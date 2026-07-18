# AUDIT_PROGRESS.md — Live Rollup and Resume Point

**If context is lost, resume from here.** Read this file, `AUDIT_MANIFEST.md`, and `AUDIT_DECISIONS.md`, then
continue the work queue — no need to re-derive scope or re-ask the user anything (see `AUDIT_DECISIONS.md` D-P1
through D-P4 for the standing preflight decisions).

## Current phase

**Pass 1 (Inventory and structural reconnaissance) — COMPLETE.**
**Pass 2 (Deep per-file audit) — IN PROGRESS.** Executing as a hybrid: judgment-heavy areas (graphics backends,
core CNA internals, Microsoft.Xna/Devices public API) audited directly by the main agent; large mechanical
batches (examples, tests, tools) fanned out via the Workflow tool per decision D-P1.

Direct-audit work so far: `backend-common` (2/2), `backend-headless` (2/2), `backend-software` (2/2),
`backend-sdlrenderer` (2/2), `backend-dx3` (2/2, static-only per D-P4), `backend-easygl` (2/2, scoped-depth review
of the 4733-line file per its own methodology note) — all fully AUDITED with genuine findings recorded (see
below). **EasyGL's audit produced this pass's most severe finding (F1: dangling window-registry entry on
constructor failure) and independently confirmed the skinned-normal-transform bug the mechanical batch had
already surfaced from the test side (F2/F3).** Next: `backend-webgpu` (last single-file adapter), then the larger
multi-file backends (`backend-ascii`, `backend-canvas`, `backend-d3d11/12/9`, `backend-sdlgpu`, `backend-bgfx`,
`backend-vulkan`, `backend-d3dcommon` — the last of which should specifically check whether `Canvas`/`SdlGpu`/
`WebGPU`'s own `RegisterForWindow` call sites share EasyGL's F1 ordering risk), then CNA core / Microsoft.Xna /
Microsoft.Devices shards.

Three more mechanical-batch Workflows launched in parallel (same proven pattern as easygl): `examples-tests-bgfx`
(98 files, run `wf_bcaa2d48-c2c`), `examples-tests-vulkan` (70 files, run `wf_97caa64c-71d`, after one transient
"model temporarily unavailable" retry), `examples-tests-sdlrenderer` (67 files, run `wf_afb2b5fa-e2b`) — all
in flight as of this update.

Background Workflow COMPLETE: `examples-tests-easygl` (218 files, run ID `wf_0b3830f6-648`, 28 agents, 0 errors,
~1.8M subagent tokens, ~30 min wall-clock) — the first mechanical-batch trial, and it worked very well: all 218
reports genuinely evidence-based (spot-checked 3, all did real formula re-derivation, git-log cross-referencing,
and production-code reading, not boilerplate). Found 4 HIGH-severity issues worth flagging prominently (see
`AUDIT_FINDINGS_INDEX.md`), the most significant being a **production** EasyGL bug (skinned-effect shaders skip
the WorldInverseTranspose normal transform) that no existing test can detect because they all use World=Identity —
queued as a priority verification item for the `backend-easygl` direct audit. A first attempt at this workflow
(run ID `wf_8c4ac6b8-702`, using an `args`-based file list) failed instantly with `args.files` undefined — worked
around by inlining the file list as a literal JS array in the script body instead of passing it via the `args`
parameter; **use this workaround for every future mechanical-batch workflow in this audit** (don't retry passing
file lists via `args`).

Next mechanical-batch candidates (same pattern, same prompt template): `examples-tests-bgfx` (98),
`examples-tests-vulkan` (70), `examples-tests-sdlrenderer` (67), remaining `examples-tests-*` shards, then
`tests-*` shards, then `tools-*` shards.

## Counts (as of this update, 2026-07-18)

- Total tracked files: **2634**
- AUDIT-eligible: **2297** (105 manifest shards)
- EXEMPT: **337** (8 reason categories)
- AUDITED so far: **297** (backend-common ×2, backend-headless ×2, backend-software ×2, backend-sdlrenderer(backend) ×2,
  backend-dx3 ×2, backend-easygl ×2, examples-tests-easygl ×218, examples-tests-sdlrenderer ×67)
- PENDING: **2000** (+ 168 still in flight: bgfx 98, vulkan 70)
- IN_PROGRESS: **0** manifest-tracked
- BLOCKED: **0**

`examples-tests-sdlrenderer` batch complete (67 files, 9 agents, 0 errors). Notable findings: `SpriteBatch::Begin()`
exception-safety bug (general, not backend-specific — permanently wedges the object if a backend call throws
mid-Begin); `GraphicsDevice::SetRenderTargets` mutates tracked state before the backend call that can reject MRT;
two tests with stale expected-throw assertions superseded by FNA-parity fix commit `90f5db2c`; SpriteFont flip
lookup tables sized 3 not 4 (potential OOB read). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the now-3-instance
"mutate state before the fallible call" pattern this reinforces.

## Findings recorded so far (see AUDIT_FINDINGS_INDEX.md for full detail)

- MEDIUM: Headless `HeadlessStatistics::primitiveCount` undercounts instanced draws by `instanceCount`×.
- MEDIUM: Software backend `DepthBufferWriteEnable`/`SetDepthWriteEnabled` have no effect (depth always written).
- MEDIUM: Software backend `DepthStencilState.DepthBufferFunction` ignored (hardcoded LessEqual-equivalent test).
- LOW/INFO: several consistency/dead-code/documentation notes in `IGraphicsBackend.hpp`, Headless, and Software
  reports (missing `final`, one dead constructor overload, missing `default:` in a primitive-count switch, etc.)

## Last completed file

`include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp` (backend-sdlrenderer shard, direct audit).

## Next exact files/subsystem

Start with the graphics backend shards (Task #2) since they're the audit's most emphasized area (prompt §12-14)
and establish the FNA/CNA/backend three-way comparison pattern other shards will reuse. Suggested order (smallest
single-file adapters first, to calibrate report depth/format before tackling the large multi-file backends):

1. `backend-headless`, `backend-software`, `backend-sdlrenderer`, `backend-dx3`, `backend-easygl`, `backend-webgpu`
   (2 files each — single massive adapter + header; note D-6 external-dependency boundary for EasyGL/Dx3)
2. `backend-ascii` (6), `backend-canvas` (8), `backend-common` (2)
3. `backend-d3d11` (20), `backend-d3d12` (26), `backend-sdlgpu` (27)
4. `backend-bgfx` (34), `backend-vulkan` (40), `backend-d3d9` (50, note D-5 vendored-shader boundary),
   `backend-d3dcommon` (46)

Then proceed to Task #3 (CNA core), Task #4 (Microsoft.Xna areas — start with `xna-framework-core` since
Vector/Matrix/Color/BoundingBox etc. are foundational to everything else), Task #5 (Microsoft.Devices), then tests/
tools/examples/docs/build (Tasks #6-9), matching each production-code audit with its paired test shard where
possible (e.g. audit `xna-graphics` and `tests-xna-graphics` close together so FNA-parity findings and test-gap
findings reinforce each other).

## Graphics backend progress

| Backend | Shard(s) | Status |
|---|---|---|
| Ascii | backend-ascii | PENDING |
| Bgfx | backend-bgfx | PENDING |
| Canvas | backend-canvas | PENDING |
| D3D11 | backend-d3d11 | PENDING |
| D3D12 | backend-d3d12 | PENDING |
| D3D9 | backend-d3d9 | PENDING |
| Dx3 | backend-dx3 | PENDING |
| EasyGL | backend-easygl | PENDING |
| Headless | backend-headless | PENDING |
| SdlGpu | backend-sdlgpu | PENDING |
| SdlRenderer | backend-sdlrenderer | PENDING |
| Software | backend-software | PENDING |
| Vulkan | backend-vulkan | PENDING |
| WebGPU | backend-webgpu | PENDING |
| D3DCommon (shared) | backend-d3dcommon | PENDING |
| Common (shared) | backend-common | PENDING |

`AUDIT_GRAPHICS_BACKEND_MATRIX.md`: skeleton only, not yet populated (needs Pass 2 backend evidence first).

## FNA parity progress

Not started (Pass 3, Task #10). Will proceed area-by-area alongside Pass 2's Microsoft.Xna shards rather than
purely as a separate pass, then get a final systematic sweep in Pass 3 to catch anything missed incidentally.

## Cross-cutting investigations open

- `known_bugs.md`'s SpriteBatch Begin/End defect — needs corroboration once `xna-graphics`/`tests-xna-graphics`
  are audited (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).
- EasyGL (4733-line single file) and Dx3/Headless/SdlRenderer/Software/WebGPU (single-file adapters) — maintainability
  question of whether single-file-backend size is a real concern, to be judged once actually read, not assumed.
- External sibling-repo boundary (`easy-gl`, `free-direct`, D-6) — track which findings bottom out at "this lives in
  a different repository" so the final report can state that boundary explicitly rather than silently.

## Commit batches so far

1. `audit: add initial repository inventory, scope, and manifest` (Pass 1 infrastructure) — about to be committed.

## Self-check log

- 2026-07-18: `2297 + 337 == 2634` verified via script (see `AUDIT_SCOPE.md`). Zero `NEEDS_REVIEW` after two
  classifier-fix rounds (D-1, D-2). Zero leftover/uncategorized shards after sharding script run (`gen_master.py`
  printed `leftover shards: []`).
