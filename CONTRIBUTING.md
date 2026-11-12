# Contributing to CNA

The rules a change to CNA has to follow. The documents they point to hold the detail.

## Where things are

- [KNOWN_ISSUES.md](KNOWN_ISSUES.md): the current state and the one list of open defects. A fixed defect leaves
  the list; finished work is recorded only in Git history.
- [docs/README.md](docs/README.md): the documentation index. [CHECKLIST.md](CHECKLIST.md) is the per-file porting
  checklist. [docs/physical-modules.md](docs/physical-modules.md) describes the module layout.
- [docs/development-environment.md](docs/development-environment.md): packages, sibling checkouts
  (`../sharp-runtime`, `../easy-gl`, `../meta-gl`), the private display and cross-development.

## Behaviour: XNA 4.0 is the specification

- FNA is the everyday readable reference. Measured Microsoft XNA 4.0 wins when the two disagree, so "FNA does it"
  is not a reason to keep a behaviour XNA does not have.
- Settle a disagreement from the IL of the shipped assemblies. Where two XNA builds differ, 4.0 Refresh
  (4.0.30901) is the reference. Name the IL offsets in a comment at the code. Add a row with the evidence and the
  regression test to [docs/xna-fna-behavior-decisions.md](docs/xna-fna-behavior-decisions.md).
- Preserve XNA's packed layouts, clamping, integer casts, default values, overloads and exception types and messages.
  Do not redesign a behaviour into cleaner C++.

## API conventions

- XNA types stay in their XNA namespace (`Microsoft::Xna::Framework::...`). The names of classes, members, enums and
  constants match XNA exactly. `CNA::` holds only extensions and internals.
- A C# property becomes `getXProperty()` / `setXProperty(...)`. .NET primitives use the sharp-runtime aliases
  (`intcs`, `bytecs`, `Single`, `String`, ...). A missing `System.*` piece is added to sharp-runtime first, as a
  minimal stub with its final name.
- Mark every member in `Microsoft::Xna` that XNA does not have with `CNAEXT`. C# `internal` members do not become
  public C++.
- Every public declaration in a header has a `/** @brief ... */` Doxygen block (`@param`, `@return` where they apply).
  Non-template implementation goes in `.cpp`. Keep member order close to the C# source.
- Do not add aliases or shortcuts to keep old code compiling; fix the old code.

## Tests

- Use Google Test under the owning `modules/<name>/tests/`, mirroring the namespace path.
- Every public overload, constructor, operator and constant needs a test. Test out-ref overloads, static factories,
  `==`/`!=`/`Equals` (both outcomes), `ToString` and `GetHashCode` separately.
- A behaviour fix comes with a test that fails before the fix and passes after it. A test that pinned the old
  behaviour is rewritten to pin the new one, not loosened to accept both.
- Generate API presence with `python3 tools/audit_xna_runtime_surface.py --write-reports`. Presence does not mean
  the API is tested.

## Building

- Build in the repository's own `cmake-build-<variant>/` trees (`build/` for a one-off). Rebuild them
  incrementally. Never build in `/tmp`. Build the targets a change needs (a test binary or an example), not the
  whole tree. There is no `CNA` target.
- `CNA_USE_CCACHE=ON` is the default; share one cache across trees with a common `CCACHE_BASEDIR`. Against a
  shared cache, never export `CCACHE_MAXSIZE`, `CCACHE_HASHDIR` or `CCACHE_NOHASHDIR`
  ([docs/build-performance.md](docs/build-performance.md#ccache)).
- Limit parallel jobs to what memory allows (`--parallel N`, not a bare `--parallel`). Sanitizer and template-heavy
  translation units need several GB each.

## Running tests

- GPU and window tests run only on the private display:
  `tools/platform/run_gpu_tests_private.sh <build-dir> [ctest args]`. It starts a headless Weston with a private
  Xwayland on the real GPU. Nothing is ever run on the live desktop.
- `scripts/check_clean_checkout.sh -- <command>` proves that a run left the checkout clean.
- After touching SDL use, the platform boundary or a renderer, run the gates:

  ```bash
  python3 tools/platform/sdl_inventory.py --check
  python3 tools/platform/sdl_classify.py --check
  python3 tools/platform/renderer_sdl_audit.py --check
  python3 tools/platform/sdl_ratchet.py --check
  python3 tools/platform/nonproduction_sdl_audit.py --check   # also SDL references in tests
  python3 tools/platform/hot_path_lint.py
  python3 scripts/check_renderer_identities.py
  ```

  SDL is called only from the platform, audio and `sdl-gpu`/`fna3d` renderer edges
  ([docs/platform-abstraction.md](docs/platform-abstraction.md)).

## Scope

- The renderer set is curated. A retired renderer is never reintroduced, and its identity number is never reused
  ([docs/removed-renderers.md](docs/removed-renderers.md)).
- Only the C API ([docs/c-api/](docs/c-api/README.md)) lives here. A new language binding needs the owner's
  agreement.

## Commits

- One task, one commit. Commit only after the affected targets have built, the tests have passed and
  `KNOWN_ISSUES.md` is current.
- Stage files by name. Never use `git add -A` or `git add .`. Read `git diff --cached --stat` before committing.
- The message is `type(TASK-ID): what changed`. The body explains why, with the evidence (IL offsets, measured
  values, the red/green test), matching the detail of `git log`.
- A change that breaks a downstream project (cna-demos, cna-samples, the templates, cna-dotnet) is fixed or reported
  there too.
