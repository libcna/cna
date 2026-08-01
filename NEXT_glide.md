# Glide backend continuity

## Session status

- **Branch:** `feature/glide` (clean before this session).
- **Authoritative plan:** [`plan_glide.md`](plan_glide.md).
- **Scope:** authentic 32-bit Windows/x86 Glide 3.x backend only. Do not add a software or modern-API fallback.
- **Session focus:** `GLIDE-AUD-006`, the CPU portion of `GLIDE-FUT-003`, and the core submission of `GLIDE-FUT-001` are implemented. Continue with another independently testable Glide capability while retaining the external visual-validation items.

## Established state

- The backend dynamically loads an external `glide3x.dll`, submits native fixed-function work, and is intentionally limited to the documented Glide subset.
- The local fake DLL / loader test is intended to work independently of CNA, SDL, sharp-runtime and dgVoodoo. Visual smoke validation still needs a compatible external Glide runtime.
- The full CNA i686 smoke executable may remain blocked by the sibling `sharp-runtime` dependency using unsupported i686 `__int128`; record any changed status here rather than working around it in CNA.

## Completed in this session

- Added `GlideAbi.hpp`, the single auditable declaration of all 37 Glide functions used by the renderer and the three native ABI data layouts. `GlideGraphicsBackend.cpp` now resolves that shared contract rather than carrying a second declaration.
- Extended the self-contained x86 fake DLL contract: it resolves the complete renderer-facing export set, calls every signature, and verifies a 37-bit recorder mask. It retains undecorated/decorated stdcall lookup and missing-export coverage.
- Validation passed: configure with the i686 MinGW toolchain, build `cna_glide_abi_loader_test`, then `ctest -R '^GlideAbiLoaderContract$'` under Wine (4.09 s on the latest run). Wine cannot run inside the filesystem sandbox (`SIGSYS`), so the successful test required the approved unsandboxed invocation.
- Direct compilation of `GlideGraphicsBackend.cpp` with i686 MinGW and the configured include paths passed. The ordinary `CNA` target remains blocked before Glide compilation by `sharp-runtime`'s unsupported i686 `__int128` implementation in `System/Int128.hpp` / `UInt128.hpp`.
- Added `GlideLighting.hpp` and six portable unit probes. Glide now evaluates FNA's per-vertex three-light Blinn-Phong term (eye position, material/light specular colors and power), correctly transforms normals through the inverse-transpose world 3x3, preserves FNA's vertex-colour/emissive/specular order, and applies fog after specular using output alpha. This also fixed a pre-existing incorrect normal-matrix cofactor in the old direct calculation.
- Validation passed: the standalone gtest build of `GlideLightingTests.cpp` ran 6/6 green, and the complete `GlideGraphicsBackend.cpp` compiled to an x86 MinGW object. Runtime image tests remain unavailable until an x86 CNA executable can be linked against a portable `sharp-runtime`.
- Implemented the core of `GLIDE-FUT-006`: explicit lower mip uploads are retained as RGBA8, expanded under the active Wrap/Clamp/Mirror mode into the shared power-of-two ARGB4444 pyramid, and re-downloaded after `grFinish`. The shared full level-zero `Texture2D::SetData` path now updates an existing backend in place, preserving those lower levels instead of recreating the texture; `Texture2DTests.cpp` has a recording-backend regression. `GlideTextureMipTests.cpp` adds three portable conversion/padding probes.
- Implemented the core submission of `GLIDE-FUT-001`: `PointListEXT` uses `GR_POINTS`; `LineList` and safely split `LineStrip` runs use `GR_LINES`. `GlidePrimitiveClip.hpp` is a portable homogeneous point/segment clipper with attribute interpolation and a positive-W floor. Its four probes, together with lighting and mip probes, pass 15/15 in the standalone runner. The i686 MinGW source compile passes for both `GlideGraphicsBackend.cpp` and the shared `Texture2D.cpp` update path.
- Audited `GLIDE-FUT-005`: Glide's `GrTexInfo.smallLodLog2`/`largeLodLog2` define the source LOD range passed to download/source, not an independent sampler maximum. CNA's differently sized edge tiles make a direct per-tile `MaxMipLevel` remap seam-variant, so the existing explicit rejection remains correct pending a validated tile-invariant design.
- Audited `GLIDE-FUT-007`: RGB565, ARGB1555 and ARGB4444 are all 16-bit Glide formats, but base-level binary alpha is insufficient to choose ARGB1555 because the current generated mip chain averages alpha and can create fractional lower levels. Keep ARGB4444 until a classifier examines the full padded logical pyramid and fake/runtime sampling tests prove a format switch correct.

## Rules and assumptions

- Use `plan_glide.md` together with this file for all Glide work. `NEXT.md` is outside this subsystem's continuity scope.
- Prefer testable x86 fake-DLL contracts and portable unit tests. Do not claim dgVoodoo or real-Voodoo visual validation without actually running it.
- Preserve unrelated working-tree changes and make only focused Glide/backend/documentation edits.

## Validation in this session

- `g++ -std=c++23 -pthread -Iinclude -Ivendor/googletest/googletest/include -Ivendor/googletest/googletest tests/CNA/Internal/Backends/Glide/GlideLightingTests.cpp tests/CNA/Internal/Backends/Glide/GlideTextureMipTests.cpp tests/CNA/Internal/Backends/Glide/GlidePrimitiveClipTests.cpp vendor/googletest/googletest/src/gtest-all.cc vendor/googletest/googletest/src/gtest_main.cc -o /tmp/cna-glide-unit-tests && /tmp/cna-glide-unit-tests` — passed 15/15.
- Direct i686 MinGW compilation of `src/CNA/Internal/Backends/Glide/GlideGraphicsBackend.cpp` with `-DCNA_BACKEND_GLIDE` and the configured CNA/SDL/sharp-runtime include paths — passed.
- Direct native compilation of `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` with `-DCNA_BACKEND_HEADLESS` and the configured CNA/SDL/sharp-runtime include paths — passed.
- Direct native compilation of `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` with `-DCNA_BACKEND_HEADLESS` and GTest/CNA include paths — passed. The full linked `CnaTests` suite was not completed in this session, so this regression has not yet been executed as part of that suite.
- `ctest --test-dir cmake-build-glide-abi --output-on-failure -R '^GlideAbiLoaderContract$'` — passed 1/1 in 4.09 s under Wine outside the filesystem sandbox.
- `git diff --check` — passed.

## Next action

Next practical work is `GLIDE-AUD-006`'s real-renderer fake-DLL sequence once a portable i686
`sharp-runtime` is available; then expand `GLIDE-AUD-007` with native point/line and texture-mip
images. Until that external dependency is fixed, retain the explicit `MaxMipLevel` rejection and
ARGB4444 default rather than inventing a seam-variant or lossy behavior.
