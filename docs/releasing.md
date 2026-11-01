# Releasing CNA

*Product release: 0.2.0 (2026-10-28); procedure checked 2026-11-01.*

CNA follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html). A release is a
reviewed commit with an annotated Git tag and a `CHANGELOG.md` entry. Use the owner's approved
release branch; 0.2.0 was cut from `next`. This procedure covers the native repository release,
while C ABI publication and external consumer packages have their own readiness requirements.

## Where the version lives

The version is decided in exactly **one** place:

```cmake
# CMakeLists.txt (repository root)
project(CNA VERSION 0.2.0 LANGUAGES CXX)
set(CNA_VERSION_PRERELEASE "")   # e.g. "beta.1"; empty on a final release
```

`project(VERSION …)` accepts numeric components only, so the pre-release identifier sits beside
it and the two are joined into `CNA_VERSION_STRING` (`0.2.0`, or a chosen pre-release such as
`0.2.0-beta.1`). `CNA_VERSION_PRERELEASE`
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

These copies are maintained by hand and must be updated as part of a bump:

- `CHANGELOG.md` — the release entry and its link definitions at the bottom.
- `Doxyfile` — `PROJECT_NUMBER`.
- `README.md` — the *Current release* line and the sibling releases it requires (step 3).

## Numbers that are *not* the product version

- **`CNA_ABI_VERSION`** (`modules/c-api/include/CNA/C/abi.h`) versions the native C ABI's binary
  contract. It moves when the ABI changes, independently of a product release.
- **XNA 4.0** is the API level CNA reimplements. It is fixed and is not a version of CNA.
- **Per-structure `struct_version` fields** in the C API version individual structures, nothing
  else.
- **The installed `CNAConfigVersion.cmake` package version** follows the C ABI version, not the
  product version. `CNA_RUNTIME_VERSION` exposes the product identity after C++ SDK discovery;
  see [installed-cpp-sdk.md](installed-cpp-sdk.md).

The native C ABI's [release gate](c-api/RELEASE_GATE.md) has its own verdict and coverage
requirements. A product version/tag or a successful gate-consistency test does not turn a
Not ready C ABI verdict into Ready.

## Pre-1.0 policy

While the major version is 0, a minor bump may change the public API. Pre-release identifiers
are `alpha.N` → `beta.N` → `rc.N`, ordered as SemVer orders them. `0.1.0-alpha.1` precedes
`0.1.0`.

## Cutting a release

1. **Choose the version and release commit.** Work on the owner-approved branch and account for
   unrelated local changes before staging. Edit `project(CNA VERSION …)` and/or `CNA_VERSION_PRERELEASE` in the
   root `CMakeLists.txt`, and set `PROJECT_NUMBER` in `Doxyfile` to the same string.
2. **Write the changelog entry.** Move what is under `## [Unreleased]` into a new
   `## [x.y.z] — YYYY-MM-DD` section in `CHANGELOG.md` and add the two link definitions at the
   bottom of the file.
3. **Require the sibling releases.** Bundled dependencies use this repository's submodule gitlinks
   and version/hash declarations; system libraries, SDKs and local dependency overrides need
   separate build evidence. sharp-runtime, easy-gl and meta-gl are sibling checkouts
   consumed with `add_subdirectory`, so the CNA tag alone does not select them. Release them
   first, check their tags out next to this repository, and set
   `CNA_REQUIRED_SHARP_RUNTIME_VERSION`, `CNA_REQUIRED_EASYGL_VERSION` and
   `CNA_REQUIRED_METAGL_VERSION` in `cmake/DependencyVersions.cmake`. Configuration then refuses
   any checkout that declares neither that version nor a later patch release of the same minor
   line. Record the exact commits in the entry's *Dependencies* table:

   ```bash
   for r in sharp-runtime easy-gl meta-gl; do
       git -C ../$r describe --tags --exact-match HEAD   # must name the release tag
       git -C ../$r log -1 --format='%H (%D, %ad)' --date=short
       git -C ../$r status --short                       # must be clean
   done
   ```
4. **Build and test** in an existing build directory (see the build rules in `CLAUDE.md` —
   reuse a `cmake-build-<variant>/` tree):

   ```bash
   cmake -S . -B cmake-build-multi
   cmake --build cmake-build-multi --parallel 8
   tools/platform/run_gpu_tests_private.sh cmake-build-multi -j8 --output-on-failure
   ```

   Verify the actual `CNA_GRAPHICS_RENDERERS` cache list rather than assuming the directory name
   enables every renderer. The established multi tree includes the GL family, WebGPU, SDLGPU,
   FNA3D, SOFTWARE, HEADLESS and STUB; it does not replace other platform/renderer qualification.
   GL-family configurations exercise easy-gl and meta-gl alongside Sharp Runtime; they are not
   unique to this one build tree. GPU and window tests must run on the private display,
   never on the owner's desktop (`CLAUDE.md` § *Running GPU/window tests*). The configure output
   prints `CNA: version <x.y.z>` and one `CNA: <sibling> <version> at <path>` line per sibling —
   check the product and every sibling selected by that configuration. Retain actual pass/fail/skip
   results, platform/compiler/dependency identities and remaining limitations; a skipped path is
   not release qualification. Installed SDK/C package checks and cross-platform runs follow their
   own documented scopes in [installed-cpp-sdk.md](installed-cpp-sdk.md),
   [c-api/CONSUMING.md](c-api/CONSUMING.md) and [c-api/RELEASE_GATE.md](c-api/RELEASE_GATE.md).
5. **Commit** the version-bearing files by explicit name (`CMakeLists.txt`, `Doxyfile`,
   `CHANGELOG.md`, `README.md`, and `cmake/DependencyVersions.cmake` when a sibling requirement
   changed), never `git add -A`.
6. **Tag** with a `v` prefix and an annotated tag:

   ```bash
   CNA_RELEASE_VERSION='X.Y.Z' # replace with the chosen version, including any pre-release suffix
   git tag -a "v${CNA_RELEASE_VERSION}" -m "CNA ${CNA_RELEASE_VERSION}"
   ```

   The tag string carries the `v`; `CNA_VERSION_STRING` never does.
7. **Push only when the project owner asks**, and push the tag explicitly:

   ```bash
   git push origin HEAD
   git push origin "v${CNA_RELEASE_VERSION}"
   ```
   `HEAD` here is the approved release branch from step 1; do not substitute an unrelated working
   branch. Keep sibling publication ordering and permissions explicit with the owner.
8. **Open the next cycle** by adding an empty `## [Unreleased]` section back to `CHANGELOG.md`
   if step 2 consumed it.
