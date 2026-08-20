# Audit: scripts/run-all-backend-smoke-tests.sh

## Metadata
- Source file: `scripts/run-all-backend-smoke-tests.sh` (71 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (cross-backend orchestrator)
- XNA/FNA relevance: N/A (developer/CI tooling)
- Main related tests: runs each backend's own `GraphicsSmoke`-labeled CTest suite

## Purpose
Sequentially configures/builds/runs the 3-frame smoke test for EasyGL, Vulkan, Bgfx, and
SDL_Renderer, using this project's own established persistent per-backend build directories,
gracefully skipping (not failing the whole run) a backend that can't configure on the current
machine.

## Executive Verdict
Correct and reasonably defensive. Sequential (never concurrent) execution is explicitly stated as
matching this project's own testing discipline. The distinction between "backend unavailable"
(configure failure → skip, not fail) and "backend available but smoke test failed" (real failure,
non-zero overall exit) is a sound, useful design for a script meant to compose into CI.

## Checklist Results
- `cmake --build "${dir}" -j4 -- -k` correctly documents (via comment) that `-k` (keep going past
  errors) is deliberately used to route around a known, unrelated, pre-existing `cna_demo_xact`
  Content-copy build error — and explicitly notes that ctest's own exit code, not the build
  command's, is the real pass/fail signal here. This is a reasonable, disclosed workaround for a
  known issue rather than a silent one.
- `set -uo pipefail` (not `-e`) is a deliberate choice consistent with the script's own control
  flow, which needs to continue past a failed `cmake`/`ctest` invocation for one backend to still
  attempt the others — using `-e` would have defeated the whole "report all backends, not just the
  first failure" design.

## Detailed Findings
None in this file. The `cna_demo_xact` Content-copy build error it references is a known,
already-tracked issue per its own comment (pointing to `NEXT.md`/`plans/plan_graphics.md`), not a new
finding.

## Cross-File Observations
Complements the per-backend `cmake/Tests/*.cmake` files' own `GraphicsSmoke`-labeled registrations
(`EasyGL_Demo2D_SmokeTest`, `Vulkan_Demo2D_SmokeTest`, `Bgfx_Demo2D_SmokeTest`,
`SDL_Renderer_Demo2D_SmokeTest`, all audited in the `build-cmake-tests` shard) — this script is the
orchestrator that runs all of them across their respective persistent build directories in one
invocation.

## Missing or Weak Tests
N/A (orchestration script, not itself under test).

## Positive Findings
Sound, disclosed handling of a known pre-existing build error via `-k`, rather than either failing
the whole run on an unrelated issue or silently suppressing the real ctest signal.

## Final Assessment
No findings.
