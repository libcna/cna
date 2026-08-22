# Building and validating the Skia renderer

This is the reproducible developer procedure for the experimental
`CNA_GRAPHICS_RENDERER=SKIA` CPU-raster 2D renderer. The commands describe the only implemented Skia
execution mode. They do not build or silently select Ganesh, Graphite, OpenGL, Vulkan, Metal, or
Dawn. Read [`skia-renderer.md`](skia-renderer.md) for the verified feature boundary before treating a
successful build as a capability claim.

## Supported build shape

The validated host shape is Linux x86-64, a GNU or Clang C++23 CNA build, Ninja, and ELF static
linking against a Clang-built upstream Skia artifact. Ordinary CNA configuration is offline: CMake
never downloads or rebuilds Skia. The source checkout and matching GN output are explicit inputs.

The Skia revision is exactly:

```text
ebf50520d720a1ce9d842d942d04c6c39c3fbc7b
```

The current adapter requires these six files in one GN output directory:

```text
libskia.a
libskcms.a
liballocator_base.a
liballocator_core.a
liballocator_shim.a
libraw_ptr.a
```

CMake rejects a missing source header or any missing archive. Do not point the source option at one
Skia revision and the build option at another.

## 1. Prepare CNA prerequisites

On Debian/Ubuntu, install the build/test tools and the optional FFmpeg development packages used by
VideoPlayer tests:

```sh
sudo apt-get update
sudo apt-get install -y \
  git cmake ninja-build build-essential clang python3 pkg-config xvfb xauth libgl1-mesa-dri \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

The default CNA build compiles SDL3, SDL3_image, and SDL3_mixer from the repository submodules.
Optional SDL features may need additional packages listed in `third_party/SDL/docs/README-linux.md`,
but they are not part of the Skia adapter contract. A Skia build that does not test video may omit
FFmpeg and configure `-DCNA_ENABLE_VIDEO=OFF`.

`sharp-runtime` is a separate sibling checkout rather than a submodule. Starting from the directory
that will contain both projects:

```sh
git clone https://github.com/openeggbert/sharp-runtime.git
git clone https://github.com/openeggbert/cna.git cnaskia
cd cnaskia
git submodule update --init
```

An existing CNA checkout only needs the last command and a `../sharp-runtime/CMakeLists.txt`.

## 2. Build the pinned raster Skia artifact

Choose absolute paths outside the CNA source tree. These task-specific variables are used by the
remaining commands:

```sh
export CNA_SKIA_SRC=/absolute/path/to/skia
export CNA_SKIA_OUT=/absolute/path/to/skia-out/raster
export CMAKE_BUILD_PARALLEL_LEVEL=3
```

The CMake environment limit also applies to the one-time vendored SDL builds that occur during the
first CNA configure; their internal `cmake --build --parallel` command would otherwise use the
native tool's unrestricted default.

Clone, detach at the pinned revision, fetch the pinned PartitionAlloc dependency and GN, generate
the exact raster configuration, and build with at most three workers (this repository's own
build-hygiene rule; see
/rv/data/development/github.com/openeggbert/CLAUDE.md):

```sh
git clone https://skia.googlesource.com/skia.git "$CNA_SKIA_SRC"
git -C "$CNA_SKIA_SRC" checkout --detach ebf50520d720a1ce9d842d942d04c6c39c3fbc7b
git clone https://chromium.googlesource.com/chromium/src/base/allocator/partition_allocator.git \
  "$CNA_SKIA_SRC/third_party/externals/partition_alloc"
git -C "$CNA_SKIA_SRC/third_party/externals/partition_alloc" checkout --detach \
  b1d0141bcecfda2bfd108882d818fc5df70ae5c7
"$CNA_SKIA_SRC/bin/fetch-gn"
"$CNA_SKIA_SRC/bin/gn" gen "$CNA_SKIA_OUT" --root="$CNA_SKIA_SRC" --args='is_official_build=true is_debug=false cc="clang" cxx="clang++" skia_use_gl=false skia_enable_ganesh=false skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false skia_enable_pdf=false skia_use_freetype=false skia_use_fontconfig=false skia_use_libpng_decode=false skia_use_libjpeg_turbo_decode=false skia_use_libwebp_decode=false skia_use_wuffs=false skia_use_icu=false skia_enable_tools=false'
ninja -C "$CNA_SKIA_OUT" -j3 skia
```

The selected raster graph still links Skia's `raw_ptr` and allocator targets, so
`partition_alloc` is required even though every GPU and codec dependency above is disabled. The
revision is the exact entry from this Skia commit's `DEPS`; omitting it makes GN stop at
`src/partition_alloc/BUILD.gn` before producing a Ninja file. `--root` is explicit so the command
works from the CNA checkout as written instead of depending on the caller's current directory.

Verify the revision and complete archive set before configuring CNA:

```sh
test "$(git -C "$CNA_SKIA_SRC" rev-parse HEAD)" = \
  ebf50520d720a1ce9d842d942d04c6c39c3fbc7b
for CNA_SKIA_ARCHIVE in \
  libskia.a libskcms.a liballocator_base.a liballocator_core.a \
  liballocator_shim.a libraw_ptr.a
do
  test -f "$CNA_SKIA_OUT/$CNA_SKIA_ARCHIVE"
done
```

`is_official_build=true` produces the no-RTTI upstream ABI used by the validated archives. CNA's
sanitizer integration accounts for that boundary; do not change the GN ABI flags independently and
reuse the same output directory.

## 3. Configure and build CNA

From the CNA repository root:

```sh
cmake -S . -B build-skia -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SKIA \
  -DCNA_SKIA_ROOT="$CNA_SKIA_SRC" \
  -DCNA_SKIA_BUILD_DIR="$CNA_SKIA_OUT" \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_USE_CCACHE=OFF \
  -DCNA_TEST_DISPLAY=:0
cmake --build build-skia --parallel 3
```

Expected configure output includes `CNA: Using SKIA raster graphics renderer`. It must not mention
an EasyGL fallback. Wait for the build command to finish before starting CTest; a partially linked
executable is not a valid test artifact.

## 4. Run the tests

The suite is split by execution requirement:

- `Audit`: display-free source/plan consistency;
- `Raster`: display-free Skia surface and feasibility tests;
- `Display`: SDL/X11 presentation and public pixel tests;
- `Accelerated`: reserved for a future real Skia GPU mode and currently empty.

The display-free gate works directly:

```sh
ctest --test-dir build-skia -L 'Audit|Raster' --parallel 3 --output-on-failure
```

On a headless Linux host, bake Xvfb's actual display number into the CTest environment and run all
Skia labels in the same shell:

```sh
xvfb-run -a bash -c '
  set -e
  cmake -S . -B build-skia -DCNA_TEST_DISPLAY="$DISPLAY"
  ctest --test-dir build-skia -L Skia --parallel 3 --output-on-failure
'
```

On a real X11 desktop, configure the build with that session's display and run the same label:

```sh
cmake -S . -B build-skia -DCNA_TEST_DISPLAY="$DISPLAY"
ctest --test-dir build-skia -L Skia --parallel 3 --output-on-failure
```

If this is a persistent developer build, restore its conventional display after an Xvfb run:

```sh
cmake -S . -B build-skia -DCNA_TEST_DISPLAY=:0
```

The exact test count can grow. At the SKIA-112 checkpoint the expected inventory is 132 tests: 16
Raster, 113 Display, and three Audit. This command must report zero tests until an accelerated Skia
implementation is intentionally added:

```sh
ctest --test-dir build-skia -N -L Accelerated
```

## 5. Run the 2D demo and diagnostics

Use the real display selected during configuration, or run through Xvfb for a three-frame smoke:

```sh
DISPLAY=:0 SDL_VIDEODRIVER=x11 CNA_SKIA_STATE_TRACE=1 \
  cmake -E chdir build-skia ./cna_demo_2d
xvfb-run -a env SDL_VIDEODRIVER=x11 CNA_SKIA_STATE_TRACE=1 \
  cmake -E chdir build-skia ./cna_demo_2d --smoke 3
```

The working-directory change is required because the post-build step copies the demo's relative
`Content/` tree next to the executable.

Every constructed renderer prints one immutable startup line containing the pinned revision,
`surface=raster`, `colour=RGBA_8888/premultiplied`, `samples=0`, and
`anisotropic filtering=unsupported`. `CNA_SKIA_STATE_TRACE=1` additionally prints target identity,
size, blend, sampler, and scissor transitions; unset it (or set it to `0`) for normal runs.

For ownership diagnostics that should exclude the host GLX presenter, use SDL's dummy/software
presentation path on a display-independent fixture, as documented in
[`skia-sanitizer-validation.md`](skia-sanitizer-validation.md). The same document gives the
ASan+UBSan configure command and explains the narrow no-RTTI `vptr` exception.

## 6. Optional: build in Ganesh/OpenGL mode (experimental, SKIA-159-161)

This is not the release-gated configuration -- see
[Accelerated prerequisites and raster fallback policy](#accelerated-prerequisites-and-raster-fallback-policy)
below and [`skia-ganesh-artifact.md`](skia-ganesh-artifact.md) for the full contract. It exists to
build and exercise `SkiaGaneshContext`/`SkiaGaneshSurface` and their `Skia_Ganesh_*` tests; it does
not wire a presentable Ganesh backbuffer into `SkiaRenderer`/`IGraphicsRenderer` itself.

First build the separately pinned Ganesh GN artifact, reusing the exact same `$CNA_SKIA_SRC`
checkout from step 2 above (never re-clone Skia for this):

```sh
export CNA_SKIA_GANESH_OUT=/absolute/path/to/skia-out/ganesh
"$CNA_SKIA_SRC/bin/gn" gen "$CNA_SKIA_GANESH_OUT" --args='is_official_build=true is_debug=false cc="clang" cxx="clang++" skia_use_gl=true skia_enable_ganesh=true skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false skia_enable_pdf=false skia_use_freetype=false skia_use_fontconfig=false skia_use_libpng_decode=false skia_use_libjpeg_turbo_decode=false skia_use_libwebp_decode=false skia_use_wuffs=false skia_use_icu=false skia_enable_tools=false'
ninja -C "$CNA_SKIA_GANESH_OUT" -j3 skia
```

Then configure a new, stable, reusable build directory -- `cmake-build-skia-ganesh`, following this
project's own `cmake-build-<variant>/` convention, not a per-ticket one-off:

```sh
cmake -S . -B cmake-build-skia-ganesh -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SKIA \
  -DCNA_SKIA_MODE=GANESH \
  -DCNA_SKIA_ROOT="$CNA_SKIA_SRC" \
  -DCNA_SKIA_GANESH_BUILD_DIR="$CNA_SKIA_GANESH_OUT" \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_USE_CCACHE=ON \
  -DCNA_TEST_DISPLAY=:99
cmake --build cmake-build-skia-ganesh -j3
```

Expected configure output includes `CNA: Using SKIA renderer in Ganesh/OpenGL mode (experimental,
SKIA-159-161)`, distinct from step 3's `CNA: Using SKIA raster graphics renderer`. Unlike this
section's own earlier revision, `-DCNA_TEST_DISPLAY` does **not** need to be a real desktop display:
confirmed directly (SKIA-161) that Xvfb (`:99`/`:101`, the same displays step 4 already uses for the
raster suite) provides a real, if software-only (Mesa llvmpipe), GLX implementation, sufficient for
every Ganesh correctness check this renderer has -- prefer the existing Xvfb displays for this reason
(and to avoid disturbing a real desktop session); a real display also works if that is what is
available. `Skia_Ganesh_ModeConstruction`/`Skia_Ganesh_Backbuffer` and the 170+ raster-labeled tests
already present in this same build directory all run identically either way.

```sh
ctest --test-dir cmake-build-skia-ganesh -L Accelerated --output-on-failure
```

For ASan+UBSan coverage of the Ganesh path, configure a second stable directory the same way as
step 3's `cmake-build-skia-asan`, adding the same `CNA_SKIA_MODE`/`CNA_SKIA_GANESH_BUILD_DIR`
options as above:

```sh
cmake -S . -B cmake-build-skia-ganesh-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SKIA \
  -DCNA_SKIA_MODE=GANESH \
  -DCNA_SKIA_ROOT="$CNA_SKIA_SRC" \
  -DCNA_SKIA_GANESH_BUILD_DIR="$CNA_SKIA_GANESH_OUT" \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_USE_CCACHE=ON \
  -DCNA_SANITIZE=address,undefined \
  -DCNA_TEST_DISPLAY=:99
cmake --build cmake-build-skia-ganesh-asan -j3
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir cmake-build-skia-ganesh-asan -R '^Skia_' --output-on-failure
```

The `detect_leaks=0` matches [`skia-sanitizer-validation.md`](skia-sanitizer-validation.md)'s
already-documented host `libGLX_mesa.so.0` residual baseline -- not specific to Ganesh mode, the
same real GLX presenter every Display-labeled Skia test already opens in any build.

## Accelerated prerequisites and raster fallback policy

There is no accelerated Skia binary or runtime fallback wired into `SkiaRenderer` (the real
`IGraphicsRenderer` implementation) in this revision. The default `CNA_SKIA_MODE=RASTER` build's
pinned GN arguments disable every Skia GPU API, and `SKIA` construction always selects raster
regardless of what else is built. SDL may use a GPU only to upload/present the completed CPU image;
that does not change the Skia execution mode.

A separate, independently pinned Ganesh/OpenGL GN artifact and CMake target (`CNA::SkiaGanesh`,
SKIA-159) exist, selectable at CMake configure time via `-DCNA_SKIA_MODE=GANESH` (SKIA-160,
[`skia-ganesh-artifact.md`](skia-ganesh-artifact.md)), and a real, testable
`SkiaGaneshContext` proves a genuine `GrDirectContext` constructs (or fails transactionally) in that
mode. Neither is wired into `SkiaRenderer` itself, though -- there is still no presentable
Ganesh backbuffer, and ordinary `CNA_GRAPHICS_RENDERER=SKIA` builds default to `RASTER` and are
completely unaffected.

The accepted [`skia-surface-mode-adr.md`](skia-surface-mode-adr.md) selects raster for this release
and names Ganesh/OpenGL only as the first future candidate. Any successor accelerated plan must
reopen the SKIA-6 proof rather than treating its conditional current-release closure as GPU
evidence. Before such a path can be built or advertised it needs, at minimum:

1. a separately named pinned GN artifact with the selected GPU API enabled, plus construction-time
   mode selection and a mode-specific startup diagnostic (**done** -- SKIA-159/160; `CNA::SkiaGanesh`,
   `CNA_SKIA_MODE`, `SkiaGaneshContext`);
2. explicit SDL/native context or device ownership and framebuffer/surface interop;
3. a tested device-loss/reset and fallback contract; and
4. the CPU/GPU parity corpus listed in [`skia-verification-boundary.md`](skia-verification-boundary.md).

Until those gates close, missing Skia headers/archives or presenter construction is a hard error,
not permission to switch implementation. To choose a deliberate non-Skia fallback, use a separate
build directory, for example:

```sh
cmake -S . -B build-sdl-renderer -G Ninja \
  -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
  -DCNA_BUILD_TESTS=ON
cmake --build build-sdl-renderer --parallel 3
```

Never reuse one CMake build directory while comparing renderer selections.

## Common failures

| Symptom | Cause | Correction |
|---|---|---|
| `SKIA requires -DCNA_SKIA_ROOT=...` | Header root is unset or does not contain `include/core/SkSurface.h`. | Pass the absolute pinned source checkout. |
| `SKIA requires -DCNA_SKIA_BUILD_DIR=...` | GN output is unset or lacks `libskia.a`. | Pass the exact `raster` output directory, not its parent. |
| `build directory is incomplete; missing ...` | One of the six static archives was not produced or paths mix revisions. | Re-run the exact GN/Ninja commands and the archive loop above. |
| Link errors involving PartitionAlloc/SkSL | Incompatible Skia arguments/revision or archives copied incompletely. | Rebuild all six archives together; let `CNA::Skia` retain their CMake link group. |
| Link errors containing `typeinfo for SkCanvas` under UBSan | A no-RTTI Skia boundary was instrumented with `vptr` outside CNA's supported sanitizer targets. | Use `-DCNA_SANITIZE=address,undefined`; CNA disables only `vptr` on the adapter/fixtures. |
| Display tests cannot open X11 | `CNA_TEST_DISPLAY` was configured for a different server. | Reconfigure inside `xvfb-run` or with the real session's `$DISPLAY`, then rerun CTest. |
| `permission denied` while CTest starts a just-built executable | CTest was started before Ninja completed replacement/linking of all selected targets. | Let `cmake --build` return successfully, then rerun the test. |
| Windowed LSan reports allocations rooted in `libGLX_mesa.so.0` | Known host presenter baseline, not automatically a CNA/Skia leak. | Compare the one-presenter control and dummy/software isolation exactly as in the sanitizer report; do not add a broad suppression. |
