# programs.md — System Package Requirements (Debian 13 "trixie")

Everything needed to build, test, and debug this project (`cna_graphics` + the sibling
`sharp-runtime`/`easy-gl` repos) on a **fresh Debian 13** machine — desktop or headless. Compiled
by surveying `CMakeLists.txt` (`find_package`/`pkg_check_modules` calls), `cmake/ThirdPartySDL.cmake`
(SDL3 is built from a vendored git submodule, so its own *build* dependencies — X11, OpenGL, audio —
still need to come from the system), `CLAUDE.md`'s already-documented FFmpeg requirement, and what
was actually needed/used during real build+test+debug sessions in this repo.

Run the "Everything" block below on a brand-new machine and every backend/target in this repo
should build. The per-category sections underneath explain *why* each package is there, so you can
trim it down if you only need one backend.

---

## Everything (copy-paste)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential gcc g++ cmake ninja-build git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev \
  libgl1-mesa-dev libegl1-mesa-dev libglu1-mesa-dev mesa-common-dev \
  libasound2-dev libpulse-dev \
  libvulkan-dev vulkan-tools vulkan-validationlayers mesa-vulkan-drivers \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
  zlib1g-dev \
  xvfb mesa-utils \
  apitrace apitrace-tracers
```

That's the full desktop-Linux (EasyGL + Vulkan + Bgfx + SDL_Renderer) build+test+debug set. See
below for what each line is for, and what's optional.

---

## 1. Core build toolchain

```bash
sudo apt-get install -y build-essential gcc g++ cmake ninja-build git pkg-config
```

- **`build-essential`, `gcc`, `g++`** — this project requires a **C++23-capable compiler**
  (GCC 12+ or Clang 15+ per `README.md`). Verified working: GCC 14.2.0 (Debian 14.2.0-19).
- **`cmake`** — 3.20+ required (`cmake_minimum_required(VERSION 3.20)` in `CMakeLists.txt`).
  Verified working: 3.31.6.
- **`ninja-build`** — optional but recommended; CMake defaults to Make otherwise, both work.
- **`git`** — needed for `git submodule update --init --recursive` (vendored SDL3/SDL_image/
  SDL_mixer at `third_party/`) and to clone the sibling `../sharp-runtime` and `../easy-gl` repos.
- **`pkg-config`** — `CMakeLists.txt` calls `find_package(PkgConfig REQUIRED)` directly for the
  FFmpeg libraries (§4).

## 2. SDL3 build dependencies (X11 / OpenGL / audio)

```bash
sudo apt-get install -y \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev \
  libgl1-mesa-dev libegl1-mesa-dev libglu1-mesa-dev mesa-common-dev \
  libasound2-dev libpulse-dev
```

SDL3, SDL3_image, and SDL3_mixer are **built from vendored git submodules** (`third_party/SDL*`),
not installed as system packages (`README.md` §9 already covers this) — but building *that vendored
source* still needs the underlying platform dev headers SDL3 probes for at CMake-configure time.
This project always drives tests with `SDL_VIDEODRIVER=x11` (see `NEXT.md` §7), so the X11 dev
headers are the load-bearing ones; Wayland is not currently exercised by any test in this repo and
is deliberately omitted here (SDL3 will still build fine without Wayland dev headers — it just won't
offer that video driver).

- **X11 libs** (`libx11-dev` + `libxext-dev`/`libxrandr-dev`/`libxinerama-dev`/`libxcursor-dev`/
  `libxi-dev`/`libxfixes-dev`/`libxss-dev`) — SDL3's X11 video backend.
- **`libgl1-mesa-dev`, `libegl1-mesa-dev`, `libglu1-mesa-dev`, `mesa-common-dev`** — OpenGL/EGL
  headers. Needed by: SDL3's own GL context creation, the `EASYGL` backend (`../easy-gl`, GL ES
  3.2 via the `easy-gl` wrapper), and the `BGFX` backend (bgfx creates its own context via **EGL**
  on Linux — see `bgfx/src/glcontext_egl.cpp` — not GLX).
- **`libasound2-dev`, `libpulse-dev`** — SDL3's audio backends (ALSA, PulseAudio), used by
  `Microsoft::Xna::Framework::Audio`/`SDL3_mixer`.

## 3. Vulkan

```bash
sudo apt-get install -y libvulkan-dev vulkan-tools vulkan-validationlayers mesa-vulkan-drivers
```

- **`libvulkan-dev`** — `CMakeLists.txt` calls `find_package(Vulkan REQUIRED)` when
  `CNA_GRAPHICS_BACKEND=VULKAN`.
- **`vulkan-tools`** — `vulkaninfo`, useful for confirming what the machine actually exposes
  (physical devices, extensions) before debugging a Vulkan-backend test failure.
- **`vulkan-validationlayers`** — surfaces real API-misuse bugs immediately in Vulkan backend
  development (this project's Vulkan backend logs `[Vulkan Validation]` messages when they fire).
- **`mesa-vulkan-drivers`** — provides **lavapipe** (software Vulkan, `llvmpipe`-backed), the
  fallback `VK_PHYSICAL_DEVICE_TYPE_CPU` device used automatically on machines with no real Vulkan
  GPU or under a virtual display (`Xvfb`) with no GPU passthrough — this is what makes the Vulkan
  backend's tests runnable at all on a headless CI-style box.

## 4. FFmpeg (video decoding)

```bash
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

Already documented in `CLAUDE.md` ("System Dependencies (Linux)") — required for
`Microsoft::Xna::Framework::Media`'s `VideoPlayer`. Note: `libswscale-dev` is **not** required —
CNA implements YUV→RGBA conversion internally rather than depending on `libswscale` headers (per
`CLAUDE.md`); the runtime `libswscale8`/`libswscale9` shared library pulled in transitively by
`libavcodec`/`libavformat` is enough.

## 5. Networking / misc

```bash
sudo apt-get install -y zlib1g-dev
```

- **`zlib1g-dev`** — the sibling `../sharp-runtime` repo's own `CMakeLists.txt` calls
  `find_package(ZLIB REQUIRED)` directly.
- **ENet** (backing `Microsoft::Xna::Framework::Net`) is vendored directly at `third_party/enet` —
  no system package needed.

## 6. Headless testing environment

```bash
sudo apt-get install -y xvfb mesa-utils
```

- **`xvfb`** — this sandbox has no real X server on `:0` by default for automated test runs; every
  GPU/window-creating test binary in this repo's `ctest` suite runs against a virtual `Xvfb` display
  (conventionally `:99` in this project's own session notes — see `NEXT.md` §7's "Environment
  note"). Start it with e.g. `Xvfb :99 -screen 0 1280x1024x24 &` before running any
  `SDL_VIDEODRIVER=x11 DISPLAY=:99 ...` command.
- **`mesa-utils`** — provides `glxinfo` (`glxinfo -B` is the fastest way to check what GL version a
  given `DISPLAY` actually negotiates/supports — this was essential when root-causing why bgfx's
  OpenGL backend reports "OpenGL 2.1" even though the underlying driver supports much more).

> **Note on real GPU vs. software rendering**: if the target machine has a real GPU with proper
> kernel driver support (confirmed in this project's own sandbox: `glxinfo -B` on the *real* display
> shows a hardware-accelerated `radeonsi`/AMD device up to GL 4.6), an `Xvfb` virtual display will
> still typically fall back to **llvmpipe** (software Mesa) unless it's specifically configured with
> GPU passthrough (e.g. `Xvfb` + DRI3, or running tests against the real display instead). Both
> paths work for this project's tests; llvmpipe is just slower and has its own quirks (documented in
> `NEXT.md` §5 — e.g. bgfx's OpenGL backend negotiating only a legacy GL 2.1 context in this
> specific software-GL environment).

## 7. Debugging tools

```bash
sudo apt-get install -y apitrace apitrace-tracers
```

- **`apitrace`** (+ `apitrace-tracers`, the `LD_PRELOAD`-based OpenGL/EGL call interceptor) — traces
  the exact sequence of GL calls (with arguments) a test binary issues; used to debug a real,
  still-open bug (Task 952, `plan_graphics.md`) where a `RenderTargetCube` face's FBO silently stops
  producing color output the instant a depth texture is attached, with **no GL error at all**
  (confirmed via `MESA_DEBUG=1 LIBGL_DEBUG=verbose`, which also needs no extra package — both are
  Mesa runtime env vars, not separate tools). Works fully headless — no GUI needed
  (`apitrace-gui`/`qapitrace` is the optional Qt frontend, not required for `apitrace dump`).
- **RenderDoc** would also help here (`bgfx` already attempts `dlopen("librenderdoc.so")` at
  startup — confirmed via a harmless `BX WARN dlopen failed` log line whenever it's absent — so
  installing it would let bgfx auto-integrate for frame capture with zero extra configuration). It
  is **not available in the default Debian 13 apt repos** (`apt-cache search renderdoc` returns
  nothing) — would need a manually-downloaded `.deb`/AppImage from
  [renderdoc.org](https://renderdoc.org) if ever pursued.

## 8. Optional: Windows cross-compilation (MinGW-w64)

```bash
sudo apt-get install -y mingw-w64
```

Only needed for `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake` builds (see `README.md`
§9, "Build (Linux → Windows cross-compilation with MinGW-w64)"). Not needed for any Linux-native
backend.

## 9. Out of scope here: Android (NDK)

Android cross-compilation needs the Android NDK (not an apt package — a separate SDK/NDK download
and toolchain file), and is currently **blocked** by pre-existing build regressions in the sibling
`sharp-runtime` repo unrelated to system packages (`NEXT.md`/`plan_graphics.md` Task 920). Not
included in the list above; set up the NDK separately if/when that work resumes.

---

## After installing: initialize submodules and sibling repos

System packages alone aren't enough — this repo also needs:

```bash
# Vendored SDL3/SDL_image/SDL_mixer submodules
git submodule update --init --recursive

# Sibling repos (separate git checkouts, NOT submodules — must live next to this repo's own directory)
cd ..
git clone https://github.com/openeggbert/sharp-runtime.git
git clone https://github.com/openeggbert/easy-gl.git   # only needed for the EASYGL backend
```

See `README.md` §9 for the full build commands per backend once all of the above is in place.
