# CNA future development roadmap

Date: 2026-08-09 (updated 2026-08-10 — all pre-expansion preparation is complete and public:
the final physical module/package layout, the renderer terminology normalization and the
module-owned examples are all promoted to `develop`; reconciled 2026-08-21 — Phase 2's renderer
table and Phase 5's body text had fallen behind the status table above, which was already being
kept current; both are now brought in line with it)

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
| **CURRENT** | Public CNA renderer identities | **50** — mechanically counted from `CNA/GraphicsRendererType.hpp` (modules/core) and `cmake/RendererSelection.cmake`, pinned by `scripts/check_renderer_identities.py`. 41 at the 2026-08-10 pre-expansion promotion, **−1** for the `ASCII` renderer identity (migrated to a renderer-neutral post-process effect), **+6** for `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG` and `PORTABLEGL`, all integrated on the `11branches` integration branch, **+1** for `TINYGL` (2026-08-13, the first identity delivered from `docs/renderer-expansion-candidates.md`), **+1** for `IGL` (2026-08-15), **+1** for `PIXIJS`, **+1** for `NANOVG` (2026-08-19, the second identity delivered from `docs/renderer-expansion-candidates.md`, §A6) |
| **CURRENT** | Phase 1.5 — naming normalization (backend→renderer, DIRECTX*, OPENGLES3, CNAEXT) | **COMPLETE AND PUBLIC** — implemented on `feature/renderer-naming-normalization` (endpoint `16f76cf1a`) and promoted to `develop` on 2026-08-10 as part of the pre-expansion fast-forward. See `docs/RendererNamingMigration.md`. Renderer count unchanged at 41 |
| **CURRENT** | Phase 1.6 — module-owned examples | **COMPLETE AND PUBLIC** — implemented on `feature/module-examples` (endpoint `675e04c7a`, a descendant of the naming endpoint) and promoted in the same fast-forward. All 1373 tracked example files now live with their owning module, registered by 44 module-local `examples/CMakeLists.txt` files; only the shared `examples/golden/` oracle corpus stays at repository level. See `docs/physical-modules.md` §"Module examples" and `modularization/module-examples/` |
| **CURRENT** | Phase 1 — CNA modularization | **COMPLETE AND PROMOTED** in three stages, all now on public `develop`: target graph + physical `src/` layout (`41028e995`), modular sharp-runtime consumption (`ea61123e6`), and the owner-requested **final physical module/package layout** (`modules/<name>/{include,src,tests}` monorepo, plans/MODULARIZATION_PLAN.md §11–§11.2) promoted 2026-08-10 by fast-forward to `3ecbbce72` (tree unchanged by the promotion). The modularization campaign is DONE |
| **CURRENT** | Phase 2 — renderer expansion (OPENGLES2 + 13 new renderers) | **in progress**: seven additions — `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG`, `PORTABLEGL`, `IGL` — are implemented on their own lanes and integrated. The remaining planned additions are untouched and each still requires its own explicit owner instruction |
| **FUTURE** | Phase 3 — complete XNA sample campaign | **not started**; blocked on Phases 1–2 |
| **FUTURE** | Phase 4 — historical plan/audit review | **not started**; blocked on Phase 3 |
| **CURRENT** | Phase 5 — glTF correctness campaign | **in progress on `feature/gltf`, ahead of its stated Phase 4 dependency** (started 2026-08-11 from `gltfissues.md`'s analysis and a forensic audit that reproduced eight defects, D1–D8, every one of which produced a *model that rendered*). The working record is `plans/plan_gltf.md`: 475 rows, **470 closed**, and all eight audit defects `fixed` in the corpus defect ledger. The evidence base is a generated 148-asset corpus, the exact L0–L6 numerical ladder run per commit under ASan+UBSan, a required production-viewer OPENGLES3 L7 gate with 137 deterministic PNGs plus 8 deterministic safe rejections, the 13-case pinned Khronos comparison and the completed 15-case Gate C viewer retake (`docs/gltf-conformance.md`). **`GLTF CORE 2.0 CORRECT` was declared on 2026-08-15** after all 20 §27.1 rows and the fresh four-renderer Gate B were green — and that declaration was later found premature (`plans/plan_gltf.md` §27.1.2: four core divergences sat inside rows that were green). The milestone in force is now the **qualified** `GLTF CORE 2.0 IMPORT/RUNTIME MODEL CORRECT` (§27.1.3), which states renderer coverage beside it rather than inside it: 15 of 17 PBR renderers apply `COLOR_0` and the other 2 refuse such a draw by name. Phase 5 remains current, rather than complete: `GLTF-459` (**GLTF ROBUST**), optional-extension/renderer-specific L7 residue and the retrospective are still open |

Explicitly **not** true today, and not to be stated as true anywhere:

- CNA does **not** have 55 renderers. It has **50** (mechanically counted,
  `scripts/check_renderer_identities.py`): 41 at the pre-expansion promotion, minus the removed
  `ASCII` renderer identity, plus `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG`,
  `PORTABLEGL`, `TINYGL`, `IGL`, `PIXIJS` and `NANOVG`.
- Modularization is complete **and promoted**, including the final physical module/package
  layout: `develop` is a module-oriented monorepo as of 2026-08-10 (`41028e995` target graph +
  physical layout, then `3ecbbce72` `modules/<name>/{include,src,tests}`; both no-loss-proven —
  see `plans/MODULARIZATION_PLAN.md` §10 and §11–§11.2). The sharp-runtime side is closed too: the
  remediation snapshot `7888a29f` was merged
  into sharp-runtime `develop` (`81624983`, published) and CNA consumes the modular
  `SharpRuntime::<Component>` targets; the audit-remediation campaign itself continues on its
  feature branch and later increments merge separately.
- The XNA samples do **not** all pass. The corpus has not been revisited.
- glTF **ROBUST** is not declared and the broader campaign is not finished. Neither is the unqualified
  **`GLTF CORE 2.0 CORRECT`**: it was declared on 2026-08-15 from all 20 §27.1 requirements and Gate B,
  found premature, and the milestone in force is the qualified
  **`GLTF CORE 2.0 IMPORT/RUNTIME MODEL CORRECT`** (`plans/plan_gltf.md` §27.1.3) — the importer, the `.cnj`
  path, the vertex ABI and the effect/draw-parameter runtime model, with renderer coverage stated as
  coverage. Four PBR renderers refuse `COLOR_0` by name, `LLGL` cannot draw a material with no
  base-colour texture, and `OPENGLES1` renders a PBR draw through its fixed-function colour path
  (`GLTF-473`). The separate `cna-gltf-viewer` completed the pinned Gate C
  retake, dual UV transport/selection is pixel- and Khronos-proven, and every remaining limitation
  is enumerated in `docs/gltf-limitations.md` and reported rather than silent. Optional material
  extensions and renderer-specific whole-corpus L7 policies remain §27.2/ROBUST work; the CORE
  declaration must not be expanded to cover them.

Phases are sequential. Each depends on its predecessor being completed and stabilized.

---

## Phase 1 — CNA modularization (COMPLETE AND PROMOTED)

Split the monolithic build/project into coherent modules with explicit dependency boundaries,
isolated renderer implementations, preserved public behavior and existing test/sanitizer coverage,
and established minimal-link/build tests that enforce each module's real dependency set rather than
an assumed one.

**Exit condition met (2026-08-10).** The stable modularized `develop` base for renderer expansion
is the current public `develop` head; the campaign record, the promotion evidence and the bounded
post-promotion gate are in `plans/MODULARIZATION_PLAN.md` §9–§10 and `NEXT.md`. Phase 2 was built on
this base rather than the pre-modular structure, which would have created renderers that then
needed migrating.

---

## Phase 2 — renderer expansion (IN PROGRESS)

Unblocked by Phase 1's promotion and **in progress**. Every remaining item still requires its own
fresh explicit owner instruction before work begins, and must start from the stable modularized
public `develop` base, not from an older pre-modularization commit.

Current public renderer count before this phase: **41** (the 2026-08-10 pre-expansion promotion).
Six of the items in the table below have since been implemented and integrated — `OPENGLES2`,
`FNA3D`, `OPENVG`, `SVG_DOM`, `IGL` and `PORTABLEGL` (rows 1, 2, 3, 4, 5 and 12) — plus `BLEND2D`,
delivered on its own lane and no longer listed as a planned addition here. Three more identities,
`TINYGL`, `PIXIJS` and `NANOVG`, landed from the separate `docs/renderer-expansion-candidates.md`
roadmap and are not rows in this table. Together with the removal of the `ASCII` renderer identity
(migrated to a renderer-neutral post-process effect, outside this phase's scope), the live count is
**50** (mechanically counted, `scripts/check_renderer_identities.py`; see the status table at the
top of this document). The remaining items in this table are still unstarted and each still
requires its own fresh explicit owner instruction.

This phase adds one new public OpenGL ES 2 path plus 12 planned new renderer implementations.

### Planned additions

| # | Public identity | Notes |
|---:|---|---|
| 1 | `OPENGLES2` | **INTEGRATED** (`feature/opengles2`) — public CNA OpenGL ES 2 renderer/profile, reusing the EasyGL ES 2 capability as its own fifth GL profile (`CNA_GL_PROFILE_OPENGLES2`) with truthful ES 2.0 capability and platform reporting. See `plans/plan_opengles2.md` / `docs/opengles2-renderer.md`. |
| 2 | `FNA3D` | **INTEGRATED** (`feature/fna3d`) — FNA3D pinned at release 26.08, executing XNA's own compiled stock effects through MojoShader; selects SDL_GPU/Direct3D 11/OpenGL at runtime. See `docs/fna3d-renderer.md` and `plans/plan_fna3d.md`. |
| 3 | `OPENVG` | **INTEGRATED** (`feature/openvg`) — OpenVG 1.1 via ShivaVG on a real desktop OpenGL context. 2D-only (no 3D pipeline, no render targets). See `docs/openvg-renderer.md`. |
| 4 | `SVG_DOM` | **INTEGRATED** (`feature/svgdom`) — Emscripten-only, 2D-only; renders `SpriteBatch` output as real pooled SVG DOM elements (`<svg>`/`<image>`/`feColorMatrix`), distinct from both `CANVAS` (rasterized) and `HTML_DOM` (CSS `<div>`s). See `docs/svg-dom-renderer.md`; real-browser validation remains an external Emscripten-SDK gate. |
| 5 | `IGL` | **INTEGRATED** (2026-08-15) — Facebook IGL, driving its own OpenGL (GLX) or Vulkan backend, fixed for the process by `CNA_IGL_BACKEND`. See `plans/plan_igl.md` / `docs/igl-renderer.md`. |
| 6 | `NVRHI` | NVIDIA NVRHI. |
| 7 | `KORE` | Kode/Kore — https://github.com/Kode/Kore |
| 8 | `METHANEKIT` | MethaneKit RHI. |
| 9 | `LINAGX` | LinaGX. |
| 10 | `TEMPEST` | Tempest. |
| 11 | `THORVG` | ThorVG. |
| 12 | `PORTABLEGL` | **INTEGRATED** (`feature/portablegl`) — CPU software OpenGL 3.x-ish pipeline via `rswinkle/PortableGL`; no GPU or window required. See `docs/portablegl-renderer.md`. |
| 13 | `REACT_DOM` | A distinct React/DOM-oriented CNA rendering implementation, **only if** the final architecture proves it can truthfully satisfy a useful CNA graphics contract. It must not be counted merely as a conceptual alias of an existing identity. |

That is `OPENGLES2` + 12 new renderer implementations = **13 additions** in this table, of which
seven are already integrated (the six marked INTEGRATED above, plus `BLEND2D`, which was delivered
on its own lane and is no longer listed as a planned addition).

### Target count

    50 live today (mechanically counted, `scripts/check_renderer_identities.py`) + 7 still-unstarted
    additions from the table above = 57 public CNA renderer identities if every remaining planned
    identity in this table lands

`TINYGL`, `PIXIJS` and `NANOVG` are already included in the 50 but are not rows in this table —
they were delivered from the separate `docs/renderer-expansion-candidates.md` roadmap, outside
Phase 2's original 13-item list.

(The original 55 target assumed the `ASCII` renderer identity would remain; it was removed in
favour of a renderer-neutral post-process effect, so the arithmetic ceiling from this table alone
is one lower.)

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

## Phase 5 — glTF correctness campaign (IN PROGRESS, ahead of schedule)

Started 2026-08-11 on `feature/gltf`, by explicit owner decision, ahead of its stated Phase 4
dependency — the one documented exception to "Phases are sequential" above. Current status,
evidence and the remaining open work are tracked in `plans/plan_gltf.md` (478 tasks) and
`docs/gltf-limitations.md`; do not reconstruct this campaign's state from this document — see the
status table at the top instead.

**Motivation:** `cna-gltf-viewer` displayed many glTF assets incorrectly or broken.

The standing rules that shaped the campaign and still apply to its remaining work:

- Treat `cna-gltf-viewer` as an integration oracle and reproduction surface — **not** as the place
  to fix things. Do not fix framework defects with viewer-specific workarounds.
- **Do not assume every `cna-gltf-viewer` visual failure is in the parser.** Classify the actual
  owning layer.
- Workflow per broken asset/path:

      reproduce
          -> minimize where practical
          -> identify the owning layer
          -> fix the owning CNA/glTF implementation
          -> add permanent regression asset/test
          -> retake cna-gltf-viewer

Final goal: glTF correctness fixed in CNA, with permanent regression coverage and
`cna-gltf-viewer` rendering the supported corpus correctly — not a viewer full of special cases.

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
