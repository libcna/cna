# No-loss reconciliation — target-graph modularization

Baseline: `modularization/baseline/` at pristine `5f2c4e941` (tree `40494b21b`).
After: `modularization/after-target-split/` at the modularized feature branch.
Method: identical `capture_inventory.py` runs, diffed; classification per the campaign's
no-loss/completeness gate.

## Production sources and headers (`include/`, `src/`)

`files-production.tsv` before/after: **byte-identical — 1357 files, every one PRESERVED.**
No file was added, removed, moved, or edited. Comments and documentation inside sources are
untouched by construction.

## Public API

`api-decls.tsv` (mechanical class/struct/enum inventory over `include/`): **identical — 1300
declarations, all PRESERVED.** No class, struct, enum, method, constant, overload, or public
header changed.

## Test sources (`tests/`)

478 baseline files all PRESERVED (hashes identical). **5 additions** (no removals):
`tests/modules/probe_{math,core,graphics,content,runtime}.cpp` — the minimal-link probe
consumers (MODULARIZATION_PLAN.md §4).

## Registered tests (compared by NAME)

- OPENGLES: baseline 6526 registrations; after = baseline + exactly 11 additions
  (`ModuleProbe_*` ×5, `ModuleLinkClosure_*` ×5, `RendererIdentityRegistry`); zero removals,
  zero renames.
- HEADLESS: pristine control 6118 registrations; modular = control + exactly 12 additions (the
  11 above plus the HEADLESS-only `ModuleLinkClosure_GraphicsNativeSdkFree`); zero removals.
- The gtest test list of `CnaTests` is byte-identical to baseline (normalized for gtest's
  pointer-valued `GetParam()` annotations).

## Production TU ownership

Enforced permanently at configure time by the source-partition validator in
`cmake/CnaLibrary.cmake`: tracked applicable production TUs minus module-owned TUs = empty
(unowned or doubly-owned TUs are a FATAL_ERROR), with the backend/GamerServices/Net trees and
the FFmpeg-gated exclusions accounted exactly as in the monolith.

## Build tooling (`CMakeLists.txt`, `cmake/`, `scripts/`, `tools/`, `examples/`)

All differences are this campaign's signed commits — INTENTIONALLY_REPLACED (modified):
`CMakeLists.txt`, `cmake/CnaLibrary.cmake`, `cmake/BackendLibraries.cmake`,
`cmake/Examples.cmake`, `cmake/UnitTests.cmake`, and the 21 per-backend
`cmake/Tests/*Tests.cmake` files (the `--start-group` collapse; `VulkanTests.cmake`
additionally gained the pre-existing-gap SDL3 link fix). Additions:
`cmake/SharpRuntimeConsumption.cmake`, `cmake/Tests/ModuleProbes.cmake`,
`scripts/check_module_link_closure.py`, `scripts/check_renderer_identities.py`.
`examples/` and `tools/` are hash-identical.

## Intentionally removed items

None. No file, test, target surface, sample, tool, generated artifact, or API element was
removed. The only pre-existing target-name change is `CNA` becoming an INTERFACE umbrella with
an unchanged consumer contract (same name, same include dir, same public defines, same link
closure).

## Generated artifacts

The committed generated shader headers (`*_shaders.hpp` under backend `shaders/` directories)
and their generation scripts are hash-identical (contained in the production/build-tooling
identity above); no generation workflow was touched.
