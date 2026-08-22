# Skia Ganesh/OpenGL artifact (SKIA-159-163)

Status: normative SKIA-159-162 contract, plus a SKIA-163 partial-progress section that is
deliberately **not** claimed as closing that task -- see below for why. SKIA-159 produced a
second, separately pinned GN artifact and a new optional CMake target; SKIA-160 added the
construction-time mode selector and diagnostic on top of it; SKIA-161 added real
default-framebuffer wrapping, flush/submit, swap, readback, and resize; SKIA-162 added genuine GL
context loss/recovery and a best-effort fullscreen proof on top of that. None of the four change
the validated raster artifact, its CMake target, or its selection logic. SKIA-159/160 together
close gate 1 of `docs/skia-surface-mode-adr.md`'s six reopening requirements in full; SKIA-161/162
together close gate 3 ("wrap and present the real backbuffer, including resize and loss/recovery")
in full too. Gates 2, 4, and 6 remain fully open.

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
-- reuse checkouts, no scratchpad builds), the Ganesh artifact reuses the **exact
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
  `cna_configure_skia_ganesh()` works standalone even when `CNA_GRAPHICS_RENDERER` is not `SKIA`.
- `cna_configure_skia_ganesh()` defines `CNA::SkiaGanesh`, an `INTERFACE` target linking the same
  `LINK_GROUP:RESCAN` archive-group pattern the raster target already uses (needed for the same
  reason: the six archives are mutually dependent, and CMake may otherwise reorder them away from
  each other while flattening an `INTERFACE` dependency into a consuming executable), plus
  `Threads::Threads`, `OpenGL::GL` (via `find_package(OpenGL REQUIRED)`), and `${CMAKE_DL_LIBS}`.
- **Nothing in `cmake/RendererSelection.cmake` or `cmake/BackendLibraries.cmake` changed.**
  `CNA_GRAPHICS_RENDERER=SKIA` still links only `CNA::Skia` (raster); `CNA::SkiaGanesh` is not linked
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
separately built dependencies consumed the same way `wgpu-native` already is for the WebGPU renderer.
See `THIRD_PARTY_NOTICES.md`'s new "Skia" section (added by this task -- Skia previously had no
NOTICE entry at all, a pre-existing gap this task closes for both artifacts, not just the new one).

## Functional verification

A below-the-API probe, matching this renderer's established "prove it below the API first" sequencing
(SKIA-93/145/147/153-156): `tools/skia/skia_ganesh_artifact_probe.cpp`, a small standalone (non-GTest)
executable that creates a real SDL `SDL_GLContext`, makes it current, and calls
`GrDirectContexts::MakeGL()` (Skia's zero-argument overload, which internally resolves the native GL
interface -- on this GN configuration, the GLX path described above).

Registered in `cmake/Harnesses.cmake` as `cna_skia_ganesh_artifact_probe`, gated entirely behind
`if(CNA_SKIA_GANESH_BUILD_DIR)` -- it does not exist as a build target in any configuration that
does not explicitly request it, including every already-configured raster build. **Not** registered
as a CTest test: SKIA-160/161 own the real construction-time mode selection and renderer integration
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

Run against the real desktop display at the time (`DISPLAY=:0`):

```text
$ DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_skia_ganesh_artifact_probe
GrDirectContexts::MakeGL() succeeded; maxTextureSize=16384, abandoned=0
$ echo $?
0
```

A real `GrDirectContext` was constructed over a real GLX-backed OpenGL context and reports a
plausible, non-degenerate `maxTextureSize` (16384) -- not just "the archives link," but "Skia's own
Ganesh GL code path genuinely initializes against this machine's OpenGL driver."

**Correction (SKIA-161):** this section originally claimed Xvfb "provides no real hardware GLX" and
must not be used. That is only half right -- Xvfb provides no *hardware-accelerated* GLX, but Mesa's
software rasterizer (llvmpipe) genuinely implements GLX on top of it, confirmed directly by running
SKIA-161's own tests against this repository's existing `:99`/`:101` Xvfb displays with identical
results to the real desktop display. Nothing here needed a real display specifically; a real display
was simply what was used at the time. See SKIA-161's own section below.

## What SKIA-159 explicitly did not do

- No construction-time raster/Ganesh mode selector -- SKIA-160, see below.
- No renderer-owned Ganesh `SkSurface`/context wrapping, flush/submit, swap, resize, or
  context-loss handling inside `SkiaRenderer` (SKIA-161/162).
- No MSAA or anisotropy probing on the Ganesh device (SKIA-164/165) -- `docs/skia-surface-mode-adr.md`
  is explicit that a future GPU mode "must probe its native maximum" rather than inherit the raster
  policy blindly; this task does not attempt that probe.
- No CTest-registered, CI-running Ganesh test -- the probe was a one-off, manually-run,
  real-display-only proof, deleted after its result was recorded above.
- `docs/skia-renderer.md` and `docs/skia-release-gate.md` are deliberately untouched, per their
  existing freeze-until-SKIA-170 policy (same precedent SKIA-158 already followed) -- also true of
  SKIA-160 below.

## SKIA-160: construction-time mode selection and diagnostic

SKIA-159's probe was deliberately thrown away after its result was recorded; it proved the artifact
works, not that CNA has a reusable, testable way to select it. SKIA-160 formalizes exactly that,
staying inside the boundary SKIA-159 already drew: it does **not** wrap a presentable backbuffer,
implement flush/submit/swap/resize, or probe MSAA/anisotropy -- those remain SKIA-161/162/164/165.

### `CNA_SKIA_MODE`: a sub-selector of `CNA_GRAPHICS_RENDERER=SKIA`, not a new renderer identity

Raster and Ganesh/GL are **mutually exclusive GN builds of the same Skia checkout** (SKIA-159): they
define the identical `libskia.a` symbol set with different internal capabilities compiled in, so
linking both into one binary is not possible (duplicate symbol definitions). "Construction-time mode
selection" therefore cannot mean a true single-binary runtime toggle between two linked
implementations -- there is only ever one linked at a time. What SKIA-160 actually adds is:

1. A new CMake cache option, `CNA_SKIA_MODE` (`RASTER`, the default, or `GANESH`), read only when
   `CNA_GRAPHICS_RENDERER=SKIA` is selected. `RASTER` calls the unchanged `cna_configure_skia()` and
   links `CNA::Skia`; `GANESH` calls `cna_configure_skia_ganesh()` and links `CNA::SkiaGanesh`
   instead, plus defines the `CNA_SKIA_MODE_GANESH` compile definition. Neither
   `cmake/ThirdPartySkia.cmake` nor the existing raster call site changed.
2. A new class, `CNA::Internal::Renderers::Skia::SkiaGaneshContext` (deliberately **not** an
   `IGraphicsRenderer` implementation -- SKIA-161 owns that), whose constructor is where "no silent
   runtime fallback" actually lives:
   - Compiled in a `RASTER`-mode build (no `CNA_SKIA_MODE_GANESH` defined, `CNA::Skia` linked, zero
     Ganesh/GL object code present at all), its constructor throws `std::runtime_error`
     **immediately and unconditionally**, before touching SDL or GL -- requesting Ganesh mode where
     it was never built in is a deterministic, display-independent refusal.
   - Compiled in a `GANESH`-mode build, its constructor performs the exact sequence SKIA-159's
     probe already proved works (a real SDL `SDL_GLContext`, made current, handed to
     `GrDirectContexts::MakeGL()`), and throws immediately if any step fails, unwinding every
     acquired resource first -- matching `SkiaRenderer`'s own established constructor
     try/catch-and-unwind pattern exactly. On success it exposes a real, runtime-computed
     diagnostic (`surface=ganesh-gl`, the pinned revision, and a genuinely queried
     `max-texture-size`, unlike raster's fixed `constexpr` diagnostic string, since this value is
     driver-dependent).
3. A single test source, `modules/renderers/skia/examples/skia_ganesh_mode_test.cpp`, that compiles and runs correctly in
   *either* mode and is registered differently depending on which: under `Skia;Raster` (no display
   needed) in a `RASTER` build, proving the refusal path; under the long-reserved
   `Skia;Accelerated;Display` label (`cna_register_skia_accelerated_test`, previously unused since
   SKIA-1) in a `GANESH` build, proving real construction -- the first test that label has ever
   contained. `scripts/validate_skia_release_gate.py` was updated to allow exactly this shape (an
   accelerated registration directly guarded by `if(CNA_SKIA_MODE STREQUAL "GANESH")`) while still
   failing the gate if one were ever registered unconditionally.

### New stable build directory: `cmake-build-skia-ganesh/`

Following this project's own established `cmake-build-<variant>/` convention (already used for
`cmake-build-skia-asan`/`cmake-build-skia-release`, distinct from and layered on top of the
sharp-runtime org-wide closed build-directory list), a new stable, reusable directory was created
once for this configuration and is meant to be reused by every future Ganesh-related Skia task, not
recreated per ticket:

```sh
cmake -S . -B cmake-build-skia-ganesh -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SKIA \
  -DCNA_SKIA_MODE=GANESH \
  -DCNA_SKIA_ROOT=~/deps/skia \
  -DCNA_SKIA_GANESH_BUILD_DIR=~/deps/skia-out/ganesh \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_USE_CCACHE=ON \
  -DCNA_TEST_DISPLAY=:99
cmake --build cmake-build-skia-ganesh -j3
```

`CNA_TEST_DISPLAY=:99` -- one of this repository's existing Xvfb displays, the same ones the raster
suite already uses -- is enough: Mesa's software rasterizer (llvmpipe) provides a real, correctness-
sufficient GLX implementation there, confirmed directly (SKIA-161) by running these same tests
against both `:99` and a real desktop display with identical results. A real display works too; it
is simply not required, and Xvfb avoids disturbing a real desktop session. The other 170 raster-
labeled tests in this same build directory run identically regardless of which kind of X11 server
they see, so one display setting covers the whole directory.

### Verification

Built once from scratch (a genuinely new, permanent configuration, not a per-ticket throwaway) and
confirmed:

- `Skia_Ganesh_ModeConstruction` passes: a real `GrDirectContext` constructs, reports a positive
  `maxTextureSize`, and the diagnostic string contains `surface=ganesh-gl` and the pinned revision.
  A second, independently constructed context on the same window also succeeds, proving no state
  leaks between instances. The negative case (null window) still throws in `GANESH` mode too.
- The full pre-existing raster suite (171 tests, +1 for the new mode-refusal registration) passes
  unchanged in this same Ganesh-linked build directory -- proving the Ganesh archive being linked in
  place of the raster one changes nothing about `SkiaRenderer`'s own raster behavior (the
  Ganesh GN build is a strict superset of the raster APIs, not a divergent implementation of them).
- Back in the original `cmake-build-skia` (still `RASTER`, the default): the new test registers as
  `Skia_Ganesh_ModeRefusal_Raster` under `Skia;Raster`, passes, and `ctest -N -L Accelerated`
  continues to report zero tests -- the default regression build's behavior is provably unchanged.

## SKIA-161: real default-framebuffer wrapping, flush/submit, swap, readback, resize

`SkiaGaneshContext` (SKIA-160) deliberately stopped at "a `GrDirectContext` exists" -- it wraps no
surface at all. SKIA-161 is the first task to actually draw and read back a pixel through the
Ganesh path. It stays inside the same boundary every prior below-the-API Skia task in this renderer
has: no `IGraphicsRenderer` implementation, no wiring into `SpriteBatch`/`GraphicsDevice`, no
resize/loss/recovery *policy* (SKIA-162 -- `Resize()` here is a mechanism the caller must invoke
explicitly, not an automatic reaction to a window event).

### `SkiaGaneshSurface`: composes, does not duplicate, `SkiaGaneshContext`

New `CNA::Internal::Renderers::Skia::SkiaGaneshSurface` owns a `SkiaGaneshContext` by value (not by
inheritance or duplicated construction logic) and wraps its `GrDirectContext` around the real
window-system default framebuffer:

- `GrGLFramebufferInfo{fFBOID=0, fFormat=GL_RGBA8}` -- FBO id 0 is always the real default
  framebuffer; this class never wraps an off-screen FBO-based render target (out of scope).
- `GrBackendRenderTargets::MakeGL(width, height, sampleCnt=1, stencilBits, fbInfo)`, where
  `stencilBits` is queried live via `glGetIntegerv(GL_STENCIL_BITS, ...)` rather than assumed --
  `SkiaGaneshContext`'s GL context creation was extended (SKIA-160's own file) to request an 8-bit
  stencil buffer, matching `SkiaRenderer`'s EasyGL sibling's established precedent, since
  Skia's own header requires this value be exactly 0, 8, or 16.
- `kBottomLeft_GrSurfaceOrigin` -- the real GL default framebuffer's row 0 is the bottom row in
  device memory. `SkSurface`/`SkCanvas` hide this from every caller (draws and `readPixels()` both
  use ordinary top-down coordinates regardless), but only the correct origin here makes that
  abstraction actually correct rather than accidentally flipped -- exactly what this task's own
  acceptance text asks to be *proven*, not just declared.
- `SkSurfaces::WrapBackendRenderTarget(..., colorSpace=nullptr, ...)`.

### A real bug this task's own test caught: the wrong `SkColorSpace`

The first implementation copied `SkiaSurface.cpp`'s raster convention verbatim --
`SkColorSpace::MakeSRGBLinear()` -- for both the wrap and every `readPixels()` call. That colour
space tells Skia the surface stores *linear-light* values, so Skia silently gamma-encoded every
`SkColor` draw going in and gamma-decoded every `readPixels()` coming out. The real GL_RGBA8 default
framebuffer this class wraps stores plain gamma-encoded bytes (no `GL_FRAMEBUFFER_SRGB` was
requested), so that transform was simply wrong here. Pure primaries (0 or 255 per channel) are fixed
points of a gamma curve, so every early check using pure red/blue/green passed anyway; a genuine
mid-tone clear colour (128, 64, 200) first exposed it, reading back as (55, 13, 147) -- matching the
sRGB encode of that value almost exactly. Fixed by passing `nullptr` (no colour management) for both
the wrap and every `readPixels()` call, matching this class's actual plain-bytes contract. Whether
raster's own `MakeSRGBLinear()` choice is itself correct for its own (different) surface type is out
of this task's scope; this fix only concerns `SkiaGaneshSurface`'s own real default framebuffer.

### The double-buffered swap ordering bug

The first test draft called `Present()` (flush + submit + `SDL_GL_SwapWindow`) and *then* read
pixels back to verify them. That is backwards for a double-buffered GL context: after a swap, the
buffer that becomes the new back buffer has driver-dependent, effectively undefined contents (it is
not guaranteed to retain the previous frame, and empirically did not). `SkSurface::readPixels()`
already flushes any pending Skia work on its own, so the correct order is: draw, `ReadPixels()` to
verify (no swap needed for this), *then* `Present()` if the frame should actually reach the screen.
`Skia_Ganesh_Backbuffer`'s draw/verify passes only ever read before their frame's `Present()` call;
`Present()` itself is exercised once per frame purely to prove the swap mechanism does not fail, not
paired with a post-swap readback.

### Verification

`modules/renderers/skia/examples/skia_ganesh_backbuffer_test.cpp` (`Skia_Ganesh_Backbuffer`, `Skia;Accelerated;Display`,
the second member the long-reserved label has ever had) proves, run with a hidden window (CTest's
default) against this repository's `:99` Xvfb display -- confirmed to also pass identically against
a real desktop display, so neither is specifically required:

- wrapping reports a positive size and a real `SkCanvas`;
- an asymmetric top-left-quadrant red rect over a blue background reads back red at the top-left
  sample and blue at all three other corners -- the actual origin-correctness proof, not a
  full-surface clear that would look identical whether the origin were right or flipped;
- a translucent overlay measurably blends rather than being fully replaced or discarded, proving
  alpha is genuinely interpreted through this path;
- `Present()` (flush/submit + `SDL_GL_SwapWindow`) does not throw;
- a real SDL window resize (`SDL_SetWindowSize` + `SDL_SyncWindow` to block until it actually
  applies, mirroring `modules/renderers/easygl/examples/easygl_real_window_resize_test.cpp`'s own established real-resize
  precedent) followed by `Resize()` rewraps the framebuffer at its new dimensions, which are then
  genuinely drawable and readable;
- a second, independent `SkiaGaneshSurface` constructed on the same window after the first is
  destroyed also wraps, draws, and reads back correctly (including the mid-tone colour that caught
  the colour-space bug above), proving no state leaked across instances;
- structurally, `SkiaGaneshSurface`/`SkiaGaneshContext` and their tests contain zero references to
  `src/CNA/Internal/Renderers/EasyGL/` (checked directly) -- the "absence of EasyGL delegation" this
  task's acceptance text asks for.

The same binary also serves as this task's "visible smoke" proof -- the other half of the
acceptance text a hidden-window automated CTest entry cannot satisfy on its own -- via a `--visible`
flag (not passed by CTest, which always runs the hidden/automated form): opens a real, on-screen,
480x480 window instead of a hidden 64x64 one, runs the identical assertions above, then redraws the
same red/blue quadrant pattern once more and holds it on screen for three seconds (pumping events so
the window manager does not consider it unresponsive) before exiting. One binary, two roles,
matching `cna_demo_2d --smoke N`'s own established dual-purpose design rather than maintaining a
second tool.

Not registered in `RASTER`-mode builds: the test's own real assertions are `#if`'d out entirely
there (no Ganesh-only Skia symbol is referenced), since `SkiaGaneshSurface`'s construction-time
refusal in that mode is already proven by `Skia_Ganesh_ModeRefusal_Raster`; registering a
permanently-vacuous always-pass entry would add CTest noise, not coverage.

Verified in both directions exactly like SKIA-160: the Ganesh build (`cmake-build-skia-ganesh`) is
172/172 (up from 171, +1), `Accelerated` now has 2 members; the raster build (`cmake-build-skia`,
still the default) is unchanged at 171/171, `Accelerated` still 0. A new, permanent
`cmake-build-skia-ganesh-asan` directory (Debug + `address,undefined`) was also built from scratch
-- unlike SKIA-160, which explicitly deferred Ganesh-mode sanitizer coverage "to SKIA-161, where the
real Ganesh rendering code lands," this task is that moment: `SkiaGaneshSurface` does real manual
GL/SDL resource management (`SDL_GL_CreateContext`/`DestroyContext`, raw `glGetIntegerv`, Skia's own
`sk_sp` reference counting across a Pimpl) that genuinely benefits from ASan+UBSan scrutiny. That
build is 172/172 as well (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, matching `skia-sanitizer-validation.md`'s already-
documented invocation), zero sanitizer findings. Building it caught one real, separate gap along the
way: `cna_skia_ganesh_artifact_probe` (SKIA-159's own harness, registered directly in
`cmake/Harnesses.cmake` rather than through `cna_skia_test()`) had never received the `-fno-sanitize=
vptr` exception every Skia-linked test executable needs for the pinned no-RTTI archives (documented
in `skia-sanitizer-validation.md`), so it failed to link under UBSan with "undefined reference to
typeinfo for GrDirectContext" the first time anything actually built it under a sanitizer. Fixed by
adding the same conditional exception `cna_skia_test()` already applies. All Ganesh-mode testing in
this task ran against this repository's `:99` Xvfb display, not a real desktop display (see the
SKIA-159 correction note above).

## SKIA-162: genuine context loss/recovery, and a best-effort fullscreen proof

SKIA-161 deliberately proved only the happy path: construct once, draw, resize, done. SKIA-162
closes gate 3 ("wrap and present the real backbuffer, including resize and loss/recovery") by
adding the loss/recovery half, using the real, already-established precedent
`EasyGLRenderer::DebugSimulateContextLoss()` set for this exact architecture (a GL-based
renderer on desktop Linux/Mesa), not inventing a new approach.

### `DebugSimulateContextLossEXT()`: a genuine destroy+recreate, mirroring EasyGL exactly

A real GL context loss cannot be safely forced on this platform (no portable way exists to make a
healthy Mesa/GLX driver actually lose a context on demand). `SkiaRenderer`'s own raster
`DebugSimulateContextLoss()` does not attempt one either -- it reconstructs the SDL presenter while
keeping CPU-owned resources live. `EasyGLRenderer::DebugSimulateContextLoss()`, the closer
architectural sibling (also GL-based), goes further because it has to: it does a real
`SDL_GL_DestroyContext`/`SDL_GL_CreateContext` cycle, reloads GL function pointers, and notifies a
resource registry to recreate every GL-backed resource (shaders/textures/buffers/VAOs) from its
CPU-side description.

New `SkiaGaneshSurface::DebugSimulateContextLossEXT()` mirrors EasyGL's real destroy+recreate
exactly, adapted to what actually exists in this path:

1. Release the wrapped `SkSurface` first (it depends on the old `GrDirectContext`).
2. Destroy the old `SkiaGaneshContext` (`context_.reset()` -- a real `SDL_GL_DestroyContext`).
3. Construct a brand new one (`context_.emplace(window_)` -- a real `SDL_GL_CreateContext` +
   `GrDirectContexts::MakeGL()`, throwing transactionally if either fails, exactly like the
   constructor).
4. Rewrap the backbuffer at the window's current drawable size (`WrapBackbuffer`).

Step 4 is the one genuine simplification versus EasyGL's own "notify a resource registry" step:
there is no resource registry here because no textures/targets/effects exist in the Ganesh path at
all yet (`SkiaGaneshSurface` is not an `IGraphicsRenderer`) -- the wrapped backbuffer surface is the
*only* thing that depends on the context, and rewrapping it is the complete recreate. This is a
declared, not silent, scope boundary: "live textures/targets/effects survive or report deterministic
loss/reset events" (this task's own acceptance text) is vacuously true today because none exist to
fail to survive; a real, non-vacuous version of that claim is real, open, un-vacuous scope for
whichever task first gives Ganesh an `IGraphicsRenderer` (SKIA-163+), not attempted here.

`context_` changed from a plain `SkiaGaneshContext` value member to `std::optional<SkiaGaneshContext>`
specifically to support this in-place destroy+reconstruct -- `SkiaGaneshContext` itself remains a
single-shot RAII object with no reconstruct operation of its own, by design (SKIA-160); the
`optional` is what makes reconstruction possible without changing that.

### Resize and fullscreen

`Resize()` itself is unchanged from SKIA-161 -- this task's contribution is proving it more
thoroughly, not changing its mechanism. `modules/renderers/skia/examples/skia_ganesh_backbuffer_test.cpp` gained:

- three consecutive `DebugSimulateContextLossEXT()` cycles on the same surface, each followed by a
  fresh draw/readback proving the recovered object is genuinely live and non-stale, not just once;
- a best-effort real fullscreen toggle (`SDL_SetWindowFullscreen`), matching the documented
  precedent that this call "may fail in headless / virtual-display environments"
  (`modules/renderers/easygl/examples/easygl_fullscreen_field_test.cpp`'s own comment): if the toggle itself fails, or
  succeeds but the drawable size does not actually change (confirmed to be exactly what happens
  under this repository's `:99` Xvfb display), the check logs an `[INFO]` line and is skipped --
  not counted as a failure, since a virtual display's inability to truly fullscreen is not this
  code's fault. On a platform where fullscreen genuinely changes the drawable size, the same
  `Resize()` mechanism the ordinary resize check already proved is exercised again and verified.

No new "presentation mapping" concept (`CnaPresentationMode`-equivalent virtual resolution,
letterbox, or overscan) was added -- `SkiaGaneshSurface` still uses the window's raw drawable pixels
1:1, exactly as SKIA-161 left it. This is a declared boundary, not an oversight: building a parallel
presentation-mapping system for a component with no `IGraphicsRenderer`/`GraphicsDeviceManager` to
actually drive it would be speculative scope with nothing real to test it against; the real system
already exists for raster (`CnaPresentationMode`) and is the one any future Ganesh
`IGraphicsRenderer` should reuse or mirror once that integration genuinely happens.

"Resource synchronization" (also named in this task's acceptance text) is likewise vacuously
satisfied today: `SkiaGaneshSurface` owns exactly one GL context, used from exactly one thread, with
no concurrent or shared resources to synchronize -- there is nothing for this task to add there
either, until real renderer integration introduces some.

### Verification

Same doubled proof shape as every prior Ganesh task: `cmake-build-skia-ganesh` (Debug) and
`cmake-build-skia-ganesh-asan` (Debug + `address,undefined`) are both 172/172 (test *count*
unchanged from SKIA-161 -- this task extended `Skia_Ganesh_Backbuffer`'s existing checks rather than
registering a new CTest entry), zero sanitizer findings; the raster builds
(`cmake-build-skia`/`-release`/`-asan`, still default) are unchanged at 171/171 in Debug, Release,
and ASan+UBSan, zero sanitizer findings. Two isolated transient test failures during parallel
`-j2` runs (`Skia_RenderTarget2D_Switch`, `Skia_SpriteBatch_NegativeScale`, `Skia_CpuDepthRaster_Spike`,
`Skia_SpriteFont_DefaultChar` -- all pre-existing, unrelated tests, none touched by this task) were
each confirmed to pass cleanly in isolation and on a full sequential rerun, matching this
repository's own established transient-Xvfb-under-parallel-load pattern; none is a real regression.

## SKIA-163: partial progress only -- a real gap in Phase S17's own task sequencing

SKIA-163's row text is "Run complete raster-versus-Ganesh 2D parity, lifecycle, resource-budget,
performance, Release, and sanitizer suites," accepted by "Exact/toleranced oracle results,
ownership, state leakage, repeated reconstruction, and platform boundaries pass before Ganesh is
advertised." Taken literally, this assumes Ganesh already has a working `IGraphicsRenderer` capable
of drawing real 2D scenes through `SpriteBatch`/`Texture2D` -- otherwise there is no "2D parity"
corpus to run, and no "performance" to compare. **No such renderer exists.** SKIA-159-162, entirely
deliberately, all stayed below the public API (`SkiaGaneshContext`/`SkiaGaneshSurface` are not
`IGraphicsRenderer` implementations); nothing in the current `plans/plan_skia.md` task list under any
number actually builds one. This is a genuine gap in Phase S17's own sequencing, discovered while
scoping this task, not a shortfall in how it was executed -- there is no task between SKIA-162 and
SKIA-163 that was supposed to close it.

Given that, this task is **not marked complete** in `plans/plan_skia.md`. What follows is the honest
subset of SKIA-163's acceptance text that genuinely can be satisfied at the level that exists
today, delivered as real, verified work -- not a substitute for the missing renderer integration,
and not claimed as one.

### What was delivered

- New `modules/renderers/skia/examples/skia_ganesh_resource_budget_test.cpp` (`Skia_Ganesh_ResourceBudget`), directly
  addressing the "resource-budget"/"repeated reconstruction" clauses at the `SkiaGaneshSurface`
  level, matching `modules/renderers/skia/examples/skia_resource_budget_test.cpp`'s own 64-cycle scale and precedent:
  - **Phase 1**: 64 independent construct/draw/readback/destroy cycles on the same window, each
    with a distinct verified colour -- proving repeated `SkiaGaneshSurface` construction is stable
    at scale, not just once (SKIA-161) or three times (SKIA-162).
  - **Phase 2**: 64 `DebugSimulateContextLossEXT()` cycles on one long-lived surface, each followed
    by a fresh draw/readback -- deepening SKIA-162's own 3-cycle proof to the same scale.
  - Real memory-safety checking throughout both phases comes from ASan's allocator-level checks
    (use-after-free, double-free, buffer overflow), which stay fully active even with
    `detect_leaks=0` (only LSan's leak *counting* is disabled, for the same pre-existing
    `libGLX_mesa.so.0` baseline-residual reason every other Ganesh/GLX test already documents).
    Byte-level leak counting across 128 real GL context create/destroy cycles was not attempted --
    the known baseline noise (confirmed non-zero and non-deterministic in earlier tasks) would make
    any such count meaningless without also isolating the host GLX presenter (dummy/software SDL
    driver), which does not work for a GL-context-requiring renderer the way it does for raster.
- **First-ever Ganesh-mode Release build and verification**: a new permanent
  `cmake-build-skia-ganesh-release` directory (mirroring `cmake-build-skia-ganesh`/`-asan`'s
  existing convention), 173/173. SKIA-160-162 each verified Ganesh in Debug (and, from SKIA-161
  onward, ASan+UBSan) but never Release; this closes that specific gap, directly addressing this
  task's "Release... suites" clause.
- "Platform boundaries": re-confirmed, not newly built -- the `RASTER`-mode refusal
  (`SkiaGaneshContext`/`SkiaGaneshSurface` both throw unconditionally there, SKIA-160/161) and the
  null-window/invalid-construction refusals (SKIA-160) remain exactly as tested.
- "Exact/toleranced oracle results": still only the pixel-level formulas SKIA-161 already proved
  (origin, channel order, alpha blending), now additionally proven stable across many independent
  constructions (this task's Phase 1) rather than a single one. Not extended to any new formula or
  scene, and explicitly not a 2D-scene-level oracle -- there is no scene-rendering path to compare.

### What remains genuinely open

- A real 2D-scene "parity" oracle (rendering the same `SpriteBatch` scene through raster and
  through Ganesh and comparing pixels) cannot exist until some task gives Ganesh an
  `IGraphicsRenderer`. No such task currently has a number in `plans/plan_skia.md`.
  `docs/skia-3d-emulation-adr.md`-style architecture-decision framing might be the right vehicle
  for scoping that work properly (a full `IGraphicsRenderer` is a materially larger undertaking than
  any single SKIA-159-163 task, closer in scope to the entire raster `SkiaRenderer`
  implementation) rather than assuming it fits inside the next available task number.
- "Performance" comparison has no meaning without that same scene-rendering path.
- Full LSan byte-level leak counting across GL context churn remains undemonstrated for the reason
  given above.

Verified: `cmake-build-skia-ganesh`, `-asan`, and the new `-release` are all 173/173 (up from 172,
+1 -- `Skia_Ganesh_ResourceBudget`), zero sanitizer findings; the raster builds
(`cmake-build-skia`/`-release`/`-asan`, still default) are unchanged at 171/171 in all three, zero
sanitizer findings, `Accelerated` still 0 there. `docs/skia-renderer.md`/`docs/skia-release-gate.md`
deliberately untouched, per their existing freeze-until-SKIA-170 policy.
