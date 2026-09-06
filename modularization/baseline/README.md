# No-loss baseline (pre-modularization)

Captured at feature/modularization commit 5f2c4e941 (tree 40494b21b) before any
production change, per the campaign's no-loss/completeness gate.

- `files-production.tsv`, `files-tests.tsv`, `files-build-tooling.tsv` — sha256 + path of every
  tracked file in the respective areas (`capture_inventory.py`).
- `api-decls.tsv` — mechanical class/struct/enum declaration inventory over `include/`.
- `graph.py` / `graph.json` — the include-graph derivation tool and its full edge data
  (plans/MODULARIZATION_PLAN.md §1.4).
- `ctest-names-opengles.txt` — `ctest -N` registration names of the pristine OPENGLES
  (default) configuration, 6526 entries.
- `gtest-list-opengles.txt` — `CnaTests --gtest_list_tests` output of the pristine binary.
- `targets-static-decl.txt` — static `add_library`/`add_executable` declaration inventory of the
  pristine cmake tree (macro-created per-test executables appear as `${target}`; their real
  names are covered by the ctest inventory).

Control run note (pristine binary, recorded 2026-08-09): the full unshuffled `CnaTests` under
`xvfb-run` on the OPENGLES/EasyGL configuration segfaults at test #269
(`MetalResourceHealth.RenderTargetCubeBackendEscapesThroughTextureCubeBaseMove`) after
repeated real-GL device creation — pre-existing behavior of the pristine tree in this
environment, consistent with the previous campaign's practice of validating the full portable
suite on HEADLESS and OPENGLES via smoke/corpus subsets. The pristine binary is preserved at
`build-probe/CnaTests-pristine-opengles` for A/B parity runs.
