# cna-engine upstream repair intake

Owner-requested intake, 2026-10-22. Read-only source inventory:
`../cna-engine/upstream-issues.md`. Do not edit cna-engine or cna-extended.
Use `cna-engine UP-NNN` in every associated commit message. No push authorized.

Baseline: CNA `a8800619e1b063e4e7f8bed5cf1b19744362c189`, branch `next`;
reported engine pin `900d0db3b` is an ancestor. Preserve the inherited dirty
content/audio ownership and ISSUE-113 changes. The uncommitted storage-path
regressions from the preceding batch are also retained, not part of this intake.
Their 29-check genuine XNA oracle passed; native red baseline is recorded in
cna-demos/build/linux-full/reports/repair-storage-path-before-v1-*.

| Finding | Current disposition | Evidence / next task |
| --- | --- | --- |
| UP-019 | Investigating | Software shadow projection uses positive Y-to-V although its raster target is top-down; reproduce producer/receiver orientation before changing it. |
| UP-018 | Investigating | Current Software framebuffer retains declared-format floats and FetchColorTexel reads them. Verify public creation, drawing, typed readback and shadow precision; do not assume the historical symptom persists. |
| UP-006 | Candidate already fixed | `470b12a5d` repairs mixed umbrella/static-sidecar ordering (ISSUE-016). Rebuild and run real mixed-runtime consumers and symbol checks, including engine-style module combinations. |
| UP-001 | Confirmed source gap | No CNA install export/config/version package exists. Design relocatable dependency/target export without breaking source consumers or duplicating the runtime. |
| UP-002 | Documentation corrected | `docs/migration-guide.md`, CNAEXT and C++ language artefacts, distinguishes declaration markers from implicit/synthesized C# semantics and explicitly retains strict compiler diagnostics. No runtime/header/test changes needed. |

Only one heavy job/compiler worker at a time, existing build directories, shared
ccache and private GPU runner. Use established MemoryHigh=4G / MemoryMax=6G /
MemorySwapMax=0 scope. Runtime validation not yet performed for this intake.
Exact next task: run focused float-target and mixed-runtime baseline, then add
the asymmetric shadow producer/receiver regression for UP-019.

UP-002 validation: checked the three input declarations, Vector3 compound
operators and their existing C# synthesis comment, CNAHelper.hpp's macro, and
cmake/Harnesses.cmake's deprecation-as-error gate. This is documentation only;
no strict marker, assertion, budget or runtime contract changed.
