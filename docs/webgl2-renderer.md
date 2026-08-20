# WEBGL2 (Emscripten, GLES 3.0 → WebGL 2.0) Renderer — Status

`WEBGL2` is one of the public GL-family `CNA_GRAPHICS_RENDERER` values (the original 4 were
introduced by `plans/plan_glbackends.md`; the Phase-2 expansion later added `OPENGLES2`) — it shares
its entire implementation with `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL1`
(`modules/renderers/easygl/`, on top of the sibling `easy-gl` library), distinguished at
compile time by the `CNA_GL_PROFILE_WEBGL2` definition. Unlike `OPENGLES3`/`OPENGL33` (native
desktop/mobile), `WEBGL2` only builds for Emscripten and requests the same GLES 3.0 context
`OPENGLES3` does — Emscripten/browsers map a GLES-3.0-shaped SDL3 GL context request to a real
WebGL 2.0 context. This is functionally what used to be "`EASYGL` under Emscripten" before this
plan gave it its own public name.

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not done.

## What's real today

- ✅ **Context creation** — unchanged from `OPENGLES3`'s GLES 3.0 request (`plans/plan_glbackends.md`
  GLB-8); confirmed by reading the vendored SDL3 Emscripten renderer
  (`third_party/SDL/src/video/emscripten/SDL_emscriptenopengles.c:87-98`) — it derives WebGL-1-vs-2
  purely from whether the requested GL major version is 3, which `EasyGLRenderer`'s
  constructor already sets correctly for this profile. No `-s USE_WEBGL2`/`FULL_ES3` emcc flags
  are needed for SDL3's own context creation (`plans/plan_glbackends.md` GLB-9 — that whole class of
  flag only matters for a GLES-emulation-shim code path SDL3 doesn't use here). **Correction found
  later the same night**: `cna_house3d_demo`'s own Emscripten *link* options (a separate, per-target
  CMake setting, not SDL3's context creation) did hardcode `-sMIN_WEBGL_VERSION`/
  `-sMAX_WEBGL_VERSION` — happened to already be the correct value (`2`/`2`) for this profile, but
  was wrong for `WEBGL1` (see `docs/webgl1-renderer.md`'s GLB-9-revisited note and
  `cmake/Examples.cmake` commit `d44ed617`) and has since been made conditional on
  `CNA_GRAPHICS_RENDERER` for both profiles.
- ✅ **Real `emcmake`/`emcc` build** — this sandbox has a working Emscripten SDK at `~/emsdk` (not
  on `PATH` by default — source `~/emsdk/emsdk_env.sh` and additionally add
  `~/emsdk/upstream/emscripten` and `~/emsdk/node/*/bin` to `PATH`). `emcmake cmake
  -DCNA_GRAPHICS_RENDERER=WEBGL2` configures cleanly and the core `CNA` library compiles cleanly.
- ✅ **Real full-binary link** — building the actual `cna_house3d_demo` example target (gated to
  the GL-family + `VULKAN` renderers, `cmake/Examples.cmake`) initially failed with
  `wasm-ld: undefined symbol: __cpp_exception`, a genuine exception-handling ABI mismatch between
  CNA's own top-level Emscripten flags and the sibling `easy-gl`/`meta-gl` chain's. Root-caused and
  fixed entirely within `cnagl` (`plans/plan_glbackends.md` GLB-39, `CMakeLists.txt`) — no sibling-repo
  change needed. After the fix, `cna_house3d_demo` links cleanly, producing a real
  `.wasm`/`.js`/`.html`.
- ✅ **Real execution, as far as this environment can go** — running the built `.js` under Node
  (`node cna_house3d_demo.js`) executes real compiled C++ through `SDL_Init`/window-creation before
  hitting `ReferenceError: window is not defined` — a genuine, expected browser-only DOM API gap
  (Node has no `window`/`document`; SDL3's Emscripten renderer touches `window.matchMedia` for
  system dark-mode detection during init). This is the same category of limitation the `CANVAS`
  renderer already documents (no DOM under Node), not something specific to `WEBGL2` or a
  regression.
- ✅ **No regression to a previously-working Emscripten configuration** — rebuilt `cna_demo_2d`
  under `CANVAS` (Emscripten, unrelated to the GL family) after the `GLB-39` exception-flag change;
  still builds and links cleanly.

## What's not yet done

- ⬜ **Real browser verification** — never run in an actual browser with a real WebGL 2 context;
  only verified as far as "compiles, links, and starts executing under Node until it hits a
  DOM-only API." This is the same honesty bar `CANVAS` and the original `EASYGL`-under-Emscripten
  claim already used — see `README.md`'s platform matrix.
- ⬜ **`CnaTests` under Emscripten** — attempted and fails with `'SDL3/SDL.h' file not found`
  regardless of GL-family renderer (also reproduces under `CANVAS`). Not a regression from this
  plan: the project's own `web` CMake preset already sets `CNA_BUILD_TESTS=OFF`, so a GTest build
  under Emscripten was never a supported/exercised configuration to begin with.
- ⬜ **CI/CTest identity** (`plans/plan_glbackends.md` GLB-25) — no dedicated `WEBGL2`-only CTest
  registration distinguishing it from `OPENGLES3`/`OPENGL33` runs.
- ⬜ **Pixel-level golden-image verification** — unlike `OPENGL33` (verified pixel-identical to
  `OPENGLES3` via a real desktop GL context), no pixel-comparison test has run against a real WebGL
  2 context (browser or otherwise) for `WEBGL2`.

## Relationship to the other 3 GL-family renderers

See `plans/plan_glbackends.md` §2's table and `docs/opengl33-renderer.md`'s own version of this section.
In short: `OPENGLES3` is today's original `EasyGL` public renderer renamed (GLES 3.0, unchanged
behavior, native/desktop); `WEBGL2` is the same GLES 3.0 path, but under Emscripten instead of
native; `OPENGL33` is a new desktop GL 3.3 core-profile variant; `WEBGL1` (GLES 2.0 / Emscripten)
needs a real GLSL ES 1.00 shader-body rewrite before it can build at all, tracked as
`plans/plan_glbackends.md` GLB-36.
