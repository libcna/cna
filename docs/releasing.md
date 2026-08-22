# Releasing CNA

*Current as of 0.1.0-alpha.1 (2026-08-20).*

CNA follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html). A release is a
git tag plus a `CHANGELOG.md` entry — there is no separate release branch, and nothing is
published to a package registry yet.

## Where the version lives

The version is decided in exactly **one** place:

```cmake
# CMakeLists.txt (repository root)
project(CNA VERSION 0.1.0 LANGUAGES CXX)
set(CNA_VERSION_PRERELEASE "alpha.1")   # empty on a final release
```

`project(VERSION …)` accepts numeric components only, so the pre-release identifier sits beside
it and the two are joined into `CNA_VERSION_STRING` (`0.1.0-alpha.1`). `CNA_VERSION_PRERELEASE`
is deliberately a normal variable and not a cache entry: a cached copy would keep an existing
build directory reporting the previous release after a bump.

Everything else derives from that:

| Consumer | How it gets the version |
|---|---|
| C++ code | `#include "CNA/Version.hpp"` → `CNA::getVersionString()`, `CNA_VERSION_MAJOR`, … |
| CMake consumers | `CNA_VERSION` / `PROJECT_VERSION`, set by `project()` |
| macOS/iOS bundles | `CNA_APPLE_BUNDLE_VERSION`, defaulting to `${PROJECT_VERSION}` |

`CNA/Version.hpp` is **generated** by `cmake/Version.cmake` from
`cmake/templates/Version.hpp.in` into `<build>/generated/include/CNA/Version.hpp`, and the
`core` module publishes that directory as a public include root. Never edit the generated file,
and never hard-code the version anywhere else.

Two copies are maintained by hand and must be updated as part of a bump:

- `CHANGELOG.md` — the release entry and its link definitions at the bottom.
- `Doxyfile` — `PROJECT_NUMBER`.

## Numbers that are *not* the product version

- **`CNA_ABI_VERSION`** (`modules/c-api/include/CNA/C/abi.h`) versions the native C ABI's binary
  contract. It moves when the ABI changes, independently of a product release.
- **XNA 4.0** is the API level CNA reimplements. It is fixed and is not a version of CNA.
- **Per-structure `struct_version` fields** in the C API version individual structures, nothing
  else.

## Pre-1.0 policy

While the major version is 0, a minor bump may change the public API. Pre-release identifiers
are `alpha.N` → `beta.N` → `rc.N`, ordered as SemVer orders them. `0.1.0-alpha.1` precedes
`0.1.0`.

## Cutting a release

1. **Choose the version.** Edit `project(CNA VERSION …)` and/or `CNA_VERSION_PRERELEASE` in the
   root `CMakeLists.txt`, and set `PROJECT_NUMBER` in `Doxyfile` to the same string.
2. **Write the changelog entry.** Move what is under `## [Unreleased]` into a new
   `## [x.y.z] — YYYY-MM-DD` section in `CHANGELOG.md` and add the two link definitions at the
   bottom of the file.
3. **Record the sharp-runtime revision** in that entry. Everything else is pinned by this
   repository — submodule gitlinks, and the `GIT_TAG` values in `cmake/ThirdParty*.cmake` and
   `cmake/RendererSelection.cmake` — but sharp-runtime is a sibling checkout consumed with
   `add_subdirectory`, so the tag alone does not select it:

   ```bash
   git -C ../sharp-runtime log -1 --format='%H (%D, %ad)' --date=short
   ```

   This is a stopgap that documents the revision without enforcing it; a configure-time check
   against a recorded pin, or a submodule, is the intended replacement.
4. **Build and test** in an existing build directory (see the build rules in `CLAUDE.md` —
   reuse a `cmake-build-<variant>/` tree):

   ```bash
   cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_RENDERER=OPENGLES3
   cmake --build cmake-build-debug --target CnaTests -j3
   ctest --test-dir cmake-build-debug --output-on-failure
   ```

   The configure banner prints `CNA: version <x.y.z>` — check it matches. Run `CnaTests` from
   the repository root: its fixtures are resolved relative to the working directory.
5. **Commit** the version-bearing files by explicit name (`CMakeLists.txt`, `Doxyfile`,
   `CHANGELOG.md`), never `git add -A`.
6. **Tag** with a `v` prefix and an annotated tag:

   ```bash
   git tag -a v0.1.0-alpha.1 -m "CNA 0.1.0-alpha.1"
   ```

   The tag string carries the `v`; `CNA_VERSION_STRING` never does.
7. **Push only when the project owner asks**, and push the tag explicitly:

   ```bash
   git push origin develop
   git push origin v0.1.0-alpha.1
   ```
8. **Open the next cycle** by adding an empty `## [Unreleased]` section back to `CHANGELOG.md`
   if step 2 consumed it.
