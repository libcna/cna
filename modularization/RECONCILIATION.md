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
consumers (plans/MODULARIZATION_PLAN.md §4).

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

---

# No-loss reconciliation — Phase 2 physical layout

Baseline: `modularization/after-target-split/` at Phase-1 final `b072f0da6` (tree `ef3cc2a91`).
After: `modularization/after-phase2-layout/` at the Phase-2 branch head.
Method: identical `capture_inventory.py` runs plus a full old-path→new-path mapping
reconciliation (`move-map.tsv`, 674 rows, generated from `git diff -M`); every Phase-1
production file classified.

## Production sources and headers (`include/`, `src/`)

1357 baseline files → 1357 files, zero additions, zero deletions:

- **683 PRESERVED** — same path, same hash: the entire public `include/` tree (public
  include-path compatibility was a hard constraint; internal-contract headers under
  `include/CNA/Internal/**` deliberately did not move).
- **662 MOVED, byte-identical** — R100 renames into the module-first `src/` layout.
- **12 MOVED_WITH_REQUIRED_EDIT** — the backend files whose src-rooted generated-shader
  include directives had to become includer-relative when the backend directories moved
  (15 directives; Sokol ×2 files, D3D9 ×7, D3DCommon, SdlGpu, Vulkan, plus
  `D3D9CnaShaderRegisters.hpp`). Per-file old-blob/new-blob diffs contain **zero**
  non-`#include` changed lines — comments and code byte-preserved.

The five move commits were 100% renames (674/674 R100); the directive edits landed in the
path-update commit, so pure moves and content changes are separated in history.

## Public API

`api-decls.tsv`: **byte-identical** to the Phase-1 snapshot (1300 declarations). `include/`
is hash-identical, so the public API surface is unchanged by construction.

## Test sources and registered tests

`files-tests.tsv`: byte-identical (483 files — the 478 baseline + Phase-1's 5 probes).
OPENGLES ctest registration: 6537 names = pristine baseline 6526 + exactly the 11 Phase-1
module gates; zero removals, zero renames (`ctest-names-opengles.txt`). HEADLESS ctest:
6130 = pristine control 6118 + the 12 Phase-1 module gates; full run at `-j4` leaves,
after the serial rerun of the known flake families (ENet, audio timing), exactly the
control's 2 accepted deterministic residuals (REMED-GFX-133
`SetRenderTargets_FourTargets`, `Headless_Smoke`).

## Production TU ownership

The source-partition validator now works over the module-first directories (ownership is
purely directory-based — physical location IS the ownership statement) and still fails the
configure on unowned or doubly-owned production TUs. Real build-target names (HEADLESS
configure): 107, identical to Phase 1.

## Build tooling

19 files INTENTIONALLY_REPLACED (modified in place): `cmake/CnaLibrary.cmake` (module
globs + validator paths + the audio→input edge + include-hygiene), `BackendSelection.cmake`
(BACKEND_DIR values), `BackendLibraries.cmake` (shared-core paths + include-hygiene),
`Tests/HtmlDomTests.cmake`, `Tests/D3D9Tests.cmake` (scoped include for the shadercache
test), `ThirdPartySokol.cmake`, `tools/media/arithmetic32bit/CMakeLists.txt`, 12 scripts,
and `examples/d3d9_shadercache_test.cpp` (one include directive re-rooted). Zero
`src/CNA|src/Microsoft` references remain in cmake/scripts/tools.

## Intentionally removed items

None. No file, test, target, sample, tool, generated artifact, API element, or comment was
removed in Phase 2.

---

# No-loss reconciliation — Phase 3 physical module layout

Baseline: `modularization/physical-modules/baseline/` at public develop `ea61123e6`
(fresh capture, 1357 production / 1300 API declarations / 483 test files / headless+opengles
registration and gtest name sets). After: `modularization/physical-modules/after/` at the
`feature/physical-modules` head. Method: identical `capture_inventory.py` runs (modules-aware),
classified through the deterministic move map by `reconcile_phase3.py`.

## Production sources and headers

1357 baseline files → 1357 files, zero additions, zero deletions, zero missing:

- **1287 MOVED, byte-identical** — R100 renames into `modules/<name>/{include,src}` and
  `modules/renderers/<family>/{include,src}`.
- **70 MOVED_WITH_REQUIRED_EDIT** — every changed line is an `#include`/path directive, zero
  other lines (verified per file against the baseline blobs): 66 renderer headers whose
  `"../Common/IGraphicsBackend.hpp"` relative spelling became the canonical
  `CNA/Internal/Backends/Common/...` (the contract now lives in the graphics module;
  AsciiGraphicsBackend.hpp additionally canonicalizes its SdlRenderer reference), plus the
  4-file D3D9 shader-register repair (a Phase-2 latent defect: the generated table's
  public-internal include spelling had pointed at a path with no physical file since the
  `src/` include root was removed; the table now lives in the d3d9 module include tree and
  the generator's default output follows it).
- **0 PRESERVED-in-place** — the global `include/` + `src/` trees are gone entirely; this is
  the point of the campaign.

## Public API

`api-decls.tsv`: 1300 → 1301 rows; **zero declarations removed**. The one addition is
`struct D3D9ShaderConstantSlot` — the relocated generated register table now sits under an
include tree and is therefore counted by the mechanical inventory; the declaration existed at
base under `src/.../shaders/`.

## Test sources

483 baseline files → 492: 64 PRESERVED in place (`tests/assets/**`, `tests/modules/**`,
where runtime fixture literals and the probe infrastructure live), 418 MOVED byte-identical
into `modules/<name>/tests/**` (each keeping its former `tests/`-relative mirror path), 1
MOVED_WITH_REQUIRED_EDIT (the content-module Xnb reader test's 2-line directive reaching the
audio module's shared test-access helper), **9 additions** (the new minimal-link probes),
zero missing.

## Registered tests (compared by NAME)

HEADLESS: baseline 6120 names → 6143; **zero removed, zero renamed, exactly 23 additions** —
the new `ModuleProbe_*`/`ModuleLinkClosure_*` fleet (input, audio, media, storage, devices,
devices-ext, graphics-ext, noxna, net; NoXna composition; Net/ENet; the four HEADLESS
native-SDK-free gates).

## Build tooling

All differences are this campaign's signed commits: the dissolved central manifests
(`cmake/CnaLibrary.cmake`, `cmake/BackendLibraries.cmake` → 55 per-module
`CMakeLists.txt` + `modules/CMakeLists.txt` + `modules/renderers/CMakeLists.txt`),
`CMakeLists.txt`, `cmake/BackendSelection.cmake` (BACKEND_DIR → module dirs),
`cmake/UnitTests.cmake` (module test globs; every path-tail filter unchanged),
`cmake/Tests/{ModuleProbes,MetalTests,WickedTests,HtmlDomTests,GlideTests,D3D9Tests}.cmake`,
the renderer discipline/verifier scripts, the workflow path filters, the standalone 32-bit
arithmetic checker, and the identity-registry path. `examples/` is hash-identical except the
one d3d9 shader-cache directive line.
