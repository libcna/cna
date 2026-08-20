<!-- SPDX-License-Identifier: MS-PL -->
# Input pre-merge regression checklist (INP-0200)

> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

Work through this before merging any change that touches `Microsoft::Xna::Framework::Input`
(or `::Touch`) or its internal bridge. Every gate here is machine-checkable from a clean checkout —
manual/hardware verification is tracked separately (see the "Input stable" gate, INP-0199).

## Automated gates (must all pass)

- [ ] **Frozen public API** — `PublicApiInputSignatureFreezeTests` compiles; if a public member changed,
      `docs/input-public-api-frozen.md` was updated in the **same** commit.
- [ ] **No SDL / `CNA::Internal` leak** — `PublicApiInputCompileTests` compiles (its `#error` guard + the
      namespace-placement + Object-exemption `static_assert`s hold).
- [ ] **Enum values frozen** — the 8 exhaustive enum value-drift tests pass (renumbering fails).
- [ ] **Member parity** — `python3 tools/input_parity/gen_input_parity_matrix.py` reports **0 STRICT/EXT
      gaps**; regenerated `docs/input-member-parity-matrix.md` committed if it changed.
- [ ] **Test coverage** — `python3 tools/input_parity/check_input_test_coverage.py` reports **0 orphans**.
- [ ] **Input subset green on every backend** — `ctest -L input` (baked `--gtest_shuffle --gtest_repeat=5`)
      passes on EasyGL / Vulkan / bgfx / SDL_RENDERER (CI matrix), under `xvfb-run` + `SDL_VIDEODRIVER=x11`.
- [ ] **Sanitizers clean** — the ASan+UBSan config runs the input subset green with
      `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` (0 ASan/UBSan errors).
- [ ] **Deviations intact** — every accepted deviation (DEC-* / KBD-011 / INPUT-TOUCH-024) still has its
      pinning test green; any new deviation is recorded in `docs/input-fna-fidelity.md` with a rationale.
- [ ] **Docs counts current** — `docs/input-build-and-test.md` §Test counts matches a fresh run.

## Behavioral change gate

- [ ] Any behavior change is **FNA-cited** (a line reference in `SDL3_FNAPlatform.cs` / the FNA `Input`
      source) and recorded in `docs/input-fna-fidelity.md`. No behavior change lands "because it's cleaner"
      without an FNA basis or an explicit, dated accepted-deviation note.

## Release gate — "Input stable" (INP-0199)

"Input stable" additionally requires: 4-backend green **and** sanitizer green **and** determinism (shuffle
×5) green **and** a **current dated hardware-verification entry** in
`docs/input-manual-verification-results.md` that includes at least one real controller family, a real
touchscreen, and a real IME. Until that manual entry exists, the input subsystem is "code-complete +
headless-verified", **not** "Input stable".
