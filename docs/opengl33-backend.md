# OPENGL33 (desktop OpenGL 3.3 core profile) Backend — Status

`OPENGL33` is one of the 4 public GL-family `CNA_GRAPHICS_BACKEND` values introduced by
`plan_glbackends.md` — it shares its entire implementation with `OPENGLES`/`WEBGL1`/`WEBGL2`
(`src/CNA/Internal/Backends/EasyGL/`, on top of the sibling `easy-gl` library), distinguished at
compile time by the `CNA_GL_PROFILE_OPENGL33` definition. Unlike the other 3 profiles (all
OpenGL ES / WebGL, GLSL ES syntax), `OPENGL33` requests a real desktop `SDL_GL_CONTEXT_PROFILE_CORE`
context, GL 3.3.

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not done.

## What's real today

- ✅ **Context creation** — `EasyGLGraphicsBackend`'s constructor requests
  `SDL_GL_CONTEXT_PROFILE_CORE`, major 3 / minor 3 when compiled with `CNA_GL_PROFILE_OPENGL33`
  (`plan_glbackends.md` GLB-8).
- ✅ **Shader compilation** — every embedded shader in `EasyGLGraphicsBackend.cpp` is authored once
  against GLSL ES 3.00 syntax; `AdaptGlslEs300ForActiveProfile()` rewrites the
  `#version 300 es` / `precision ... float;` header pair to `#version 330 core` (dropping the
  precision line) for this profile at first-use time — no shader is duplicated (`plan_glbackends.md`
  GLB-10/GLB-11).
- ✅ **Real driver verification** — built and ran, against a real desktop Mesa 4.5 core-profile GL
  context (not a mock/headless stub), 7 examples covering `BasicEffect` (default lighting),
  `DualTextureEffect`, `SkinnedEffect`, `SkinnedPbrEffect`, `AlphaTestEffect`,
  `EnvironmentMapEffect`, and `SpriteBatch` — all exit 0 with zero shader/link compile errors, and
  every golden pixel-comparison check `[PASS]`es, byte-identical to the same scenes' `OPENGLES`
  (GLES 3.0) reference images. This also confirms Mesa's desktop core-profile compiler accepts the
  handful of inline `highp`/`mediump` qualifiers some shader bodies use (e.g.
  `uniform highp vec4 uDiffuseColor;`) as no-ops, with no extra stripping needed beyond the
  top-of-file `precision` line.
- ✅ **Full `CnaTests` regression run** — built and ran the complete `CnaTests` GTest binary under
  `-DCNA_GRAPHICS_BACKEND=OPENGL33`: same 117 pre-existing failures as the `OPENGLES` baseline
  (all in unrelated Media/Content/XNB/Audio/Video subsystems — no ffmpeg-decodable fixtures/real
  media devices in this sandbox), zero new failures, zero Graphics/EasyGL/shader-related failures.
- ✅ **Full example/pixel-test suite — all 241 `cna_easygl_test`-registered binaries, not a
  sample** (`plan_glbackends.md` GLB-22, upgraded from an earlier 7-binary sample). Built all 241
  (`cmake --build --target <all 241 names>`, exit 0) and ran every one of them
  (`DISPLAY=:99` Xvfb, from the repo root so relative-path golden-image/fixture lookups resolve
  correctly): **236/241 pass, 0 crash, 5 fail** (updated after the `GLB-40` fix below — was
  235/241, 6 fail, before it).
  - **The remaining 5 failures (of the original 6) are all pre-existing, confirmed by building and
    running the exact same 6 binaries under `OPENGLES` for direct comparison — they fail
    identically there too**:
    `cna_oracle_render_easygl`, `cna_test_avatar_tint_routing` (documented pre-existing bug,
    `plan_graphics.md` Task 1115 — a real `AvatarRenderer` tint-doubling defect, unrelated to GL
    profile), `cna_test_easygl_graphicsdevicemanager_vsync`,
    `cna_test_easygl_graphicsdevice_reference_stencil` (also a documented pre-existing gap,
    `plan_graphics.md` Task 872), `cna_test_easygl_mrt`.
  - **1 failure was real and `OPENGL33`-specific — root-caused and fixed as `GLB-40`** (not the
    shader-compile-failure explanation first guessed, which turned out wrong — see below).
    `cna_test_easygl_shipgame_particle_shader` uses a `GL_POINTS`/`gl_PointSize`/`gl_PointCoord`
    particle-sprite shader. GLES/WebGL always honor a vertex shader's `gl_PointSize` output
    automatically; desktop GL requires an explicit `glEnable(GL_VERTEX_PROGRAM_POINT_SIZE)`, which
    `EasyGLGraphicsBackend` never called — every point silently rendered at the fixed 1.0-pixel
    default instead of the shader's requested size, invisible at the test's sampled pixel.
    (The initial guess — that the shader's hardcoded `#version 300 es` fails to compile under a
    desktop core-profile driver — was verified wrong with a standalone test program: Mesa accepts
    that version pragma leniently even under a core-profile context.) Fixed with a
    `#ifdef CNA_GL_PROFILE_OPENGL33`-gated `glEnable` call, loaded via a runtime function pointer
    (matching this project's "no static `libGL` linkage" convention). All 4 of the test's checks
    now `[PASS]` under `OPENGL33`; re-verified no regression under `OPENGLES`. See
    `plan_glbackends.md`'s `GLB-40` entry for full detail.

## What's not yet done
- ⬜ **CI/CTest identity** — `cmake/Tests/EasyGLTests.cmake`'s guard was widened to cover both
  `OPENGLES` and `OPENGL33` (`plan_glbackends.md` GLB-6), but there is no dedicated
  `OPENGL33`-only CTest label distinguishing it from an `OPENGLES` run.
- ⬜ **Non-Linux desktop verification** — only verified on Linux (Mesa). Windows/macOS desktop GL
  3.3 core-profile drivers (especially older/proprietary NVIDIA/AMD Windows drivers, which are
  historically stricter about GLSL core-profile syntax than Mesa) have not been checked.

## Relationship to the other 3 GL-family backends

See `plan_glbackends.md` §2's table. In short: `OPENGLES` is today's original `EasyGL` public
backend renamed (GLES 3.0, unchanged behavior); `WEBGL2` is the same GLES 3.0 path under
Emscripten; `WEBGL1` (GLES 2.0 / Emscripten) has a real GLSL ES 1.00 shader rewrite implemented and
verified as far as this sandbox can go, including `SkinnedEffect`/`SkinnedPbrEffect` (see
`docs/webgl1-backend.md`) — no real browser-level driver verification exists for any of the 4
profiles in this sandbox.
