# CNA maintainer study guide

This guide is for a maintainer who wants to understand CNA deeply enough to review, debug and
change it without treating existing code as authoritative merely because it compiles. It defines
the evidence hierarchy, study workflow and dependency order. It is not a feature roadmap, a live
bug list, a release calendar or a commitment to spend a fixed number of years on the current tree.

CNA changes too quickly for a useful 520-week file-by-file calendar. Plan one thirteen-week cycle
in detail, keep later work as dependency-ordered phases, and regenerate the inventory before every
cycle.

## Sources of truth

Use these sources in this order:

1. Measured Microsoft XNA 4.0 behavior, including the committed oracle evidence and
   [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md).
2. FNA as the normal readable API and implementation reference when no measured XNA disagreement
   exists. Its checkout location is intentionally not part of this repository's contract.
3. [`CHECKLIST.md`](../CHECKLIST.md) for accepted C++ mappings and the completion requirements of
   an individual ported type.
4. Tests, generated coverage reports and maintained subsystem documentation.
5. Current implementation code. Old CNA code, generated stubs and historical plans are evidence of
   what existed, not proof that behavior is correct.

The current project state and its one live defect/limitation list are in
[`handoff.md`](../handoff.md), especially §5. [`task-id-index.md`](task-id-index.md) maps historical
task IDs to their surviving code or documentation. Completed plans live in Git history and must not
be used as current backlogs.

## Inventory before study

Record the commit being studied and regenerate facts rather than copying counts into notes:

```bash
git rev-parse HEAD
./loc.sh --no-submodules
python3 tools/audit_xna_runtime_surface.py
python3 scripts/check_renderer_identities.py
```

Use [`physical-modules.md`](physical-modules.md) for module ownership and
[`renderer-registry.md`](renderer-registry.md) for the current public renderer set. Renderer
directories are implementation families, not public identities. Retired implementations belong to
[`removed-renderers.md`](removed-renderers.md) and Git history; they are not part of the current
line-by-line study scope.

The line-count report distinguishes hand-written code, generated routes and embedded shader data.
Do not mix physical file lines, code lines and generated output in schedule arithmetic. A generated
file is studied through its specification, generator and round-trip checks, not by reading every
emitted line.

## Study cycle

Each topic follows the same evidence loop:

1. **Orient.** Identify the owning module, public surface, target dependencies and maintained
   documentation.
2. **Read.** Compare declarations and implementation with FNA and any recorded XNA decision.
3. **Run.** Build the affected target and execute a small representative program under a debugger
   or with focused diagnostics.
4. **Verify.** Locate the regression test, coverage entry or oracle. Write the expected result from
   primary evidence before changing semantics.
5. **Change only when warranted.** A study week may correctly produce no source change. Do not add a
   comment, refactor or test solely to satisfy a commit quota.
6. **Record.** Keep personal reading notes and unresolved questions outside the product repository.
   Move only durable contracts, reproduced defects and reusable evidence into CNA.

When a change is warranted, finish the whole bounded task: implementation, public documentation,
tests, maintained subsystem document and `handoff.md` when its current-state record changes. Follow
[`AGENTS.md`](../AGENTS.md) for build, test, staging and commit rules.

## Questions for each file or coherent group

Answer these in study notes:

1. What public or internal contract does this code own, and who calls it?
2. What do XNA measurements and FNA require?
3. Is each difference an accepted C++ mapping, a CNA extension, an intentional measured decision or
   an unverified divergence?
4. Which test would fail if the behavior changed? Does it assert semantics or merely reach code?
5. Is the file hand-written, generated, embedded data or third-party integration? Where is its real
   source of truth?
6. Which modules, platforms and current renderer families depend on it?
7. Can the same contract be expressed more simply without changing public behavior, lifetime,
   binary layout or error semantics?

Deletion experiments and broad refactors belong in a disposable worktree or purpose-built spike,
never in the maintainer's main working tree. Passing compilation alone does not establish that code
was dead.

## Code classes and depth

### Hand-written contracts and behavior

Read line by line when the code implements XNA-visible behavior, resource lifetime, platform
abstraction, renderer contracts, serialization, threading or error semantics. Keep the relevant
FNA file and recorded XNA decisions open.

### Generated surfaces

For the C API and other generated output, study:

- the public ABI and ownership/error contracts under [`c-api/`](c-api/README.md);
- the TOML specifications and generator under `modules/c-api/spec/` and `tools/c-api/`;
- representative hand-written adapters;
- generated coverage, pure-C compilation, ABI and browser/native round-trip tests.

Generated routes still require machine verification, but repeating the same marshalling pattern by
eye is not a useful measure of understanding.

### Dependencies, retired code and embedded data

Study a third-party dependency at the boundary CNA relies on, including pins and local patches; do
not claim ownership of the dependency's whole source tree. Read a retired renderer only when Git
history is needed to recover a measured conclusion. Regenerate embedded shader or asset data from
its source and validate the package rather than reviewing numeric arrays line by line.

## Dependency-ordered phases

The phases are ordered; their duration is deliberately not fixed.

1. **Build and repository model.** Presets, module targets, dependency closure, `loc.sh`, test
   registration, private GPU runner, coverage/oracle tools and generated files.
2. **Foundation and game lifecycle.** Sharp Runtime's CNA-used surface, `core`, `math`, `platform`,
   `runtime`, `storage`, events, disposal, timing and window/device lifecycle.
3. **Public XNA runtime and Graphics.** Public types, state objects, resources, `GraphicsDevice`,
   SpriteBatch, stock effects, models and member/test coverage.
4. **Current renderer architecture.** Shared contracts first, then STUB and HEADLESS, SOFTWARE,
   EasyGL profiles, SDLGPU, WebGPU, DirectX 9 and the FNA3D adapter. Use the canonical registry at
   the start of the phase in case this list changes.
5. **Content.** Runtime loading, XNB/CNB/CNJ, glTF, `content-pipeline`, XNA pipeline compatibility,
   importers/processors/writers, determinism and corrupted-input handling.
6. **Other framework subsystems.** Audio, media/video, input, devices, phone, storage, networking,
   GamerServices, design-time APIs, diagnostics, Inspector and CNA extensions.
7. **C API.** ABI design, generated routes, handle ownership, callbacks, error translation,
   versioning, native/WASM artifacts and coverage tooling. Language-specific bindings remain out of
   scope unless the project owner explicitly authorizes one.
8. **Cross-cutting quality.** Sanitizers, fuzzing, differential/oracle runs, performance, memory,
   binary/link size, build time, documentation accuracy and release qualification.

Optimization comes only after the owning behavior and tests are understood, with measurements
before and after. A release follows evidence and platform qualification, not a calendar week.

## Time model

Use one plan with an optional production track instead of maintaining competing ten-hour and
twenty-hour documents.

| Track | Weekly time | Purpose |
|---|---:|---|
| Core study | 10 h | 4 h reading, 2 h running/debugging, 2 h verification, 2 h durable notes or documentation |
| Optional production | up to 10 h | Complete a diagnosed fix with regression, measurement and documentation |

The optional track is not permission to double the reading volume. It turns understood findings
into verified changes. Unused time is not debt.

Plan twelve study weeks and one reserve/review week at a time. At the review:

- regenerate the inventory with `loc.sh`;
- compare completed work with evidence, not hours spent;
- carry forward unresolved questions without calling them defects;
- update the next cycle for modules that were added, removed or reorganized;
- discard calendar entries for retired code instead of replacing them with filler.

## Suggested first cycle

This is a starting template, not a permanent project schedule:

| Week | Topic | Evidence produced |
|---:|---|---|
| 1 | Sources of truth and repository map | Commit-pinned module/doc map |
| 2 | Presets, build graph and `loc.sh` | Reproducible native build and current inventory |
| 3 | Physical modules and link closure | Target/dependency diagram plus a validator run |
| 4 | Renderer registry and selection | Canonical identity/family map; retired names understood |
| 5 | Test registration and private GPU execution | Focused CPU and GPU test runs without the live desktop |
| 6 | XNA/FNA oracles and generated coverage | One coverage row traced from evidence to implementation |
| 7 | `core` and Sharp Runtime boundary | Used aliases/types and ownership responsibilities |
| 8 | `math` conventions | Packed layout, numeric and transformation evidence |
| 9 | Platform abstraction | Event, filesystem, timing and native-handle flow |
| 10 | `Game` and runtime lifecycle | Initialize/update/draw/exit/dispose sequence reproduced |
| 11 | Graphics device ownership | Manager/service/device/context lifetime reproduced |
| 12 | Review one coherent public type completely | Checklist, all overloads, docs and tests reconciled |
| 13 | Reserve and replan | Findings classified; next twelve weeks selected |

## Assistance and change discipline

[`AGENTS.md`](../AGENTS.md) is the active contribution policy. Assistance may generate inventories,
mechanical cross-references, candidate diffs, fixtures and implementation drafts, but correctness
comes from XNA/FNA evidence and regression tests. Do not infer semantic authority from who or what
wrote a line, and do not replace review with arbitrary AI line or file quotas.

An atomic fix may legitimately span a declaration, implementation, tests, generated specification
and documentation. Keep that scope coherent and stage only its files. Do not create a production
renderer, module or public API as a learning exercise; use an external scratch project or an
explicitly authorized spike.

## Completion of a phase

A phase is complete when the maintainer can:

- explain the owned public behavior, internal responsibilities and dependency boundaries;
- reproduce its representative build and test path;
- identify generated and external sources of truth;
- trace important behavior to XNA/FNA evidence and a regression;
- distinguish current limitations from historical plans;
- make a small change and predict which tests and consumers it affects.

Completion is about demonstrated control of the current subsystem, not reading every historical
line or producing a predetermined number of commits.
