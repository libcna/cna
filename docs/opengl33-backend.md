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

## What's not yet done

- ⬜ **Full example/pixel-test suite** — only 7 of the ~241 `cna_easygl_test`-registered example
  binaries were built and run under this profile (a deliberate sample across every distinct shader
  family in `EasyGLGraphicsBackend.cpp`, not exhaustive). The remaining ~234 have not been
  individually verified under `OPENGL33` (`plan_glbackends.md` GLB-22).
- ⬜ **CI/CTest identity** — `cmake/Tests/EasyGLTests.cmake`'s guard was widened to cover both
  `OPENGLES` and `OPENGL33` (`plan_glbackends.md` GLB-6), but there is no dedicated
  `OPENGL33`-only CTest label distinguishing it from an `OPENGLES` run.
- ⬜ **Non-Linux desktop verification** — only verified on Linux (Mesa). Windows/macOS desktop GL
  3.3 core-profile drivers (especially older/proprietary NVIDIA/AMD Windows drivers, which are
  historically stricter about GLSL core-profile syntax than Mesa) have not been checked.

## Relationship to the other 3 GL-family backends

See `plan_glbackends.md` §2's table. In short: `OPENGLES` is today's original `EasyGL` public
backend renamed (GLES 3.0, unchanged behavior); `WEBGL2` is the same GLES 3.0 path under
Emscripten; `WEBGL1` (GLES 2.0 / Emscripten) is not yet functional — its shader bodies still need a
real GLSL ES 1.00 rewrite (`attribute`/`varying`/`texture2D()` instead of `in`/`out`/`texture()`,
plus dropping `layout(location=N)` qualifiers GLSL ES 1.00 doesn't support), tracked as
`plan_glbackends.md` GLB-36 and explicitly not attempted yet — it needs an architecture decision
(how vertex attribute locations get (re)bound without `layout(location=N)`) before implementation,
not a mechanical shader edit.
