# CNA future development roadmap

Date: 2026-08-09 (updated 2026-08-10 — all pre-expansion preparation is complete and public:
the final physical module/package layout, the renderer terminology normalization and the
module-owned examples are all promoted to `develop`)

> **THIS DOCUMENT IS A ROADMAP, NOT AUTHORIZATION TO START FUTURE WORK.**
>
> Nothing described under FUTURE below may be started because it is written here. Current tasks
> always take precedence. Each major future phase requires a fresh explicit owner instruction and
> its own acceptance criteria. Roadmap details may evolve when evidence changes.

## Status at the time this document was written

Distinguish these three clearly. Everything in the FUTURE column is unstarted.

| Horizon | Item | State |
|---|---|---|
| **CURRENT** | Post-audit integration campaign promoted to `develop` | 21/21 lanes accepted, 0 pending, Batch 0–6 complete, `FINAL-STAB-001` complete |
| **CURRENT** | Public CNA renderer identities | **46** — mechanically counted from `CNA/GraphicsRendererType.hpp` (modules/core) and `cmake/RendererSelection.cmake`, pinned by `scripts/check_renderer_identities.py`. 41 at the 2026-08-10 pre-expansion promotion, **−1** for the `ASCII` renderer identity (migrated to a renderer-neutral post-process effect), **+6** for `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG` and `PORTABLEGL`, all integrated on the `11branches` integration branch |
| **CURRENT** | Phase 1.5 — naming normalization (backend→renderer, DIRECTX*, OPENGLES3, CNAEXT) | **COMPLETE AND PUBLIC** — implemented on `feature/renderer-naming-normalization` (endpoint `16f76cf1a`) and promoted to `develop` on 2026-08-10 as part of the pre-expansion fast-forward. See `docs/RendererNamingMigration.md`. Renderer count unchanged at 41 |
| **CURRENT** | Phase 1.6 — module-owned examples | **COMPLETE AND PUBLIC** — implemented on `feature/module-examples` (endpoint `675e04c7a`, a descendant of the naming endpoint) and promoted in the same fast-forward. All 1373 tracked example files now live with their owning module, registered by 44 module-local `examples/CMakeLists.txt` files; only the shared `examples/golden/` oracle corpus stays at repository level. See `docs/physical-modules.md` §"Module examples" and `modularization/module-examples/` |
| **CURRENT** | Phase 1 — CNA modularization | **COMPLETE AND PROMOTED** in three stages, all now on public `develop`: target graph + physical `src/` layout (`41028e995`), modular sharp-runtime consumption (`ea61123e6`), and the owner-requested **final physical module/package layout** (`modules/<name>/{include,src,tests}` monorepo, MODULARIZATION_PLAN.md §11–§11.2) promoted 2026-08-10 by fast-forward to `3ecbbce72` (tree unchanged by the promotion). The modularization campaign is DONE |
| **CURRENT** | Phase 2 — renderer expansion (OPENGLES2 + 13 new renderers) | **in progress**: six additions — `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG`, `PORTABLEGL` — are implemented on their own lanes and integrated on `11branches`. The remaining planned additions are untouched and each still requires its own explicit owner instruction |
| **FUTURE** | Phase 3 — complete XNA sample campaign | **not started**; blocked on Phases 1–2 |
| **FUTURE** | Phase 4 — historical plan/audit review | **not started**; blocked on Phase 3 |
| **CURRENT** | Phase 5 — glTF correctness campaign | **in progress on `feature/gltf`, ahead of its stated Phase 4 dependency** (started 2026-08-11 from `gltfissues.md`'s analysis and a forensic audit that reproduced eight defects, D1–D8, every one of which produced a *model that rendered*). The working record is `plan_gltf.md`: 460 rows, **448 closed**, and all eight audit defects `fixed` in the corpus defect ledger. The evidence base is a generated 145-asset corpus, the exact L0–L6 numerical ladder run per commit under ASan+UBSan, a required production-viewer OPENGLES3 L7 gate with 137 deterministic PNGs plus 8 deterministic safe rejections, and the completed 14-row pinned Khronos Gate C viewer retake (`docs/gltf-conformance.md`). **The milestone is NOT declared:** §27.1 row 13 remains partial by the recorded application-policy decision, while `GLTF-458` (**GLTF CORE 2.0 CORRECT**) and `GLTF-459` (**GLTF ROBUST**) are still open, so the line below stands |

Explicitly **not** true today, and not to be stated as true anywhere:

- CNA does **not** have 55 renderers. It has **46** (41 at the pre-expansion promotion, minus the
  removed `ASCII` renderer identity, plus `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG`
  and `PORTABLEGL`).
- Modularization is complete **and promoted**, including the final physical module/package
  layout: `develop` is a module-oriented monorepo as of 2026-08-10 (`41028e995` target graph +
  physical layout, then `3ecbbce72` `modules/<name>/{include,src,tests}`; both no-loss-proven —
  see `MODULARIZATION_PLAN.md` §10 and §11–§11.2). The sharp-runtime side is closed too: the
  remediation snapshot `7888a29f` was merged
  into sharp-runtime `develop` (`81624983`, published) and CNA consumes the modular
  `SharpRuntime::<Component>` targets; the audit-remediation campaign itself continues on its
  feature branch and later increments merge separately.
- The XNA samples do **not** all pass. The corpus has not been revisited.
- glTF is **not** corrected — the campaign is under way, not finished, and this line is retired only when `GLTF-458`
  (**GLTF CORE 2.0 CORRECT**) is declared with the evidence its own row demands. The separate
  `cna-gltf-viewer` **has** completed the pinned 14-row Gate C retake; the remaining CORE blocker is
  §27.1 row 13's recorded application-owned alpha/double-sided policy boundary, not missing viewer evidence.
  What *is* true today is narrower and checkable: the import path is corrected for everything
  `plan_gltf.md` marks closed, what it still cannot carry is enumerated in `docs/gltf-limitations.md`,
  and each such loss is reported rather than silent.

Phases are sequential. Each depends on its predecessor being completed and stabilized.

---

## Phase 1 — CNA modularization (COMPLETE AND PROMOTED)

Completed as the CNA development campaign following the post-audit integration promotion, and
promoted into `develop` on 2026-08-10 (`41028e995`).

Goals:

- split the monolithic build/project into coherent modules;
- establish explicit dependency boundaries between those modules;
- isolate renderer implementations into clean renderer modules;
- preserve public behavior;
- avoid semantic changes unless a proven dependency cycle requires a minimal architecture
  correction;
- establish minimal-link/build tests, so a module's real dependency set is enforced rather than
  assumed;
- preserve existing test and sanitizer coverage.

Exit condition: modularization is completed **and stabilized**, and one stable modularized
`develop` commit is established as the common base for renderer expansion.

**Exit condition met (2026-08-10).** The stable modularized `develop` base for renderer expansion
is the current public `develop` head; the campaign record, the promotion evidence and the bounded
post-promotion gate are in `MODULARIZATION_PLAN.md` §9–§10 and `NEXT.md`.

**Dependency:** Phase 2 must not begin before this exit condition holds. Starting renderer
expansion against the pre-modular structure would create 14 new renderers that then have to be
migrated.

---

## Phase 2 — renderer expansion (FUTURE)

Unblocked by Phase 1's promotion, but **not started** — and, like every phase here, it requires a
fresh explicit owner instruction before any work begins. It must start from the stable modularized
public `develop` base, not from an older pre-modularization commit.

Current public renderer count before this phase: **41** (the 2026-08-10 pre-expansion promotion).
Six of the planned additions have since been implemented on their own lanes and integrated on the
`11branches` integration branch — `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG` and
`PORTABLEGL`. Together with the removal of the `ASCII` renderer identity (migrated to a
renderer-neutral post-process effect, outside this phase's scope) that brings the live count to
**46**. The remaining items in this table are still unstarted and each still requires its own
fresh explicit owner instruction.

This phase adds one new public OpenGL ES 2 path plus 12 planned new renderer implementations.

### Planned additions

| # | Public identity | Notes |
|---:|---|---|
| 1 | `OPENGLES2` | **INTEGRATED** (`feature/opengles2`) — public CNA OpenGL ES 2 renderer/profile, reusing the EasyGL ES 2 capability as its own fifth GL profile (`CNA_GL_PROFILE_OPENGLES2`) with truthful ES 2.0 capability and platform reporting. See `plan_opengles2.md` / `docs/opengles2-renderer.md`. |
| 2 | `FNA3D` | **INTEGRATED** (`feature/fna3d`) — FNA3D pinned at release 26.08, executing XNA's own compiled stock effects through MojoShader; selects SDL_GPU/Direct3D 11/OpenGL at runtime. See `docs/fna3d-renderer.md` and `plan_fna3d.md`. |
| 3 | `OPENVG` | **INTEGRATED** (`feature/openvg`) — OpenVG 1.1 via ShivaVG on a real desktop OpenGL context. 2D-only (no 3D pipeline, no render targets). See `docs/openvg-renderer.md`. |
| 4 | `SVG_DOM` | **INTEGRATED** (`feature/svgdom`) — Emscripten-only, 2D-only; renders `SpriteBatch` output as real pooled SVG DOM elements (`<svg>`/`<image>`/`feColorMatrix`), distinct from both `CANVAS` (rasterized) and `HTML_DOM` (CSS `<div>`s). See `docs/svg-dom-renderer.md`; real-browser validation remains an external Emscripten-SDK gate. |
| 5 | `IGL` | Facebook IGL — https://github.com/facebook/igl |
| 6 | `NVRHI` | NVIDIA NVRHI. |
| 7 | `KORE` | Kode/Kore — https://github.com/Kode/Kore |
| 8 | `METHANEKIT` | MethaneKit RHI. |
| 9 | `LINAGX` | LinaGX. |
| 10 | `TEMPEST` | Tempest. |
| 11 | `THORVG` | ThorVG. |
| 12 | `PORTABLEGL` | **INTEGRATED** (`feature/portablegl`) — CPU software OpenGL 3.x-ish pipeline via `rswinkle/PortableGL`; no GPU or window required. See `docs/portablegl-renderer.md`. |
| 13 | `REACT_DOM` | A distinct React/DOM-oriented CNA rendering implementation, **only if** the final architecture proves it can truthfully satisfy a useful CNA graphics contract. It must not be counted merely as a conceptual alias of an existing identity. |

That is `OPENGLES2` + 12 new renderer implementations = **13 additions** in this table, of which
six are already integrated (the five marked INTEGRATED above, plus `BLEND2D`, which was delivered
on its own lane and is no longer listed as a planned addition).

### Target count

    46 live today + 8 still-unstarted additions from the table above = 54 public CNA renderer
    identities if every remaining planned identity lands

(The original 55 target assumed the `ASCII` renderer identity would remain; it was removed in
favour of a renderer-neutral post-process effect, so the arithmetic ceiling is one lower.)

**This count is a TARGET, not an invariant.** After implementation, recount public identities
mechanically from the actual registry and report the truthful result. If `REACT_DOM` cannot
truthfully satisfy a CNA graphics contract, or any other planned identity is withdrawn, the real
number is lower and the real number is what gets reported.

### Requirements for every new renderer

Each new renderer must:

- begin from one stable modularized `develop` baseline;
- be implemented against the modular renderer system from inception, not retrofitted;
- remain renderer-local where possible;
- have truthful `GraphicsCapability` reporting — no capability claimed that is not implemented;
- have deterministic unsupported-path rejection;
- provide permanent tests;
- avoid silent fallback to another CNA renderer;
- distinguish public CNA identity from internal native/RHI API choices, exactly as the existing
  LLGL/Diligent/Sokol/bgfx/Skia identities already do.

---

## Phase 3 — complete XNA sample campaign (FUTURE)

Blocked on Phases 1–2 being sufficiently stable.

Perform a complete review and iteration of the XNA Samples corpus in `cna-samples`.

**ALL XNA samples must be revisited, including samples that were ported previously.**

The purpose is **not** "make the sample appear to work". The purpose is to use the original XNA
samples as framework-level compatibility and correctness probes for CNA.

### Strict rule: no sample-side workarounds for CNA defects

If a valid original XNA sample exposes that CNA:

- implements XNA behavior incorrectly;
- implements an API incompletely;
- lacks a required valid XNA API;
- has a graphics/content/input/audio/runtime regression;
- differs from valid XNA 4.0 behavior;

then **fix the owning behavior in CNA itself**. Do not add sample-specific conditionals to hide
CNA defects.

### Required workflow

    sample exposes problem
        -> minimize / reproduce
        -> establish expected XNA behavior from authoritative reference/evidence
        -> assign CNA finding/task
        -> fix CNA
        -> add permanent CNA regression test
        -> retake sample without workaround

A dedicated CNA remediation session may run in parallel with the `cna-samples` campaign,
consuming framework defects discovered by sample work.

Preserve a clear boundary between **valid sample adaptation required by C++/CNA porting** and
**invalid workaround hiding a CNA compatibility defect**. The first is expected and legitimate;
the second is prohibited.

### XNA Racing Game is deliberately LAST

The XNA Racing Game sample is reserved as the **last** XNA sample in the campaign. Do not move it
earlier merely because it is visually attractive or complex.

Intended sequence:

    simpler/normal XNA samples
        -> framework defects discovered and fixed
        -> broad sample corpus stable
        -> Racing Game LAST

Racing Game acts as a late integration/showcase test after the rest of the sample corpus has
already exercised and improved CNA.

---

## Phase 4 — historical plan/audit review (FUTURE)

Blocked on Phase 3.

Systematically review historical CNA planning and audit material predating the major
audit/integration campaign, including `plan_*.md`, old audit documents, old TODO/remediation
documents, technical planning files, historical issue inventories, and pre-major-audit notes.

**Do not blindly execute old tasks.** For every historical item:

1. understand the original claim/task;
2. inspect current CNA;
3. reproduce it where applicable;
4. determine whether later work already solved or superseded it;
5. classify it.

Classifications:

    DONE            SUPERSEDED      DUPLICATE       OBSOLETE        STILL_VALID
    BUG             MISSING_FEATURE FUTURE          NEEDS_RESEARCH

Only still-valid problems become new active work. Preserve useful historical evidence rather than
rewriting history merely to make old plans look current.

---

## Phase 5 — glTF correctness campaign (FUTURE)

Blocked on Phase 4.

**Motivation:** `cna-gltf-viewer` currently displays many glTF assets incorrectly or broken.

Treat `cna-gltf-viewer` as an integration oracle and reproduction surface — **not** as the place
to fix things. Do not fix framework defects with viewer-specific workarounds.

For every broken asset/path:

    reproduce
        -> minimize where practical
        -> identify the owning layer
        -> fix the owning CNA/glTF implementation
        -> add permanent regression asset/test
        -> retake cna-gltf-viewer

### Audit scope

Audit the complete relevant glTF path, including where applicable:

- GLB/glTF parsing; accessors; sparse accessors; buffers and buffer views; component types;
  normalized data;
- primitive topology; indices; vertex attributes; coordinate conventions;
- scene hierarchy; node transforms; local/world transforms; matrix/TRS interaction; cameras;
- meshes; materials; PBR parameters;
- textures; image loading; samplers; UV sets; color spaces; alpha modes; double-sided state;
- normals; tangents;
- skinning; inverse bind matrices; joints; animation; morph targets if supported/claimed;
- lighting/effects where CNA owns the mapping; render state; renderer interaction;
- content lifetime/caching.

**Do not assume every `cna-gltf-viewer` visual failure is in the parser.** Classify the actual
owning layer.

Final goal:

    glTF correctness fixed in CNA
    + permanent regression coverage
    + cna-gltf-viewer rendering the supported corpus correctly

rather than a viewer full of special cases.

---

## Governance

- This document is a roadmap and vision record. It is **not** authorization to start any phase.
- Current tasks always take precedence over anything written here.
- Do not begin a later phase merely because it is documented here.
- Each major future phase requires a fresh explicit owner instruction and its own acceptance
  criteria.
- Roadmap details may evolve when evidence changes; update this document rather than letting it
  drift into a false present-tense claim.
- When a phase completes, replace its target numbers with mechanically recounted, truthful values.
