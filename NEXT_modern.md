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
| ✅ | `MOD-105`, `MOD-108`/`124`, `MOD-131` — unsupported formats refused, format-aware readback, and the end-to-end proof that values above 1.0 survive |
| ⛔ | `MOD-106` — lenient-substitution opt-out, dropped (it would preserve a behaviour that never existed) |
| ✅ | `MOD-107` — `RenderTargetCube` carries its `SurfaceFormat` to the renderer too (the path IBL needs) |
| ✅ | `MOD-118`–`MOD-123`, `MOD-125` — completeness diagnostic, depth/MSAA/mip/MRT on float targets, half-float filtering query |
| ✅ | `MOD-21`/`MOD-22` — `Uncharted2` and the settings fields the passes read |
| ✅ | Phase 2 core — `PostProcessContext`, `FullscreenPass`, `PostProcessPass`, `RenderTargetPool`, `BlitPass`, `PostProcessChain` (`MOD-16`, `19`, `200`–`208`, `225`–`228`) |
| ✅ | Phase 3 — `TonemapPass`, all five operators, shader verified against a CPU reference (`MOD-300`–`313`, `317`) |
| ✅ | Phase 7 core — `RenderPipeline`, the consumer `RenderPipelineSettings` never had (`MOD-700`–`737`) |
| ✅ | Phase 4 — `BloomPass`, wired ahead of tonemapping (`MOD-400`–`418`) |
| ✅ | Phase 6 — `FxaaPass`, wired after tonemapping (`MOD-600`–`606`) |

**The HDR spine is complete and verified end to end.** A game can wrap its draw calls in
`RenderPipeline::begin`/`end`, enable HDR, bloom, a tonemapping operator and FXAA, and get them --
on EasyGL, in software, under Xvfb. With everything off it allocates nothing and renders exactly
what it would have rendered without a pipeline.

**Next up:** Phase 5 (depth/normal prepass + SSAO, `MOD-500`–`MOD-529`) is the last post-process
subsystem and the one that needs new scene-side plumbing rather than another fullscreen pass. Then
Phase 8 (shadow maps). Smaller open rows behind those: `MOD-203` (restore-on-exception around a
pass), `MOD-209`/`MOD-210`, `MOD-405`/`407`/`409`/`413`/`415`–`417` (bloom quality/perf/goldens),
`MOD-314`–`316`/`318`–`320` (tonemap goldens, example, docs), `MOD-5`, and Phase 1's
`MOD-130`–`MOD-141`.

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
| 2026-08-17 | `cmake-build-cnaext` (OPENGLES3, `CNA_CNAEXT=ON`, Debug), base `origin/next` @ `05a9eab0` | full `CnaTests`, from the repo root, under Xvfb (`MOD-1710`) | **7548 ran · 7484 pass · 64 skip · 0 fail** |
| 2026-08-17 | same, with `MOD-105`/`108`/`124`/`131` applied | same | 7565 ran · 7501 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, with `MOD-107` applied | same | 7567 ran · 7503 pass · 64 skip · **0 fail** |
| 2026-08-17 | same, through Phases 2/3/4/6/7 (tonemap, bloom, FXAA, pipeline) | same | 7630 ran · 7566 pass · 64 skip · **0 fail** |

That is a clean run, and it is new. Measuring the first baseline on this branch turned up eight
failures and a segfault at ~7300 tests, all of them pre-existing on `next` (verified by rebuilding
the branch without any engine-layer change and getting the identical set). They were fixed on
`next` itself rather than worked around here — `next` @ `05a9eab0`:

- **`PLAT-46`** — a `Game` whose construction throws never ran `~Game`, so its platform stayed
  installed process-wide while the unwind destroyed it. Every later `Keyboard::GetState` /
  `StorageDevice` / `TitleContainer` call read freed memory through the ambient accessor; that was
  the segfault. Fixed with a scope guard that undoes the installation on the failed-construction
  path only.
- **`GLTF-374`** — filling in the renderer inventories for `igl`/`pixijs` exposed that IGL bound an
  opaque-white 1×1 stand-in into the *normal-map* slot, lighting every PBR material without a
  normal map as though its pixels were tilted 55°. It now binds the flat-normal texel.
- **EasyGL diagnostics** — the GL banner and capability dump moved off `std::cout` onto the logger.
- Two `GameEventSemanticsGoldenTest` cases (only reachable once the segfault was gone) now skip
  where the parameterised platform cannot back the build's renderer.

An earlier baseline in this file recorded 6360/6351 — measured on the **develop**-based tree before
the rebase onto `next`, so it never described this branch; replaced above.

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
