# GPU test isolation and test-registration hygiene

A small infrastructure workstream, opened by the owner right after `plans/plan_vulkan_parity.md` was
integrated (`47d8c4631`). Two lessons from that workstream, both outside Vulkan itself:

* a test must provably **reach its assertions** — 103 Vulkan example tests had aborted before their
  first check for two days, and nobody noticed because an abort reads as "known red";
* tests must run **isolated** — a plain `ctest` here opened hundreds of GPU windows on the owner's live
  desktop, because the build trees baked `DISPLAY=:0` into ~990 test registrations.

Task IDs: `GTI-0001`, `GTI-0002`, … . No renderer behaviour changes here.

## Status

| ID | Task | Status |
|---|---|---|
| GTI-0001 | `CNA_TEST_DISPLAY`: empty by default, the live desktop opt-in, existing trees migrated | ✅ |
| GTI-0002 | `tools/platform/run_gpu_tests_private.sh`: the standard private GPU test runner | ✅ |
| GTI-0003 | The SOFTWARE-213 HiDef fix carried to WebGPU (32 tests) and SDL_GPU (3) | ✅ |
| GTI-0004 | Seven Vulkan tests failed, not skipped, in builds that run without validation | ✅ |
| GTI-0005 | Correction: two VKPAR "zero validation messages" claims were measured with validation off | ✅ |
| GTI-0006 | The Wayland half of the live-desktop hole: libwayland's implicit `wayland-0`; a deterministic policy test and a tree-level isolation check | ✅ |
| GTI-0007 | A generic guard for tests that die on, or skip because of, a graphics-profile refusal | ✅ |
| GTI-0008 | `run_gpu_tests_private.sh --exec`: one command in the same private environment; cleanup and exit-status evidence | ✅ |
| GTI-0009 | Eight shared render-target, lifetime and winding tests passed with their back-buffer legs skipped on Reach | ✅ |
| GTI-0010 | The WebGPU and SDL_GPU tests GTI-0003 only compiled, run on real hardware | ✅ (3 real failures recorded, not fixed) |

---

## GTI-0001 — `CNA_TEST_DISPLAY`

**Before.** `set(CNA_TEST_DISPLAY ":0" CACHE STRING ...)` in the root `CMakeLists.txt`, and ~990
registrations of the form `ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}"`. ctest applies
a test's `ENVIRONMENT` over the caller's, so `DISPLAY=:9 ctest` still ran every renderer test on `:0` —
the Xwayland of the owner's GNOME session.

**Now** (`cmake/TestDisplayPolicy.cmake`, included from the root):

* `CNA_TEST_DISPLAY` defaults to **empty**. At the end of configuration a deferred pass walks every
  directory's `TESTS` and removes the resulting empty `DISPLAY=` entry, so the test **inherits the
  caller's** `DISPLAY`. Where tests appear is decided by how they are launched, not by the build tree.
  (It needs `get_test_property`/`set_tests_properties … DIRECTORY`, CMake ≥ 3.28; older CMake warns
  and leaves the empty `DISPLAY`, which cannot reach a desktop.)
* **The live desktop is opt-in.** A value naming `:0` is honoured only with
  `-DCNA_TEST_ALLOW_LIVE_DISPLAY=ON`; otherwise it is reset to empty with a message saying how to opt
  in. That is also the migration: every existing tree's cache holds the old `:0` default and is reset
  on its next configure. Other values (an Xvfb `:99`, say) are honoured as before.
* Tests discovered at test time (`gtest_discover_tests`, the ~9 400 `CnaTests` cases) never carried a
  `DISPLAY` and are unaffected.

**Measured on `cmake-build-vulkan`:** reconfigured, the cache went from `:0` to empty with the
migration message; **374** renderer tests had their `DISPLAY` entry removed and **no** registered test
sets `DISPLAY` any more (`ctest --show-only=json-v1`).

## GTI-0002 — the private GPU test runner

`tools/platform/run_gpu_tests_private.sh <build-dir> [ctest args…]` runs plain `ctest` inside
`tools/platform/wayland_test_server.sh` (headless Weston, GL renderer, private `XDG_RUNTIME_DIR`, no
session bus, `DISPLAY` unset) plus a **rootful Xwayland** that picks its own free display number
(`-displayfd`). That display has DRI3, so Vulkan presents on the real GPU — which Xvfb cannot do. The
native Wayland backend's tests get the private compositor through `WAYLAND_DISPLAY`. It **refuses** a
build tree whose cache still forces a `DISPLAY` (ctest would override the private one) and exits 77
when no compositor or Xwayland exists.

**Measured:** the whole `^Vulkan_` suite through it — `DISPLAY=:2 (private Xwayland)`, 319 passed /
51 failed in 49 s, exactly the failure set of the previous private run (no new failure, none fixed),
every test process on the private display. Pointed at `cmake-build-multi`, whose cache still holds
`:0`, it refuses with the reconfigure instruction.

Known limit, recorded rather than hidden: the Wine-based XNA interop/differential tests hang inside
the private runtime directory; run them separately (their harness pins Xvfb `:99`).

## GTI-0003 — the dead WebGPU and SDL_GPU tests

`SOFTWARE-213` made `GetBackBufferData` throw under the Reach profile (XNA's rule) and the renderer example
suites were not carried across; `plans/plan_vulkan_parity.md` VKPAR-0006 fixed 104 Vulkan tests and
measured the rest: **WebGPU 32, SDL_GPU 3** example tests still read the back buffer without asking for
HiDef, so they abort before their first check.

All 35 now request HiDef by the shapes the EasyGL, Software and Vulkan suites already use
(`gdm_->setGraphicsProfileProperty(...)` where the test owns a `GraphicsDeviceManager`).
`sdlgpu_backbuffer_format_test.cpp` constructed its device with an explicit `GraphicsProfile::Reach`;
nothing it asserts (the depth/stencil contract, the `Color` transfer format) is Reach-specific, so it now
says `HiDef`, with a comment saying why the choice was incidental.

**Verified by compilation, not by running** — there is no WebGPU or SDL_GPU build tree here, and
building one is those renderers' own work, not this row's. Throwaway configure-only probes
(`build-probe/cfg-gti0003-*`, removed) compiled every changed source: WebGPU 31/31 native ones, SDL_GPU
3/3, 0 warnings. The first SDL_GPU attempt failed — `sdlgpu_minimized_retry_test.cpp` has no
`using namespace …::Graphics`, so the name is now fully qualified there; the probe is what caught it.
`webgpu_browser_coverage_test.cpp` builds only for Emscripten and there is no `emcc` here; it includes
`GraphicsProfile.hpp` and has the `using namespace` the inserted line needs.

## GTI-0004 — validation assertions in builds without validation

Seven Vulkan example tests assert `VulkanRenderer::IsValidationActiveEXT()` so that their "no
validation message" claim cannot pass vacuously. In any build with `NDEBUG` (Release, RelWithDebInfo)
`VulkanRenderer` does not load the layer by design, so they **failed** there — found by the Release smoke
before the `vulkan-parity` merge.

The intent is kept: a **Debug** build still asserts the layer is live and fails loudly if it is not. Under
`NDEBUG` the validation-dependent claims are reported as `[SKIP]` — neither a failure nor a pass. For
`Vulkan_Swapchain_Sync`, which *is* a synchronization-hazard measurement through the layer from end to
end, the whole test returns the ctest skip code 77 instead.

| Build | `NormalizedByteFormat` | `DisposedTextureEviction` | `MsaaRuntimeChange` | `Swapchain_Sync` | other three |
|---|---|---|---|---|---|
| Debug (`cmake-build-vulkan`, via the runner) | Passed | Passed | Passed | Passed | fail, as before (known `VKPAR-0013` defects) |
| Release (`build-probe/gti-release-smoke`, removed) | **Passed** (was Failed on D1) | Passed | Passed | **Skipped** | fail on the same known defects; none of their failures mentions validation |

The Release run printed the three `[SKIP]` lines the change adds. The whole `^Vulkan_` suite in Debug
is unchanged: 319 passed / 51 failed, the same set as before.

## GTI-0005 — correction: two validation claims had nothing to measure

`plans/plan_vulkan_parity.md` reported **zero validation messages** for the SDL-free X11 path
(`cmake-build-multi`) and for the native Wayland path (`cmake-build-wayland`). Both trees are
**RelWithDebInfo** — `NDEBUG` — so `VulkanRenderer` never loaded the layer there and "zero" measured
nothing. The rendering results stand; the validation claim did not, until now.

Re-measured with the layer **injected by the loader** (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`,
report flags `error,warn,info`, logged to stdout), on private displays only:

* **positive control first:** with informational reporting the layer logs one
  `Khronos Validation Layer Active` line per `VkInstance`, which proves both that it loaded and that
  the channel reports. (Logging to a file instead proved misleading: each new instance truncates it, so
  a file showed only the last process.)
* the same 14 contract tests (`StockEffectNullTexture*`, `DualTextureEffectNullSampler*`,
  `TwoSidedStencilTest*`) and 600 frames of `cna_demo_2d`, `CNA_GRAPHICS_RENDERER=VULKAN`:

| Path | Layer-active instances | Validation errors | Validation warnings | Result |
|---|---|---|---|---|
| SDL-free X11, tests (private rootful Xwayland) | 14 | **0** | **0** | 14/14 passed |
| SDL-free X11, demo 600 frames | 1 | **0** | **0** | exit 0 |
| native Wayland, tests (private Weston) | 14 | **0** | **0** | 14/14 passed |
| native Wayland, demo 600 frames (private Weston) | 1 | **0** | **0** | exit 0 |

So the conclusion holds, now with the measurement behind it. Two limits remain as stated: this is the
layer's default set, not synchronization validation, and the Wayland demo ran on the private Weston
rather than on the live GNOME session — which is exactly where it should run.

---

## Second pass (2026-09-22), opened with `plans/plan_vulkan_modern_graphics.md`

The owner's brief for the modern-Vulkan workstream restated this plan's goals as its Workstream A.
GTI-0001–0005 were already in `next` (`33105f3ae`); the rows below are what that re-check found still
open. Base: `origin/next` = `7791f8fc7`, branch `vulkan-modern-graphics`. Host: AMD Radeon 780M, RADV
PHOENIX, Mesa 25.0.7, Vulkan 1.4.305 loader, `VK_LAYER_KHRONOS_validation` from the system package.

### GTI-0006 — the Wayland half of the live-desktop hole

GTI-0001 closed the X display. It did not close Wayland: **libwayland, asked to connect with no
`WAYLAND_DISPLAY` at all, does not fail — it connects to `$XDG_RUNTIME_DIR/wayland-0`**, and on this
machine that is the owner's GNOME session (`/run/user/1000/wayland-0` exists). Any GPU test in a
`CNA_PLATFORM=WAYLAND` tree, or any SDL build whose video driver picks Wayland, run from a shell whose
`WAYLAND_DISPLAY` is unset, opens its window on the live desktop. (This agent's shell happened to carry
`WAYLAND_DISPLAY=` *empty*, which fails to connect; `env -u WAYLAND_DISPLAY` or another harness does not.)

Measured first, privately, with a fake socket (a Python listener bound to `wayland-0` inside a scratch
`XDG_RUNTIME_DIR`) and `wl_display_connect(NULL)` through ctypes:

| `WAYLAND_DISPLAY` | connects | fake `wayland-0` accepted |
|---|---|---|
| unset | yes | **yes** — the hole |
| empty string | no | no |
| `wayland-0` (explicit) | yes | yes — an explicit choice, like an inherited `DISPLAY` |

**The fix** needs no conditional ctest cannot express: `ENVIRONMENT_MODIFICATION
"WAYLAND_DISPLAY=string_append:"` turns *unset* into *empty* and leaves an exported value untouched
(checked with a no-compile probe project in `build-probe/gti-envmod`, removed: unset → `''`, empty →
`''`, `wayland-7` → `wayland-7`). Every test carries it:

* `cmake/TestDisplayPolicy.cmake`'s deferred directory walk (the one GTI-0001 added) now also appends
  the guard to every configure-time test, writing back only the property it changed;
* `cmake/UnitTests.cmake` passes it through `gtest_discover_tests(PROPERTIES …)` for the discovered
  `CnaTests` cases, which are in no directory's `TESTS` at configure time;
* not on Windows (`CNA_TEST_WAYLAND_GUARD_APPLIES`), which has neither X nor Wayland.

The decisions moved into pure functions, `cmake/TestDisplayPolicyRules.cmake`, so they can be tested:
**`CnaTestDisplayPolicy`** runs them in `cmake -P` script mode — every spelling of display 0 (`:0`,
`:0.1`, `unix:0`, `localhost:0`, `127.0.0.1:0.0`, `/tmp/.X11-unix/X0`) counts as the live desktop, the
opt-in keeps it, private displays are kept, the empty `DISPLAY=` entry is dropped, the guard is added
once and never on Windows. **`CnaTestDisplayIsolation`** (`scripts/check_test_display_isolation.py`)
checks what ctest would actually run (`--show-only=json-v1`): no live `DISPLAY` without the opt-in, no
forced `wayland-0`, no leftover empty `DISPLAY=`, and the guard on every test.

**Evidence.**

* `CnaTestDisplayPolicy`, `CnaTestDisplayIsolation` pass in `cmake-build-cnaext` (586 tests checked)
  and `cmake-build-vulkan`; the configure prints "586 / 532 configure-time tests never fall back to the
  default Wayland socket".
* Negative control for the checker: on `cmake-build-multi`, not reconfigured since before GTI-0001,
  it reports 9,637 problems in 9,634 tests.
* Negative control for the guard, live and safe (a fake `wayland-0` in a scratch runtime directory, so
  even a broken guard could reach only the fake): `Vulkan_Orientation_Calibration` in the native-Wayland
  tree, `env -u WAYLAND_DISPLAY -u DISPLAY`, through **ctest: 0 connections**, the test skipped
  ("no usable … display"); the **same binary run directly: 1 connection** — the hole is real and the
  guard is what closes it.

Not changed: an inherited, explicitly exported `DISPLAY=:0` or `WAYLAND_DISPLAY=wayland-0` (the
owner's own terminal) is still honoured, exactly as GTI-0001 decided for X. Renderer example tests
still inherit the caller's session bus; `CnaTests` and the private runner both point it at nothing.

### GTI-0007 — a guard for tests that never reach their assertions

`tools/platform/profile_dead_tests.py` reads ctest's own record of a run
(`Testing/Temporary/LastTest.log`) and names every test that

* **failed** with one of CNA's Reach-profile refusal messages in its output (`DEAD-ON-PROFILE`) —
  wording taken from every throw site in `modules/graphics/src/Xna`; HiDef's own ceilings are not
  listed, exceeding them is a real limit;
* **skipped** with such a refusal in its skip line (`SKIPPED-ON-PROFILE`) — ctest writes an exit 77
  as "Test Passed." in that log, so a skip is only visible through its reason;
* **passed** while reporting a leg "unavailable" because of `NotSupportedException`
  (`WARN-LEG-UNAVAILABLE`, a warning: legitimate on a renderer that does not rasterize).

`run_gpu_tests_private.sh` runs it after every ctest run whose log is newer than the run's start, and
keeps ctest's exit status. `CnaProfileDeadTestClassifier` is its self-test (a failed and a skipped
refusal and a NotSupported leg found; a passing refusal test, a HiDef limit and a genuine capability
skip not). It is a classifier, not a framework: a flagged test is a test defect until shown otherwise
and is not a renderer result either way.

**It found what it was written for, the same day.** On the modern-Vulkan baseline it named
`CNAEXT_ShadowReceiver`, `CNAEXT_ClusteredLights` and `CNAEXT_GpuDriven` (dead on
`GetBackBufferData`), and after the skip-line rule, the 17 CNAEXT examples that had been skipping on
the same refusal — `plans/plan_vulkan_modern_graphics.md` VMG-0004. The warning rule found the
GTI-0009 legs.

### GTI-0008 — the runner runs one command too

`run_gpu_tests_private.sh --exec <command…>` runs one command — a test binary with a gtest filter, a
demo, a stress run — in exactly the private environment a ctest run gets (headless Weston, rootful
Xwayland with DRI3, private `XDG_RUNTIME_DIR`, no session bus). Measured: inside, `DISPLAY=:2`,
`WAYLAND_DISPLAY=cna-weston-…`, `xdpyinfo` reports DRI3, `vulkaninfo` lists AMD Radeon 780M (RADV
PHOENIX) and llvmpipe; a command's exit status 3 comes back as 3; afterwards no Weston, shell client
or Xwayland of the run remains (process table compared before/after, the owner's own `Xwayland` and
`dbus-daemon` untouched). The whole-suite runs of this pass (below and in the VMG plan) show the same
before/after equality.

### GTI-0009 — eight shared tests passed with dead legs

The modern-Vulkan baseline (`plans/plan_vulkan_modern_graphics.md` VMG-0001) showed `[SKIP] … backbuffer
oracle unavailable on VULKAN (NotSupportedException) -- boundary recorded` inside **passing** tests.
These shared sources catch the exception, record a "boundary" and go on: under the default Reach
profile `GetBackBufferData` throws (SOFTWARE-213), so their back-buffer legs never ran on any renderer.

`rendertarget_first_use`, `rendertarget_producer_consumer` (three registrations: plain, MSAA,
SyncVal), `rendertarget_backbuffer_consumer`, `frontface_winding`, then — found by GTI-0007's warning
rule — `bound_target_lifetime`, `deferred_source_lifetime`, `backbuffer_first_read` and
`rendertarget_sampling_orientation` now ask for HiDef **where the adapter offers it**
(`GraphicsAdapter::IsProfileSupported(HiDef)`, so Direct3D 9 on a Reach-only adapter keeps its
recorded boundary rather than failing to create a device).

**Evidence (RADV, private runner):** all ten registrations pass with the legs live — e.g. 95 PASS
lines in `Vulkan_FrontFaceWinding`, 90 in `Vulkan_RenderTarget_BackbufferConsumer`; no
`WARN-LEG-UNAVAILABLE` left in the classic run for them. `Vulkan_SpriteBatch3DOrder` still records one
unavailable oracle for a different, non-profile reason ("the render target must be resolved before
it…"); it already requests HiDef and is left as it is.

### GTI-0010 — the WebGPU and SDL_GPU tests, run

GTI-0003 had compiled the 35 fixes and not run them. Two throwaway probe trees
(`build-probe/gti-webgpu`: `WEBGPU` default, pinned `~/deps/wgpu-native-v29.0.1.1`, Debug, `libcna.so`;
`build-probe/gti-sdlgpu`: `SDL_GPU` default; each built only the tests in question — 1.3 GB and 1.2 GB,
both removed after this row) and the private runner:

| Suite | Tests | Passed | Failed | Died/skipped on the profile |
|---|---|---|---|---|
| WebGPU (the 31 native ones; the 32nd is Emscripten-only) | 31 | **29** | 2 | 0 |
| SDL_GPU | 3 | **2** | 1 | 0 |

Rerun with `VK_DRIVER_FILES` pinned to the RADV ICD only: identical, so the adapter is the Radeon.
**The three failures are real and are left for those renderers' own workstreams, not fixed here:**

* `WebGPU_Scissor_Cardinality` — `ArgumentException`: "The scissor rectangle must fit inside the active
  render surface". The shared scissor-range validation refuses what this test sets; test contract or
  renderer, to be decided there.
* `WebGPU_RealWindowResize` — "the platform resize never arrived" on the private rootful Xwayland,
  which has no window manager; environment-sensitive, needs a WM-backed private server to judge.
* `SdlGpu_BackbufferFormat` — "Cannot clear depth or stencil because the device does not have an
  active depth or stencil buffer": the test clears depth on a `DepthFormat::None` device, which the
  shared XNA-rule validation refuses.

### Second-pass result

* Automated tests default to no live display, **and** no longer fall back to the live Wayland
  compositor; both are pinned by tests.
* The private runner is proven for Wayland (native-Wayland Vulkan tree: 380/381 `Vulkan_*`) and X11
  (`cmake-build-vulkan` over SDL3's x11 driver on the private rootful Xwayland: 369/370), all on RADV,
  with no leftover processes.
* Dead-test coverage: the WebGPU/SDL_GPU fixes run (31/31, 3/3 reach their checks), eight shared
  sources' dead legs revived, and a guard that names the whole class after every private run.
* Build trees: `cmake-build-cnaext` 2.5 GB, `cmake-build-vulkan` 5.4 GB (unchanged), both on
  `libcna.so`; the two probe trees removed.
