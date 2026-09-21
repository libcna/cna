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
