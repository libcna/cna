# NEXT_modern.md — running ledger for the CNAEXT engine layer

Continuity file for [`plan_modern.md`](plan_modern.md) (the `MOD-*` backlog implementing
[`CNAEXT.md`](CNAEXT.md)). Same role `NEXT_skia.md` has for the Skia renderer: read this first,
do not reconstruct the layer's state from the general `NEXT.md`.

---

## 1. Where the work stands

**Phase 0 (foundation) — in progress.** Nothing of the orchestration layer
(`RenderPipeline`, post-process passes, shadows, skybox/IBL, compute) exists yet; see
`docs/cnaext-engine-layer.md` for the honest capability boundary.

| Done | Task |
|---|---|
| ✅ | `MOD-1` — `CNA/Graphics/CNAEXT.hpp` master include + `CnaExtMasterIncludeTests` |
| ✅ | `MOD-2` — `cnaext` CMake configure/build preset |
| ✅ | `MOD-3` — `scripts/check_cnaext_guards.sh` |
| ✅ | `MOD-4` — `docs/cnaext-engine-layer.md` |
| ✅ | `MOD-14` — this file |
| ✅ | `MOD-100`/`101`/`103`/`104` — float render-target capabilities, derived from a per-format renderer verdict |
| ✅ | `MOD-115`/`116`/`117` — EasyGL really allocates RGBA16F/RGBA32F targets, probed at runtime |

**Next up (critical path to a first HDR frame):** `MOD-105`–`MOD-108` (unsupported-format policy,
cube-target format, float readback) → `MOD-118`–`MOD-125` (the rest of the EasyGL float work: depth
combination, MSAA resolve, mips, MRT) → `MOD-131` (a float target that provably keeps values above
1.0) → `MOD-200`–`MOD-210` (pass infrastructure) → `MOD-300`–`MOD-305` (tonemapping) →
`MOD-700`–`MOD-712` (`RenderPipeline`). `MOD-5`, `MOD-21`, `MOD-22` remain open from Phase 0.

**Owner decisions in force** (asked 2026-08-17): start with the HDR spine; EasyGL is the
reference renderer with Vulkan and D3D11 as the committed follow-ups and the rest opportunistic;
verify every task in a real `cmake-build-cnaext` build; one task = one commit, pushed as it lands.

---

## 2. Build environment (what a fresh container needs)

A fresh clone cannot configure CNA without these. Recorded here because the first session spent
real time rediscovering them one CMake error at a time.

```bash
# 1. Vendored submodules (non-recursive is sufficient and much faster)
git submodule update --init vendor/googletest third_party/SDL third_party/SDL_image third_party/SDL_mixer

# 2. Sibling checkouts CNA expects next to its own directory (NOT submodules)
cd .. && git clone https://github.com/openeggbert/sharp-runtime.git
        git clone -b develop https://github.com/openeggbert/easy-gl.git
        git clone -b develop https://github.com/openeggbert/meta-gl.git

# 3. System packages (Debian/Ubuntu). SDL3 is built from source at configure time and needs
#    the full X11 set; the FFmpeg headers are required by the media module.
apt-get install -y \
  libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxkbcommon-dev libwayland-dev \
  libdecor-0-dev libxtst-dev libxext-dev libxfixes-dev libxinerama-dev libdrm-dev libgbm-dev \
  libibus-1.0-dev libasound2-dev libpulse-dev libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev ccache

# 4. Configure + build (first configure builds SDL3 into .sdl-prebuilt-<system>-<arch>/, ~minutes;
#    the first CnaTests build is ~1055 targets, so keep it to -j4 in a sandbox)
cmake --preset cnaext
cmake --build --preset cnaext --target CnaTests -j4
SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests --gtest_filter='CnaExt*'
```

Notes:

- `CNA_BUILD_EXAMPLES` is OFF in the preset; turn it on when a task needs the module's demos.
- `ccache` is picked up automatically (`CNA_USE_CCACHE=ON`); without it installed, a build tree
  configured with `CMAKE_CXX_COMPILER_LAUNCHER=ccache` fails with `/bin/sh: 1: ccache: not found`.
- Draco is absent here, so Draco-compressed glTF primitives throw at import time by design.

---

## 3. Test baselines

Recorded so "no regressions" is checkable rather than asserted. Update at each phase boundary
(`MOD-1711`).

| Date | Build | Suite | Result |
|---|---|---|---|
| 2026-08-17 | `cmake-build-cnaext` (OPENGLES3, `CNA_CNAEXT=ON`, Debug), branch base `origin/next` | full `CnaTests`, from the repo root, under Xvfb (`MOD-1710`) | **7292 pass · 8 fail · aborts** at `GraphicsDeviceManagerPlatformTest.ADefaultManagerConstructsWithNoGame` |
| 2026-08-17 | same, with `MOD-115`–`MOD-117` applied | same | 7294 pass (+2 = the new tests) · same 8 fail · same abort point |

**The 8 failures and the abort are pre-existing on `next`** — measured by rebuilding the branch
without the engine-layer changes and getting the identical set, not assumed:

- `GltfRendererPbrFallbackPolicy.InventoryCoversEveryRendererThatConsumesPbrMaps`,
  `…SpecularTextureInventoryClassifiesEveryPbrRenderer`,
  `GltfRendererIndexWidthPolicy.InventoryClassifiesEveryRenderer` — renderer inventories that have
  not caught up with the identities `next` added.
- `GraphicsDeviceRendererTest.StartupDiagnosticNeverWritesToStdout` — EasyGL's capability line is
  written with `std::cout`; the test wants stderr. Untouched here deliberately: fixing it is a
  one-line change in someone else's area, unrelated to this plan.
- `GamePlatformOwnershipTest` (4 cases).
- The abort: the suite segfaults entering `GraphicsDeviceManagerPlatformTest`; that suite passes on
  its own, so it is a full-run ordering/teardown issue, again present without any of this work.

An earlier baseline in this file recorded 6360/6351 — that was measured on the **develop**-based
tree before the rebase onto `next` and does not describe this branch; it has been replaced above.

Two things about how the suite is run matter more than they look:

- **Run it from the repository root**, not from the build directory. Content/media/audio tests
  resolve fixtures like `tests/assets/xnb/...` relative to the CWD; from `cmake-build-cnaext/` that
  is 116 failures of pure path noise.
- **Run it with a display.** Without one, every test whose fixture constructs a `GraphicsDevice`
  fails on "No available video device" (~1000 failures). `Xvfb :99 -screen 0 1280x720x24` plus
  `DISPLAY=:99 SDL_VIDEODRIVER=x11` gives Mesa llvmpipe, which reports **OpenGL ES 3.2** to
  EasyGL — enough for the whole Phase 1–7 HDR spine to be verified for real, in software.

```bash
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy ./cmake-build-cnaext/CnaTests
```

`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` segfaulted in an earlier run started
from the build directory (with no media fixtures resolvable); from the repo root it passes. Worth
remembering if a future run aborts mid-suite.

---

## 4. Open questions

The ten `OQ-*` entries at the end of `plan_modern.md` each carry a default that is being followed.
Four were answered by the owner on 2026-08-17 (`OQ-1`, `OQ-7`, `OQ-8`, `OQ-9` — see §1); the rest
stay on their defaults until raised.
