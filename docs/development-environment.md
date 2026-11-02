# CNA development environment — Debian 13

What a fresh Debian 13 ("trixie") machine, desktop or headless, needs to build CNA natively and
run its tests. This is a host/configuration recipe, reconciled on 2026-11-01, not a claim that
every target or the entire test matrix runs after one package install. Current CMake probes and
feature messages define the selected dependency set; optional missing tools can disable/skip
checks, while required dependencies fail configuration.

Install the baseline, fetch the sibling checkouts and submodules, then add only the optional
groups you need. The build commands themselves are in the repository
[`README.md`](../README.md), *Getting Started*.

## Baseline

```bash
sudo apt-get update

# Build: what the default configuration needs (OPENGLES3 renderer, SDL3 platform and audio,
# Net and GamerServices on)
sudo apt-get install -y \
  build-essential cmake ninja-build git pkg-config ccache \
  libcurl4-openssl-dev nlohmann-json3-dev \
  libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev \
  libxtst-dev libgl-dev libegl-dev libasound2-dev libpulse-dev

# Tests: the private GPU display and what the default ctest suite checks
sudo apt-get install -y \
  weston xwayland xvfb x11-utils mesa-vulkan-drivers \
  python3 python3-pil libfreetype-dev doxygen
```

| Packages | Why |
|---|---|
| `build-essential`, `cmake`, `ninja-build`, `git`, `pkg-config` | A compiler and standard library with the C++23 features CNA/Sharp Runtime use. The audited host uses GCC 14; do not infer complete library support from a Clang version alone. Root minimum CMake is 3.20, but the native ELF shared runtime requires 3.30+; older CMake needs CNA_SHARED_LIBRARY=OFF. Trixie's [CMake package](https://packages.debian.org/trixie/cmake) is 3.31.6. Ninja is the documented tree generator. |
| `ccache` | Optional but recommended. `CNA_USE_CCACHE=ON`, the default, uses it when it is found and builds without it otherwise. |
| `libcurl4-openssl-dev`, `nlohmann-json3-dev` | Required by the default native GamerServices/Net configuration for service TLS/WebSockets and JSON. To omit those modules/dependencies, use CNA_ENABLE_NET=OFF and CNA_ENABLE_GAMER_SERVICES=OFF; keep CNA_BUILD_C_API=OFF because the C adapter currently requires them. |
| `libx11-dev` … `libxtst-dev` | SDL3 is built from the vendored submodule, but its configure looks for the X11 headers of its x11 video driver on the system. Once it finds `libx11-dev` it stops if any of these extension packages is missing; XTest is one of them. Xinerama is no longer used. |
| `libgl-dev`, `libegl-dev` | The OpenGL and EGL headers SDL3 probes for its GL context support, which the GL renderers use. Without them SDL3 silently builds without that support. meta-gl carries its own GLES headers. The older names `libgl1-mesa-dev` and `libegl1-mesa-dev` that CI uses install the same headers. |
| `libasound2-dev`, `libpulse-dev` | SDL3's ALSA and PulseAudio output backends. libasound2-dev also enables CNA_AUDIO_PLATFORM=ALSA ([audio-alsa.md](audio-alsa.md)). SDL tests normally use dummy audio; SDL_AUDIODRIVER does not mute/configure CNA's separate ALSA mixer. |
| `weston`, `xwayland`, `mesa-vulkan-drivers` | [`tools/platform/run_gpu_tests_private.sh`](../tools/platform/run_gpu_tests_private.sh) is the standard way to run GPU and window tests, never on the live desktop. It starts a headless Weston and a private rootful Xwayland, which has DRI3 and so presents Vulkan on the real GPU. Without Weston or Xwayland it exits 77 (skip). `mesa-vulkan-drivers` is the Vulkan driver (RADV, Intel, or lavapipe on a machine with no usable GPU) that `SDLGPU` and `WEBGPU` run on; neither needs Vulkan headers to build. |
| `xvfb`, `x11-utils` | Xvfb serves the X11-protocol tests (`CnaPlatformSdl3X11Tests`, through `tools/platform/x11_test_server.sh`) and the `xvfb-run` harnesses under `scripts/`. It is **not** a substitute for the private runner: it has no DRI3, so Vulkan cannot present on it, and its OpenGL is Mesa's software llvmpipe. `x11-utils` gives the runner `xdpyinfo` for its DRI3 check. |
| `python3`, `python3-pil` | Configure works without Python, but with it the default suite registers its Python checks, and the platform boundary gates are Python scripts ([`platform-abstraction.md`](platform-abstraction.md)). `EasyGL_XnaLineCoverage` diffs images with Pillow. |
| `libfreetype-dev`, `doxygen` | FreeType enables the optional build-time .spritefont pipeline (CNA_ENABLE_FONT_PIPELINE=AUTO); CnaXnbDependencyBoundary checks its isolation. C API coverage pins use Doxygen 1.9.x; point CNA_C_API_DOXYGEN at that version when needed. [Trixie ships 1.9.8](https://packages.debian.org/trixie/doxygen). Availability/skip behavior follows each registered gate. |

The vendored SDL3 is cached by target/capability key in a `.sdl-prebuilt-<key>/` directory in the
source tree and shared by every build tree. Installing an SDL prerequisite later does not rebuild
it, so install the SDL packages before the first configure. If you add one afterwards, remove the
cache directory the configure output names (other build trees use it too) and reconfigure. The
Wayland packages below are the exception, because they select a different key.

Each install records the manifest it was built from (source tree, CNA's patches, configure
arguments, toolchain file and the settings it decides -- read by evaluating the toolchain in a
`cmake -P` process; a toolchain that cannot be evaluated outside a project draws a configure
warning, and edits to the files it includes then do not rebuild SDL). A valid install is never
rebuilt in place for a different manifest, because a tree that accepted it may be compiling or
linking against it at that moment: a tree whose manifest differs from what the root holds -- other
configure arguments, another toolchain or compiler, a new SDL pin -- builds into
`<root>/manifests/<key>` instead and shares that directory only with trees of the same manifest
(`CNA_SDL_PREBUILT_DIR` in its cache names the directory in use). The root itself is (re)built
only when it holds no complete, stamped build. Every tree also watches the record of the install it
uses, so an interrupted rebuild there makes its next build reconfigure. Directories under
`manifests/` that no tree uses any more, after a pin or compiler change, can be deleted by hand.

## Sibling checkouts and submodules

```bash
# Fresh integration checkout; sibling directories must not already exist.
git clone --branch next https://github.com/libcna/cna.git
cd cna
scripts/ci/clone_siblings.sh next sharp-runtime easy-gl meta-gl
git submodule update --init
```

- The siblings are separate checkouts consumed with `add_subdirectory`, not submodules.
  `sharp-runtime` is required by every configuration. `easy-gl` is required by the GL renderers
  (`OPENGLES3`, the Linux default, `OPENGL33` and `WEBGL2`), and it adds `meta-gl` from its own
  `../meta-gl`. `CNA_SHARP_RUNTIME_ROOT` and `CNA_EASYGL_ROOT` point a build, such as a worktree,
  at checkouts elsewhere; meta-gl must still sit next to that easy-gl.
- CI tries candidate branches supplied by the workflow, then next, then develop, independently
  for each sibling (`scripts/ci/sibling_branch.sh`). The fresh recipe uses that resolver rather
  than assuming a permanent default branch for easy-gl/meta-gl. Update existing checkouts
  deliberately instead of rerunning the clone helper. A release uses the recorded sibling pins;
  configure also checks the declared version is
  compatible with the one `cmake/DependencyVersions.cmake` requires
  ([`releasing.md`](releasing.md)).
  A matching version number alone does not prove that an older checkout supplies every component
  target/header required by the current modular SDK.
- `git submodule update --init` fills `third_party/SDL`, `third_party/SDL_mixer`,
  `third_party/draco`, `vendor/googletest` and the avatar catalogs in
  `modules/gamer-services/assets/avatars` ([cna-avatar-assets](https://github.com/libcna/cna-avatar-assets));
  ENet, cgltf, stb and dr_libs are committed. Recursive submodule updates also clone SDL_mixer's codec
  submodules and Draco's test dependencies, which CNA's configuration builds none of, so the plain
  `git submodule update --init` suffices for the normal CNA configuration. Upstream test/codec
  configurations can require additional nested sources.
- Some renderer dependencies are downloaded at configure time instead: the pinned wgpu-native
  release for `WEBGPU` (`CNA_WEBGPU_ROOT` for an offline build), FNA3D and MojoShader -- CNA's
  forks libcna/FNA3D and libcna/mojoshader, see `docs/fna3d-renderer.md` -- for `FNA3D` and the
  `*_COMPILED_EFFECTS` options (`FETCHCONTENT_SOURCE_DIR_FNA3D`), and SDL_shadercross for
  `SDLGPU` on Windows and Apple targets.
- Patch/provenance rules are explicit in the dependency integrations. Current SDL patch selection
  is in cmake/ThirdPartySDL.cmake, shadercross integration in cmake/ThirdPartySDLShaderCross.cmake,
  and CNA's FNA3D/MojoShader fixes are commits on the libcna forks. Consult those files and the
  recorded pins rather than inferring an unchanged upstream tree or a fixed patch count from this
  package guide. SDL patches are applied to build copies, not silently to the tracked submodule.
- Configure checks that the sources it builds are the pinned ones, read-only: `third_party/SDL` and
  `third_party/SDL_mixer` against the revisions `cmake/VendoredSources.cmake` records -- a stale
  submodule, or a source archive without `.git`, by its tree id -- with
  `CNA_VENDORED_SOURCE_CHECK=ON|WARN|OFF`; and a FetchContent-populated FNA3D checkout and its
  MojoShader submodule against the pin, refused when they drift unless
  `CNA_FNA3D_ALLOW_SOURCE_DRIFT=ON`. A `FETCHCONTENT_SOURCE_DIR_FNA3D` checkout is only reported.
  An old tree whose `_deps/fna3d-src` still holds the former configure-time patches stops there:
  delete `_deps/fna3d-*` and configure again.
- The forks' own regression suites live in their `tests/cna` (`-DMOJOSHADER_CNA_TESTS=ON`,
  `-DFNA3D_CNA_TESTS=ON`; FNA3D's open a window, so run them through
  `tools/platform/run_gpu_tests_private.sh --exec ctest --test-dir <build>`). Run both before moving
  the FNA3D pin.

## Optional native features

```bash
sudo apt-get install -y \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
  libzstd-dev libopus-dev zlib1g-dev libshaderc1 libudev-dev libdbus-1-dev mold
```

| Packages | Enables | Switch (default) |
|---|---|---|
| `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswresample-dev` | VideoPlayer decoding and build-time MP3/WMA/WMV importers. VIDEO controls shared detection: VIDEO=OFF also disables AUTO media importing; MEDIA_PIPELINE=ON then fails. No libswscale headers are required ([video-backend.md](video-backend.md)). | CNA_ENABLE_VIDEO, CNA_ENABLE_MEDIA_PIPELINE (AUTO) |
| `libzstd-dev` | Zstandard-compressed `.cnb` chunks. | `CNA_CNB_ZSTD` (`AUTO`) |
| `libopus-dev` | Network voice (`NetworkGamer.HasVoice`). | `CNA_ENABLE_VOICE` (`AUTO`) |
| `zlib1g-dev` | Compressed arrays of binary FBX files in the build-time content pipeline. Nothing else in the default configuration needs zlib. | probed |
| `libshaderc1` | `SDLGPU`'s `ShaderEffect` (GLSL compiled at run time). Without it `CustomEffects` reports false. The runtime library is enough. | probed |
| `libudev-dev`, `libdbus-1-dev` | SDL3's udev device hotplug and its D-Bus integration (screensaver inhibition, IME, portals). SDL3 builds without them. | SDL3's own configure |
| `mold` (or `lld`) | Faster links. `CNA_LINKER=AUTO` tries mold first, then lld ([`build-performance.md`](build-performance.md)). | `CNA_LINKER` (`AUTO`) |

Draco comes from the pinned submodule. [Trixie's libdraco-dev](https://packages.debian.org/trixie/libdraco-dev)
is 1.5.6 while CNA pins 1.5.7; that system package is used
only with `-DCNA_USE_SYSTEM_DRACO=ON`.

## Wayland

```bash
sudo apt-get install -y libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev
```

Without the Wayland build prerequisites, the vendored SDL3 has no wayland video driver; a Wayland
desktop can use Xwayland when it is available. The native capability probe checks pkg-config,
wayland-scanner, Wayland/EGL and xkbcommon. When it succeeds, CMake selects a separate `-wayland`
SDL cache key and builds the driver without disturbing an existing X11-only cache.
`libdecor-0-dev` is optional decoration support for compositors that do not supply server-side
decorations; it is not itself the native-driver capability probe.
`CnaPlatformSdl3WaylandTests` runs on a private headless Weston, the compositor the GPU runner
uses.

## Windows cross-development

```bash
sudo apt-get install -y mingw-w64 wine dxvk dxvk-wine64
```

- `mingw-w64` provides the compilers for `cmake/toolchains/mingw-w64.cmake`, which builds CNA for
  Windows; `DIRECTX9` configures only there, and `SDLGPU` is the Windows default. The resulting
  `.exe`s run under Wine. Trixie's Wine has a single `wine` command for 32- and 64-bit
  executables; there is no `wine64` command.
- GamerServices and Net need a Windows libcurl, which Debian does not package. Point
  `CURL_INCLUDE_DIR` and `CURL_LIBRARY` at a MinGW build of curl, or configure
  `-DCNA_ENABLE_NET=OFF -DCNA_ENABLE_GAMER_SERVICES=OFF -DCNA_BUILD_C_API=OFF` as a dependency-free
  service profile. nlohmann-json comes
  from the host's headers.
- `DIRECTX9` tests run through `scripts/run-wine-dxvk9.sh` on DXVK, which fails a run (exit 3)
  that silently fell back to Wine's own WineD3D. Each prefix is created with
  `WINEPREFIX=<prefix> wineboot --init` followed by `WINEPREFIX=<prefix> dxvk-setup install`.
  CNA uses three of them:

  | Prefix | Holds | Used by |
  |---|---|---|
  | `~/.wine-cna-d3d11` (a historical name) | DXVK | The default `CNA_D3D9_WINEPREFIX` of `run-wine-dxvk9.sh`, so most `DIRECTX9` tests. |
  | `~/.wine-cna-d3d9-spike` | DXVK and the real Microsoft `d3dcompiler_47.dll`, because Wine's builtin cannot compile SM2/SM3 shaders | The shader-compiling tests pinned to it, and `scripts/verify_d3d_shaders_reproducible.sh`. |
  | `~/.wine-cna-xna40` | A 32-bit prefix with .NET Framework 4.0, the real XNA 4.0 runtime and 32-bit DXVK | Only the genuine-XNA references: regenerating the oracle images (`D3D9_XNA_Diff` does not need it) and the XNA interop harness. |

  The two Microsoft-component prefixes also need `winetricks` (Debian contrib). The XNA prefix needs
  `wine32:i386` and `dxvk-wine32:i386`, after `sudo dpkg --add-architecture i386`. The interop
  harness compiles with `mcs` (`mono-mcs`). What each prefix contains and how it was built:
  [`../spikes/dx9-spike/README.md`](../spikes/dx9-spike/README.md),
  [`../tools/xna-oracle/README.md`](../tools/xna-oracle/README.md) and
  [`../tests/interop/xna40/README.md`](../tests/interop/xna40/README.md). How to build, run and
  test the renderer: [`directx9-renderer.md`](directx9-renderer.md).
- `SDLGPU`'s Direct3D 12 driver runs under Wine through vkd3d-proton, which Debian does not
  package. It uses its own prefix (`scripts/run-wine-vkd3d.sh`).

## Diagnostics and debugging tools

```bash
sudo apt-get install -y mesa-utils vulkan-tools vulkan-validationlayers apitrace apitrace-tracers
```

- `mesa-utils`: `glxinfo -B` and `eglinfo` show which GL driver and version a display provides —
  llvmpipe on Xvfb, the real GPU on the private runner's Xwayland.
- `vulkan-tools`: `vulkaninfo --summary` lists the Vulkan devices `SDLGPU` and `WEBGPU` can use.
- `vulkan-validationlayers`: the Khronos validation layer, enabled through the loader with
  `VK_LOADER_LAYERS_ENABLE='*validation'`.
- `apitrace`: `apitrace trace` and `apitrace dump` record and print every GL/EGL call a program
  makes, headless. The Qt viewer (`apitrace-gui`) is optional.
- RenderDoc is optional; use the [upstream project](https://renderdoc.org) for distribution and
  version-specific capture/export instructions. This audit host has no RenderDoc package candidate
  in its configured trixie index; that is not an assertion about every Debian suite/repository.
  Run GPU capture commands through the private runner as well.
- Mesa's own `MESA_DEBUG=1` and `LIBGL_DEBUG=verbose` environment variables need no package.

## Other targets

- Browser builds (`WEBGL2`, and `WEBGPU` in the browser) use the Emscripten SDK rather than apt:
  [`webgl2-renderer.md`](webgl2-renderer.md),
  [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md).
- Android uses the Android NDK: [`devices-build.md`](devices-build.md),
  [`android-graphics-limitations.md`](android-graphics-limitations.md).
- macOS and iOS: [`apple-platforms.md`](apple-platforms.md).
- Native Windows development with Visual Studio, VS Code or the MSVC command line:
  [`windows-visual-studio.md`](windows-visual-studio.md).
- A windowless SDL-free build (`-DCNA_ENABLE_SDL=OFF`, with the `HEADLESS`, `SOFTWARE` or `STUB`
  renderer and `NULL` or `ALSA` audio) needs none of the SDL3 packages:
  [`platform-abstraction.md`](platform-abstraction.md).

## Verification boundary

The 2026-11-01 audit checks current CMake/CI scripts, all 40 package names in the installation
blocks against the host's apt index, and Debian's CMake/Doxygen/Draco/DXVK package metadata.
Existing native builds/probes run during the documentation campaign use this host; no clean
machine installation, package upgrade, new Wine-prefix setup or cross-toolchain provisioning was
performed. Respect the eight-job build limit and use the private runner for every GPU/window test.
All nine focused dependency/cache/patch/header/deployment configuration checks pass in the existing
multi tree through the private runner; this does not replace a fresh-system installation test.
