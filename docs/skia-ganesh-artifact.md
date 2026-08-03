# Skia Ganesh/OpenGL artifact (SKIA-159)

Status: normative SKIA-159 contract. Produces a second, separately pinned GN artifact and a new
optional CMake target; changes nothing about the validated raster artifact, its CMake target, or
its selection logic. Opens gate 1 of `docs/skia-surface-mode-adr.md`'s six reopening requirements
(the artifact half only -- construction-time mode selection is SKIA-160's job).

## Why a second artifact, not a second mode of the existing one

`docs/skia-surface-mode-adr.md` names Ganesh/OpenGL "the first future accelerated candidate" and is
explicit that adding it "requires a successor plan and must reopen the surface, reset, parity,
sanitizer, MSAA, and anisotropy gates." Its own evaluated-routes table already fixes the shape: a
`GrDirectContexts::MakeGL` context wrapping a real SDL `SDL_GLContext`, not Vulkan or Graphite (both
explicitly rejected/deferred there). SKIA-159 does not reopen any of those gates; it produces the one
prerequisite every later gate needs to even begin: a Ganesh-enabled Skia build that coexists with,
and is fully independent of, the already-validated raster one.

Skia's raster and Ganesh/GL configurations are **mutually exclusive GN outputs of the same source
checkout** -- `skia_use_gl`/`skia_enable_ganesh` are build-time flags baked into `libskia.a`, not a
runtime switch. They therefore need two separate GN output directories built from one shared source
checkout, exactly the way `docs/skia-developer-build.md`'s raster procedure already works, doubled.

## Shared source checkout, two GN outputs

Per the repository's own build-hygiene rules (`/rv/data/development/github.com/openeggbert/CLAUDE.md`
-- reuse checkouts, cap parallelism, no scratchpad builds), the Ganesh artifact reuses the **exact
same pinned Skia source checkout** the raster artifact already uses. It is not re-cloned:

```text
Revision: ebf50520d720a1ce9d842d942d04c6c39c3fbc7b   (identical to the raster artifact)
Source:   the same shared checkout the raster artifact's docs/skia-developer-build.md already names
```

Only the GN output directory and the two GPU-enabling GN args differ from the raster command:

```sh
# From within the shared Skia source checkout ($CNA_SKIA_SRC):
"$CNA_SKIA_SRC/bin/gn" gen "$CNA_SKIA_GANESH_OUT" --args='is_official_build=true is_debug=false cc="clang" cxx="clang++" skia_use_gl=true skia_enable_ganesh=true skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false skia_enable_pdf=false skia_use_freetype=false skia_use_fontconfig=false skia_use_libpng_decode=false skia_use_libjpeg_turbo_decode=false skia_use_libwebp_decode=false skia_use_wuffs=false skia_use_icu=false skia_enable_tools=false'
ninja -C "$CNA_SKIA_GANESH_OUT" -j3 skia
```

Every other GN arg is byte-identical to the raster command (`docs/skia-developer-build.md`): still no
Vulkan, Dawn, Graphite, PDF, FreeType, fontconfig, image-decoder libraries, wuffs, ICU, or Skia's own
tools -- this is the minimal Ganesh/OpenGL surface, not a general-purpose Skia build. On this GN
revision, `skia_use_gl=true skia_enable_ganesh=true` are actually the upstream *defaults*
(`gn/skia.gni`); they are still spelled out explicitly here, matching the raster command's own style
of never relying on an implicit default for a capability-determining flag.

`skia_use_x11` (default `true` on Linux) and `skia_use_egl` (default `false`) are left at their
defaults, which selects Skia's GLX native-interface path
(`src/gpu/ganesh/gl/glx/GrGLMakeGLXInterface.cpp`) -- consistent with
`docs/skia-surface-mode-adr.md`'s "Ganesh/OpenGL default framebuffer" route. This is the only GN
configuration SKIA-159 built or tested; EGL/Wayland is not evaluated.

## Archive list

Ganesh/GL sources compile into `libskia.a` itself; enabling them does not introduce a seventh
archive. The Ganesh output directory produces **the same six archive names** as the raster one:

```text
libskia.a
libskcms.a
liballocator_base.a
liballocator_core.a
liballocator_shim.a
libraw_ptr.a
```

Confirmed directly against the built output directory listing, not assumed from the raster
precedent. `libskia.a` is roughly 18.5 MB here versus the raster artifact's smaller size (the
Ganesh/GL/GLX sources, absent from the raster build, are now compiled in); the total Ganesh GN
output directory is about 41 MB.

## Extra system library: `-lGL`

Reading `BUILD.gn`'s own conditional `libs +=` additions for this exact configuration
(`skia_use_gl && is_linux && skia_use_x11 && !skia_use_egl`, none of `is_android`/`is_mac`/`is_win`)
shows exactly one new system library requirement beyond what the raster artifact already links:
`libs += [ "GL" ]` (system Mesa/libGL, providing the GLX entry points Skia's native interface calls).
No other conditional `libs +=` block in `BUILD.gn` is reachable under this GN configuration (checked
line-by-line, not assumed) -- `dl`/pthread were already required by the raster artifact.

## CMake integration

New `cmake/ThirdPartySkiaGanesh.cmake`, parallel to and independent of the existing
`cmake/ThirdPartySkia.cmake` (untouched by this task):

- `CNA_SKIA_GANESH_BUILD_DIR` (new cache var): path to the Ganesh GN output directory. Optional --
  unset by default, so no existing configured build (raster or otherwise) is affected merely by this
  file existing.
- `CNA_SKIA_ROOT` is declared defensively in this file too (a repeated `CACHE` declaration of the
  same variable name is a CMake no-op that preserves any already-set value), so
  `cna_configure_skia_ganesh()` works standalone even when `CNA_GRAPHICS_BACKEND` is not `SKIA`.
- `cna_configure_skia_ganesh()` defines `CNA::SkiaGanesh`, an `INTERFACE` target linking the same
  `LINK_GROUP:RESCAN` archive-group pattern the raster target already uses (needed for the same
  reason: the six archives are mutually dependent, and CMake may otherwise reorder them away from
  each other while flattening an `INTERFACE` dependency into a consuming executable), plus
  `Threads::Threads`, `OpenGL::GL` (via `find_package(OpenGL REQUIRED)`), and `${CMAKE_DL_LIBS}`.
- **Nothing in `cmake/BackendSelection.cmake` or `cmake/BackendLibraries.cmake` changed.**
  `CNA_GRAPHICS_BACKEND=SKIA` still links only `CNA::Skia` (raster); `CNA::SkiaGanesh` is not linked
  by any existing target. Construction-time mode selection between the two is explicitly SKIA-160's
  job, not this task's.

### Missing/mismatched artifact diagnostics

`cna_configure_skia_ganesh()` fails transactionally with an actionable `FATAL_ERROR`, mirroring the
raster target's existing diagnostics in `cmake/ThirdPartySkia.cmake`, for each of:

- `CNA_SKIA_ROOT` unset or missing `include/core/SkSurface.h` (same check the raster target uses).
- `CNA_SKIA_ROOT` present but missing `include/gpu/ganesh/gl/GrGLDirectContext.h` -- catches a
  checkout of a different, non-Ganesh-capable Skia revision (Ganesh's public headers are part of
  every checkout of the pinned revision regardless of GN build args, so their absence signals a
  wrong or corrupted source tree, not merely an unbuilt one).
- `CNA_SKIA_GANESH_BUILD_DIR` unset or missing `libskia.a`.
- `CNA_SKIA_GANESH_BUILD_DIR` equal to `CNA_SKIA_BUILD_DIR` -- the two GN configurations are
  incompatible outputs of the same checkout and can never share one output directory; this is
  caught explicitly rather than surfacing as a confusing downstream link error.
- Any of the six archives missing from `CNA_SKIA_GANESH_BUILD_DIR` (per-archive check, naming the
  exact missing file, same pattern as the raster target).

## License / NOTICE

Skia (both the raster and Ganesh/GL artifacts -- one shared upstream project) is BSD-3-Clause,
copyright Google Inc. Neither artifact vendors Skia source into this repository; both are external,
separately built dependencies consumed the same way `wgpu-native` already is for the WebGPU backend.
See `THIRD_PARTY_NOTICES.md`'s new "Skia" section (added by this task -- Skia previously had no
NOTICE entry at all, a pre-existing gap this task closes for both artifacts, not just the new one).

## Functional verification

A below-the-API probe, matching this backend's established "prove it below the API first" sequencing
(SKIA-93/145/147/153-156): `tools/skia/skia_ganesh_artifact_probe.cpp`, a small standalone (non-GTest)
executable that creates a real SDL `SDL_GLContext`, makes it current, and calls
`GrDirectContexts::MakeGL()` (Skia's zero-argument overload, which internally resolves the native GL
interface -- on this GN configuration, the GLX path described above).

Registered in `cmake/Harnesses.cmake` as `cna_skia_ganesh_artifact_probe`, gated entirely behind
`if(CNA_SKIA_GANESH_BUILD_DIR)` -- it does not exist as a build target in any configuration that
does not explicitly request it, including every already-configured raster build. **Not** registered
as a CTest test: SKIA-160/161 own the real construction-time mode selection and backend integration
this probe deliberately does not attempt.

Built by reconfiguring the existing `cmake-build-skia` directory in place (adding
`-DCNA_SKIA_GANESH_BUILD_DIR=...` to its cache, reusing its already-built SDL3 rather than paying for
a redundant from-scratch SDL3 build in a fresh directory -- the repository's own build-hygiene rule
1, "reuse an existing build directory whenever usable"), building only the new target
(`cmake --build cmake-build-skia --target cna_skia_ganesh_artifact_probe -j3`), and confirmed
afterward that the full raster `Skia_*` suite (170/170) still passes unchanged in the same directory
-- concrete proof of "without changing the validated raster artifact," not merely an assumption from
not having touched its files. The probe binary and its intermediate object files were deleted after
recording the result below, per this repository's build-probe hygiene convention; the CMake
registration and source remain, so rebuilding it is a one-line, ccache-accelerated operation.

Run against the real desktop display (`DISPLAY=:0`), not Xvfb -- the same requirement already
established for the EasyGL golden build; Xvfb provides no real hardware GLX:

```text
$ DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_skia_ganesh_artifact_probe
GrDirectContexts::MakeGL() succeeded; maxTextureSize=16384, abandoned=0
$ echo $?
0
```

A real `GrDirectContext` was constructed over a real GLX-backed OpenGL context and reports a
plausible, non-degenerate `maxTextureSize` (16384) -- not just "the archives link," but "Skia's own
Ganesh GL code path genuinely initializes against this machine's OpenGL driver."

## What this task explicitly does not do

- No construction-time raster/Ganesh mode selector (SKIA-160).
- No backend-owned Ganesh `SkSurface`/context wrapping, flush/submit, swap, resize, or
  context-loss handling inside `SkiaGraphicsBackend` (SKIA-161/162).
- No MSAA or anisotropy probing on the Ganesh device (SKIA-164/165) -- `docs/skia-surface-mode-adr.md`
  is explicit that a future GPU mode "must probe its native maximum" rather than inherit the raster
  policy blindly; this task does not attempt that probe.
- No CTest-registered, CI-running Ganesh test. The probe above is a one-off, manually-run,
  real-display-only proof, not part of the regression suite.
- `docs/skia-backend.md` and `docs/skia-release-gate.md` are deliberately untouched, per their
  existing freeze-until-SKIA-170 policy (same precedent SKIA-158 already followed).
