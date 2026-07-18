# AUDIT_PROGRESS.md — Live Rollup and Resume Point

**If context is lost, resume from here.** Read this file, `AUDIT_MANIFEST.md`, and `AUDIT_DECISIONS.md`, then
continue the work queue — no need to re-derive scope or re-ask the user anything (see `AUDIT_DECISIONS.md` D-P1
through D-P4 for the standing preflight decisions).

## Current phase

**Pass 1 (Inventory and structural reconnaissance) — COMPLETE.**
**Pass 2 (Deep per-file audit) — NOT STARTED. Next action: begin Task #2 (graphics backend shards) or Task #3
(CNA core shards) — either order is fine, they don't depend on each other.**

## Counts (as of Pass 1 completion, 2026-07-18)

- Total tracked files: **2634**
- AUDIT-eligible: **2297** (105 manifest shards)
- EXEMPT: **337** (8 reason categories)
- AUDITED so far: **0**
- PENDING: **2297**
- IN_PROGRESS: **0**
- BLOCKED: **0**

## Last completed file

None yet — Pass 1 produced infrastructure only, no per-file `.audit.md` reports exist yet.

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
