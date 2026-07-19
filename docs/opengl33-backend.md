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
  correctly): **235/241 pass, 0 crash, 6 fail.**
  - **5 of the 6 failures are pre-existing, confirmed by building and running the exact same 6
    binaries under `OPENGLES` for direct comparison — they fail identically there too**:
    `cna_oracle_render_easygl`, `cna_test_avatar_tint_routing` (documented pre-existing bug,
    `plan_graphics.md` Task 1115 — a real `AvatarRenderer` tint-doubling defect, unrelated to GL
    profile), `cna_test_easygl_graphicsdevicemanager_vsync`,
    `cna_test_easygl_graphicsdevice_reference_stencil` (also a documented pre-existing gap,
    `plan_graphics.md` Task 872), `cna_test_easygl_mrt`.
  - **1 failure is real and `OPENGL33`-specific, but not a regression in this plan's shader-adapter
    work**: `cna_test_easygl_shipgame_particle_shader` passes under `OPENGLES` but fails under
    `OPENGL33` (`[FAIL] Check A/D`, no rendered content — background color only). Root cause:
    `examples/easygl_shipgame_particle_shader_test.cpp` hardcodes `#version 300 es` directly in a
    **custom `ShaderEffect`** (raw user-authored GLSL passed straight to
    `EasyGLEffectBackend::CompileProgram()`, which — by design, matching every other backend —
    does **not** run shader source through `AdaptGlslEs300ForActiveProfile()`; that adapter only
    covers this file's own ~26 *stock* effect shaders). `#version 300 es` is not valid syntax for a
    real desktop GL 3.3 core-profile driver, so the shader fails to compile silently (no
    `std::cerr` output reached this test's own stdout capture — worth a follow-up to make custom
    `ShaderEffect` compile failures under `OPENGL33` less silent, not investigated further here).
    This is the same, already-existing "custom `ShaderEffect` shaders are not portable across GL
    flavors, the caller owns that" limitation every backend already has — this specific example
    was simply never exercised against a desktop-core-profile target before `OPENGL33` existed.

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
verified as far as this sandbox can go (see `docs/webgl1-backend.md`) — its `SkinnedEffect`/
`SkinnedPbrEffect` shaders don't work yet (a real, documented, separate gap: their `uvec4`
bone-index vertex attribute has no GLSL ES 1.00 equivalent, needing a real float-encoding
architecture change, not attempted — `plan_glbackends.md` GLB-36).
