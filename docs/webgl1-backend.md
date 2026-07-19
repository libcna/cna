# WEBGL1 (Emscripten, GLSL ES 1.00 → WebGL 1.0) Backend — Status

`WEBGL1` is one of the 4 public GL-family `CNA_GRAPHICS_BACKEND` values introduced by
`plan_glbackends.md` — it shares its entire implementation with `OPENGLES`/`OPENGL33`/`WEBGL2`
(`src/CNA/Internal/Backends/EasyGL/`, on top of the sibling `easy-gl` library), distinguished at
compile time by the `CNA_GL_PROFILE_WEBGL1` definition. Unlike the other 3 profiles (all
GLSL ES 3.00 / desktop GLSL 3.30 core, close enough in body syntax to share one shader source),
`WEBGL1` requests a GLES 2.0-shaped context (mapping to a real WebGL 1.0 context under Emscripten)
and needs a real GLSL ES 1.00 shader rewrite, not just a header swap.

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not done.

## What's real today

- ✅ **Context creation** — `EasyGLGraphicsBackend`'s constructor requests GLES 2.0
  (`SDL_GL_CONTEXT_MAJOR_VERSION=2`) for this profile (`plan_glbackends.md` GLB-8). Confirmed via
  the vendored SDL3 Emscripten backend that this correctly produces a real WebGL 1 context (not
  WebGL 2) — see `docs/webgl2-backend.md`'s GLB-9 note for the mechanism.
- ✅ **Shader body rewrite** (`plan_glbackends.md` GLB-10/11/12/36) — every embedded shader in
  `EasyGLGraphicsBackend.cpp` is authored once against GLSL ES 3.00; `AdaptGlslEs300ForActiveProfile()`'s
  `WEBGL1` branch (`TransformGlslEs300BodyToEs100()`) rewrites:
  - `layout(location=N) in TYPE NAME;` → `attribute TYPE NAME;` (GLSL ES 1.00 has no `layout`
    qualifier at all — the location itself is instead rebound from the C++ side, see below)
  - `out`/`in` varyings (vertex/fragment respectively) → `varying`
  - the single `out vec4 FragColor;` output every shader in this file uses (no MRT anywhere) →
    dropped; every `FragColor` reference in the body (including `FragColor.a`-style member access)
    rewritten to the ES 1.00 built-in `gl_FragColor`
  - `texture(sampler, ...)` → `texture2D(...)`/`textureCube(...)` depending on the sampler's
    actual declared type (scanned from the shader source itself — not hardcoded — only `uEnvMap`
    is `samplerCube`, everything else is `sampler2D`)
- ✅ **Vertex attribute locations preserved without touching any VAO setup code** — since GLSL
  ES 1.00 has no `layout(location=N)`, a new `ExtractVertexAttribLocations()` helper parses the
  `(location, name)` pairs out of the *original* ES 3.00 source, and `CompileAndLink()`/
  `EasyGLSpriteBatchBackend::InitializeResources()` rebind those exact same numeric locations via
  `Program::bind_attrib_location()` before linking (`WEBGL1` only). Every existing
  `VertexArray`/VAO attribute-binding call site elsewhere in this file (all hardcoded numeric
  indices, e.g. `vao.enable_attribute(0)`) needed zero changes.
- ✅ **Standalone string-transform verification** — before attempting any GL/Emscripten build, the
  transform functions were extracted into an isolated, regular-`g++`-compiled test harness (no GL,
  no Emscripten) and run against real shader strings copied from this file — `SpriteBatch` (spaced
  `layout(location = N)`), `EnvironmentMapEffect` (has the one `samplerCube`), and `SkinnedEffect`
  (has the one `uvec4` attribute). Every output line was manually inspected and confirmed correct
  before moving on to a real build.
- ✅ **Real `emcmake`/`emcc` build** — `-DCNA_GRAPHICS_BACKEND=WEBGL1` configures cleanly and
  `cna_house3d_demo` (which exercises `BasicEffect`'s colored/textured/lit shader family) compiles
  and links cleanly end to end, producing a real `.wasm`/`.js`/`.html`.
- ✅ **Real execution, as far as this environment can go** — running the built `.js` under Node
  executes real compiled C++ through the same `SDL_Init`/window-creation sequence `WEBGL2` already
  reaches (`window` → `screen` → `document` DOM globals, verified by incrementally polyfilling each
  one in Node and re-running) before hitting a genuine browser-only DOM API gap — the same category
  of limitation already documented for `CANVAS`/`WEBGL2`, not something specific to `WEBGL1` or a
  regression. No GL-context-creation or shader-compile error was ever reached in this environment.
- ✅ **Found and fixed a real, separate Emscripten linker-flag bug while verifying this** —
  `cna_house3d_demo`'s Emscripten link options hardcoded `-sMIN_WEBGL_VERSION=2
  -sMAX_WEBGL_VERSION=2` unconditionally, which would have forced a WebGL 2 context even under this
  profile, contradicting the GLES 2.0 context `EasyGLGraphicsBackend` requests. Fixed to be
  conditional on `CNA_GRAPHICS_BACKEND` (`cmake/Examples.cmake`); verified via a real `emcc`
  `VERBOSE=1` build that `WEBGL1` now links with `MIN_WEBGL_VERSION=1`/`MAX_WEBGL_VERSION=1` and
  `WEBGL2` is unchanged at `2`/`2`.
- ✅ **No regression to `OPENGLES`/`OPENGL33`** — both re-verified against a real desktop Mesa GL
  context after the `AdaptGlslEs300ForActiveProfile()` signature change this work required;
  `BasicEffect` and `SkinnedEffect` golden-image tests still `[PASS]` on both.

## What's not yet done — the real, documented remaining gaps

- ⬜ **Real GLSL ES 1.00 driver/browser verification** — the transform's *output* was verified by
  hand (standalone harness) and the *C++ build pipeline* was verified for real (`emcc` compile +
  link), but no real WebGL 1 implementation (browser, or a headless-GL shim under Node) has ever
  actually compiled the generated GLSL text. This is a real, not-yet-closed gap — the standalone
  verification is strong evidence of correctness, not proof.
- ⬜ **`SkinnedEffect`/`SkinnedPbrEffect` do not work under `WEBGL1`** — their vertex shaders
  declare `layout(location=4) in uvec4 aBoneIndices;`. GLSL ES 1.00 vertex attributes must be
  float-based (`float`/`vecN`/`matN`) — there is no integer attribute type at all. The transform
  still runs (producing `attribute uvec4 aBoneIndices;`, which the ES 1.00 grammar itself
  rejects) but detects this and logs a specific, clear diagnostic
  (`"shader uses an integer vertex attribute type (uvec4/ivecN)..."`) rather than leaving only an
  opaque driver compile-error as the symptom. **A real fix needs**: encoding bone indices as a
  `vec4` (float) attribute instead, `int()`-casting each component when indexing `uBones[]` in the
  shader body, and updating the C++-side `VertexBuffer`/vertex-format upload path for this profile
  specifically to match (a real, separate architecture change — not attempted here).
- ⬜ **Full example/pixel-test suite** — only one example (`cna_house3d_demo`, `BasicEffect` only)
  was built under this profile. The `cna_test_easygl_*` GTest-based pixel-comparison suite cannot
  run under Emscripten at all regardless of GL profile (`cmake/Tests/EasyGLTests.cmake` gates it to
  `NOT EMSCRIPTEN`) — a pre-existing scope limit, not new to `WEBGL1`.
- ⬜ **CI/CTest identity** (`plan_glbackends.md` GLB-25, shared with `WEBGL2`) — no dedicated
  `WEBGL1`-only CTest registration.

## Relationship to the other 3 GL-family backends

See `plan_glbackends.md` §2's table and `docs/opengl33-backend.md`/`docs/webgl2-backend.md`'s own
versions of this section. In short: `OPENGLES` is today's original `EasyGL` public backend renamed
(GLES 3.0, native); `WEBGL2` is the same GLES 3.0 path under Emscripten; `OPENGL33` is a new
desktop GL 3.3 core-profile variant. `WEBGL1` is the only profile needing a real shader-body
rewrite (not just a header swap) and the only one with a known, currently-unsupported effect family
(`SkinnedEffect`/`SkinnedPbrEffect`).
