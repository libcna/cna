# Audit: .github/workflows/input-ci.yml

## Metadata
- Source file: `.github/workflows/input-ci.yml` (126 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-ci` shard
- File type: GitHub Actions workflow (YAML)
- XNA/FNA relevance: N/A — CI infrastructure; runs the backend-agnostic Input test suite
- Main related tests: runs `ctest -L input` across a 5-entry backend matrix

## Purpose
Runs the backend-agnostic input test suite headlessly (via Xvfb) across a 5-way build matrix
(EasyGL, EasyGL+ASan/UBSan, SDL_RENDERER, Vulkan, bgfx), on push to `feature/input`/`develop`/
`master` and PRs to `develop`/`master`, plus manual dispatch.

## Executive Verdict
Correct and well-engineered. The `concurrency` group (`input-ci-${{ github.ref }}`,
`cancel-in-progress: true`) correctly avoids wasting CI minutes on superseded pushes to the same
branch/PR — a real, deliberate resource-hygiene choice, not merely boilerplate.

## Checklist Results
- The `paths-ignore: ['**.md', 'docs/**']` filter on both `push`/`pull_request` correctly avoids
  spinning up this (comparatively expensive, 5-way matrix) job for documentation-only changes —
  consistent with keeping CI cost proportional to actual risk.
- The Xvfb rationale (line 113-114 comment: "the SDL 'dummy' driver has null cursors, which fails
  the MouseCursor tests") is a specific, falsifiable, technically precise justification — not a
  vague "needed for headless" comment.
- `ASAN_OPTIONS`/`UBSAN_OPTIONS` are only meaningfully set for the "ASan+UBSan" matrix entry (empty
  string for the other 4), and `halt_on_error=1` is correctly set for both sanitizers so a detected
  issue actually fails the job rather than merely logging it.
- The comment (line 116-118) explicitly identifies `CNA_INPUT_TEST_FILTER` in `CMakeLists.txt` as
  the ctest label's single canonical source, and states the entry additionally runs the suite
  shuffled 3x for order-independence checking — a genuine, non-trivial extra layer of rigor for a
  CI job (order-dependency bugs are a real, easy-to-miss class of test flakiness).
- `CC`/`CXX` pinned to `gcc-14`/`g++-14` explicitly (not relying on the runner image's floating
  default) — reduces "worked on my machine, broke in CI because the default GCC moved" risk.

## Detailed Findings
None.

## Cross-File Observations
The sibling-repo checkout pattern here (public HTTPS clone of `sharp-runtime`/`easy-gl`/`meta-gl`
from the `develop` branch) differs slightly from `devices-tests.yml`'s equivalent step (also public
HTTPS clone, same three repos, same pattern) — both are internally consistent with each other and
with `d3d-windows-ci.yml`'s `actions/checkout@v4` variant of the same three-repo dependency, so this
is a confirmed, deliberate, project-wide convention (sibling checkouts, not submodules), not an
inconsistency.

## Missing or Weak Tests
Not applicable to a CI workflow file.

## Positive Findings
Concurrency cancellation, precise Xvfb rationale, explicit compiler pinning, and the
shuffled-3x order-independence check are all genuinely strong CI-engineering choices, not just
boilerplate.

## Final Assessment
No findings.
