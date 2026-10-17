# CNA development environment — Debian 13

What a fresh Debian 13 ("trixie") machine, desktop or headless, needs to build CNA natively and
run its tests. The configure step is the authority this page is checked against: a missing
required package stops it with an error naming the package, and each optional one prints a
`CNA: … enabled` or `… disabled` line saying what it changed.

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
| `build-essential`, `cmake`, `ninja-build`, `git`, `pkg-config` | A C++23 compiler (trixie's GCC 14; Clang 15+ also works), CMake 3.20 or newer (trixie has 3.31), the Ninja generator the documented build trees use, and `pkg-config` for the optional probes below. |
| `ccache` | Optional but recommended. `CNA_USE_CCACHE=ON`, the default, uses it when it is found and builds without it otherwise. |
| `libcurl4-openssl-dev`, `nlohmann-json3-dev` | **Required.** GamerServices and Net, both on by default, use libcurl for TLS and WebSockets and nlohmann-json for the service protocol. Configure fails without them; `-DCNA_ENABLE_NET=OFF -DCNA_ENABLE_GAMER_SERVICES=OFF` removes both modules and both packages. |
| `libx11-dev` … `libxtst-dev` | SDL3 is built from the vendored submodule, but its configure looks for the X11 headers of its x11 video driver on the system. Once it finds `libx11-dev` it stops if any of these extension packages is missing; XTest is one of them. Xinerama is no longer used. |
| `libgl-dev`, `libegl-dev` | The OpenGL and EGL headers SDL3 probes for its GL context support, which the GL renderers use. Without them SDL3 silently builds without that support. meta-gl carries its own GLES headers. The older names `libgl1-mesa-dev` and `libegl1-mesa-dev` that CI uses install the same headers. |
| `libasound2-dev`, `libpulse-dev` | SDL3's ALSA and PulseAudio output backends. `libasound2-dev` is also what `CNA_AUDIO_PLATFORM=ALSA` needs ([`audio-alsa.md`](audio-alsa.md)). Tests use SDL's dummy audio driver either way. |
| `weston`, `xwayland`, `mesa-vulkan-drivers` | [`tools/platform/run_gpu_tests_private.sh`](../tools/platform/run_gpu_tests_private.sh) is the standard way to run GPU and window tests, never on the live desktop. It starts a headless Weston and a private rootful Xwayland, which has DRI3 and so presents Vulkan on the real GPU. Without Weston or Xwayland it exits 77 (skip). `mesa-vulkan-drivers` is the Vulkan driver (RADV, Intel, or lavapipe on a machine with no usable GPU) that `SDL_GPU` and `WEBGPU` run on; neither needs Vulkan headers to build. |
| `xvfb`, `x11-utils` | Xvfb serves the X11-protocol tests (`CnaPlatformSdl3X11Tests`, through `tools/platform/x11_test_server.sh`) and the `xvfb-run` harnesses under `scripts/`. It is **not** a substitute for the private runner: it has no DRI3, so Vulkan cannot present on it, and its OpenGL is Mesa's software llvmpipe. `x11-utils` gives the runner `xdpyinfo` for its DRI3 check. |
| `python3`, `python3-pil` | Configure works without Python, but with it the default suite registers its Python checks, and the platform boundary gates are Python scripts ([`platform-abstraction.md`](platform-abstraction.md)). `EasyGL_XnaLineCoverage` diffs images with Pillow. |
| `libfreetype-dev`, `doxygen` | The default suite checks these rather than skipping them. FreeType is the build-time `.spritefont` pipeline (`CNA_ENABLE_FONT_PIPELINE=AUTO`), whose link boundary `CnaXnbDependencyBoundary` verifies. The C API coverage gates parse headers with Doxygen 1.9.x, which is the version trixie ships. |

The vendored SDL3 is built once per configuration into a `.sdl-prebuilt-<key>/` directory in the
source tree and shared by every build tree. Installing an SDL prerequisite later does not rebuild
it, so install the SDL packages before the first configure. If you add one afterwards, remove the
cache directory the configure output names (other build trees use it too) and reconfigure. The
Wayland packages below are the exception, because they select a different key.

## Sibling checkouts and submodules

```bash
cd ..   # the directory that holds the cna checkout
git clone --branch next https://github.com/libcna/sharp-runtime.git
git clone https://github.com/libcna/easy-gl.git
git clone https://github.com/libcna/meta-gl.git
cd cna
git submodule update --init
```

- The siblings are separate checkouts consumed with `add_subdirectory`, not submodules.
  `sharp-runtime` is required by every configuration. `easy-gl` is required by the GL renderers
  (`OPENGLES3`, the Linux default, `OPENGL33` and `WEBGL2`), and it adds `meta-gl` from its own
  `../meta-gl`. `CNA_SHARP_RUNTIME_ROOT` and `CNA_EASYGL_ROOT` point a build, such as a worktree,
  at checkouts elsewhere; meta-gl must still sit next to that easy-gl.
- CNA `next` builds against sharp-runtime `next` and the default `develop` branch of easy-gl and
  meta-gl. CI makes the same pairing (`scripts/ci/sibling_branch.sh`). A CNA release tag needs the
  siblings' release tags instead: configure refuses a sibling that does not declare a version
  compatible with the one `cmake/DependencyVersions.cmake` requires
  ([`releasing.md`](releasing.md)).
- `git submodule update --init` fills `third_party/SDL`, `third_party/SDL_mixer`,
  `third_party/draco` and `vendor/googletest`; ENet, cgltf, stb and dr_libs are committed. Do not
  add `--recursive`. It also clones SDL_mixer's codec submodules and Draco's test dependencies,
  and CNA's configuration builds none of them.
- Some renderer dependencies are downloaded at configure time instead: the pinned wgpu-native
  release for `WEBGPU` (`CNA_WEBGPU_ROOT` for an offline build), FNA3D and MojoShader for `FNA3D`
  and the `*_COMPILED_EFFECTS` options (`FETCHCONTENT_SOURCE_DIR_FNA3D`), and SDL_shadercross for
  `SDL_GPU` on Windows and Apple targets.

## Optional native features

```bash
sudo apt-get install -y \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
  libzstd-dev libopus-dev zlib1g-dev libshaderc1 libudev-dev libdbus-1-dev mold
```

| Packages | Enables | Switch (default) |
|---|---|---|
| `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswresample-dev` | `VideoPlayer` playback and the build-time MP3/WMA/WMV importers. `libswscale-dev` is not needed; CNA converts YUV itself ([`video-backend.md`](video-backend.md)). | `CNA_ENABLE_VIDEO`, `CNA_ENABLE_MEDIA_PIPELINE` (`AUTO`) |
| `libzstd-dev` | Zstandard-compressed `.cnb` chunks. | `CNA_CNB_ZSTD` (`AUTO`) |
| `libopus-dev` | Network voice (`NetworkGamer.HasVoice`). | `CNA_ENABLE_VOICE` (`AUTO`) |
| `zlib1g-dev` | Compressed arrays of binary FBX files in the build-time content pipeline. Nothing else in the default configuration needs zlib. | probed |
| `libshaderc1` | `SDL_GPU`'s `ShaderEffect` (GLSL compiled at run time). Without it `CustomEffects` reports false. The runtime library is enough. | probed |
| `libudev-dev`, `libdbus-1-dev` | SDL3's udev device hotplug and its D-Bus integration (screensaver inhibition, IME, portals). SDL3 builds without them. | SDL3's own configure |
| `mold` (or `lld`) | Faster links. `CNA_LINKER=AUTO` tries mold first, then lld ([`build-performance.md`](build-performance.md)). | `CNA_LINKER` (`AUTO`) |

Draco comes from the pinned submodule. `libdraco-dev` (trixie has 1.5.6, CNA pins 1.5.7) is used
only with `-DCNA_USE_SYSTEM_DRACO=ON`.

## Wayland

```bash
sudo apt-get install -y libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev
```

Without these, the vendored SDL3 has no wayland video driver, and a Wayland desktop runs CNA
programs through Xwayland. When they are present, configure selects a separate `-wayland` SDL
cache key and builds SDL3 with the driver, without disturbing an existing X11-only cache.
`libdecor-0-dev` draws window decorations on compositors that do not provide them, such as GNOME.
`CnaPlatformSdl3WaylandTests` runs on a private headless Weston, the compositor the GPU runner
uses.

## Windows cross-development

```bash
sudo apt-get install -y mingw-w64 wine dxvk dxvk-wine64
```

- `mingw-w64` provides the compilers for `cmake/toolchains/mingw-w64.cmake`, which builds CNA for
  Windows; `DIRECTX9` configures only there, and `SDL_GPU` is the Windows default. The resulting
  `.exe`s run under Wine. Trixie's Wine has a single `wine` command for 32- and 64-bit
  executables; there is no `wine64` command.
- GamerServices and Net need a Windows libcurl, which Debian does not package. Point
  `CURL_INCLUDE_DIR` and `CURL_LIBRARY` at a MinGW build of curl, or configure
  `-DCNA_ENABLE_NET=OFF -DCNA_ENABLE_GAMER_SERVICES=OFF` as the Windows CI does. nlohmann-json comes
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
- `SDL_GPU`'s Direct3D 12 driver runs under Wine through vkd3d-proton, which Debian does not
  package. It uses its own prefix (`scripts/run-wine-vkd3d.sh`).

## Diagnostics and debugging tools

```bash
sudo apt-get install -y mesa-utils vulkan-tools vulkan-validationlayers apitrace apitrace-tracers
```

- `mesa-utils`: `glxinfo -B` and `eglinfo` show which GL driver and version a display provides —
  llvmpipe on Xvfb, the real GPU on the private runner's Xwayland.
- `vulkan-tools`: `vulkaninfo --summary` lists the Vulkan devices `SDL_GPU` and `WEBGPU` can use.
- `vulkan-validationlayers`: the Khronos validation layer, enabled through the loader with
  `VK_LOADER_LAYERS_ENABLE='*validation'`.
- `apitrace`: `apitrace trace` and `apitrace dump` record and print every GL/EGL call a program
  makes, headless. The Qt viewer (`apitrace-gui`) is optional.
- RenderDoc is not packaged by Debian. The self-contained Linux tarball from
  [renderdoc.org](https://renderdoc.org) needs no installation. `renderdoccmd capture <program>`
  injects it into a program, and `renderdoccmd convert` turns a capture into a structured XML
  dump of its calls without the GUI.
- Mesa's own `MESA_DEBUG=1` and `LIBGL_DEBUG=verbose` environment variables need no package.

## Other targets

- Browser builds (`WEBGL2`, and `WEBGPU` in the browser) use the Emscripten SDK rather than apt:
  [`webgl2-renderer.md`](webgl2-renderer.md),
  [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md).
- Android uses the Android NDK: [`devices-build.md`](devices-build.md),
  [`android-graphics-limitations.md`](android-graphics-limitations.md).
- macOS and iOS: [`apple-platforms.md`](apple-platforms.md).
- A windowless SDL-free build (`-DCNA_ENABLE_SDL=OFF`, with the `HEADLESS`, `SOFTWARE` or `STUB`
  renderer and `NULL` or `ALSA` audio) needs none of the SDL3 packages:
  [`platform-abstraction.md`](platform-abstraction.md).
