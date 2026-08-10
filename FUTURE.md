# CNA future development roadmap

Date: 2026-08-09

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
| **CURRENT** | Public CNA renderer identities | **41** — mechanically counted from `include/CNA/GraphicsBackendType.hpp` |
| **NEXT** | Phase 1 — CNA modularization | **complete on `feature/modularization`** (target graph 2026-08-09 + physical layout/hardening 2026-08-10, no-loss-proven); pending owner review and develop promotion; the sharp-runtime audit-remediation develop merge is a separate still-open external gate |
| **FUTURE** | Phase 2 — renderer expansion (OPENGLES2 + 13 new renderers) | **not started**; blocked on the Phase-1 develop promotion |
| **FUTURE** | Phase 3 — complete XNA sample campaign | **not started**; blocked on Phases 1–2 |
| **FUTURE** | Phase 4 — historical plan/audit review | **not started**; blocked on Phase 3 |
| **FUTURE** | Phase 5 — glTF correctness campaign | **not started**; blocked on Phase 4 |

Explicitly **not** true today, and not to be stated as true anywhere:

- CNA does **not** have 55 renderers. It has 41.
- Modularization is complete **on `feature/modularization` only** (target graph + physical
  layout, no-loss-proven; see `MODULARIZATION_PLAN.md`); `develop` itself is **not** yet
  modularized — the promotion merge has not happened, and the sharp-runtime
  audit-remediation develop merge is a separate still-open gate.
- The XNA samples do **not** all pass. The corpus has not been revisited.
- glTF is **not** corrected. `cna-gltf-viewer` still displays many assets incorrectly.

Phases are sequential. Each depends on its predecessor being completed and stabilized.

---

## Phase 1 — CNA modularization (NEXT)

The immediate next major CNA development campaign after the develop promotion.

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

**Dependency:** Phase 2 must not begin before this exit condition holds. Starting renderer
expansion against the pre-modular structure would create 14 new renderers that then have to be
migrated.

---

## Phase 2 — renderer expansion (FUTURE)

Blocked on Phase 1.

Current public renderer count before this phase: **41**.

This phase adds one new public OpenGL ES 2 path plus 13 planned new renderer implementations.

### Planned additions

| # | Public identity | Notes |
|---:|---|---|
| 1 | `OPENGLES2` | Public CNA OpenGL ES 2 renderer/profile. Expected to reuse the existing EasyGL/MetaGL OpenGL ES 2 capability where technically appropriate, but must remain a genuine public CNA identity with truthful capability and platform reporting. |
| 2 | `FNA3D` | Based on the FNA3D graphics library. |
| 3 | `OPENVG` | OpenVG. |
| 4 | `SVG_DOM` | SVG DOM. |
| 5 | `IGL` | Facebook IGL — https://github.com/facebook/igl |
| 6 | `NVRHI` | NVIDIA NVRHI. |
| 7 | `KORE` | Kode/Kore — https://github.com/Kode/Kore |
| 8 | `BLEND2D` | Blend2D. |
| 9 | `METHANEKIT` | MethaneKit RHI. |
| 10 | `LINAGX` | LinaGX. |
| 11 | `TEMPEST` | Tempest. |
| 12 | `THORVG` | ThorVG. |
| 13 | `PORTABLEGL` | PortableGL. |
| 14 | `REACT_DOM` | A distinct React/DOM-oriented CNA rendering implementation, **only if** the final architecture proves it can truthfully satisfy a useful CNA graphics contract. It must not be counted merely as a conceptual alias of an existing identity. |

That is `OPENGLES2` + 13 new renderer implementations = **14 additions**.

### Target count

    41 + 14 = 55 public CNA renderer identities

**This 55 count is a TARGET, not an invariant.** After implementation, recount public identities
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
