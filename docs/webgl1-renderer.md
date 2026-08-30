# WEBGL1 (Emscripten, GLSL ES 1.00 → WebGL 1.0) Renderer — Status

`WEBGL1` is one of the public GL-family `CNA_GRAPHICS_RENDERER` values (the original 4 were
introduced by `plans/plan_glbackends.md`; the Phase-2 expansion later added `OPENGLES2`, this
profile's native twin — see `docs/opengles2-renderer.md`) — it shares its entire implementation
with `OPENGLES2`/`OPENGLES3`/`OPENGL33`/`WEBGL2`
(`modules/renderers/easygl/`, on top of the sibling `easy-gl` library), distinguished at
compile time by the `CNA_GL_PROFILE_WEBGL1` definition. Unlike the ES 3.0-class profiles (all
GLSL ES 3.00 / desktop GLSL 3.30 core, close enough in body syntax to share one shader source),
`WEBGL1` requests a GLES 2.0-shaped context (mapping to a real WebGL 1.0 context under Emscripten)
and needs a real GLSL ES 1.00 shader rewrite, not just a header swap.

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not done.

## What's real today

- ✅ **Context creation** — `EasyGLRenderer`'s constructor requests GLES 2.0
  (`SDL_GL_CONTEXT_MAJOR_VERSION=2`) for this profile (`plans/plan_glbackends.md` GLB-8). Confirmed via
  the vendored SDL3 Emscripten renderer that this correctly produces a real WebGL 1 context (not
  WebGL 2) — see `docs/webgl2-renderer.md`'s GLB-9 note for the mechanism.
- ✅ **Shader body rewrite** (`plans/plan_glbackends.md` GLB-10/11/12/36) — every embedded shader in
  `EasyGLRenderer.cpp` is authored once against GLSL ES 3.00; `AdaptGlslEs300ForActiveProfile()`'s
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
  `EasyGLSpriteBatchRenderer::InitializeResources()` rebind those exact same numeric locations via
  `Program::bind_attrib_location()` before linking (`WEBGL1` only). Every existing
  `VertexArray`/VAO attribute-binding call site elsewhere in this file (all hardcoded numeric
  indices, e.g. `vao.enable_attribute(0)`) needed zero changes.
- ✅ **Standalone string-transform verification** — before attempting any GL/Emscripten build, the
  transform functions were extracted into an isolated, regular-`g++`-compiled test harness (no GL,
  no Emscripten) and run against real shader strings copied from this file — `SpriteBatch` (spaced
  `layout(location = N)`), `EnvironmentMapEffect` (has the one `samplerCube`), and `SkinnedEffect`
  (has the one `uvec4` attribute). Every output line was manually inspected and confirmed correct
  before moving on to a real build.
- ✅ **Real `emcmake`/`emcc` build** — `-DCNA_GRAPHICS_RENDERER=WEBGL1` configures cleanly and
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
  profile, contradicting the GLES 2.0 context `EasyGLRenderer` requests. Fixed to be
  conditional on `CNA_GRAPHICS_RENDERER`; the policy now lives in one renderer-aware helper shared
  by the examples, benchmark and `cna_c_api_wasm`. Clean C-API builds and real Chrome probes verify
  `WEBGL1` at `MIN/MAX=1/1` and a WebGL 1.0 context, while `WEBGL2` remains `2/2`.
- ✅ **No regression to `OPENGLES3`/`OPENGL33`** — both re-verified against a real desktop Mesa GL
  context after the `AdaptGlslEs300ForActiveProfile()` signature change this work required;
  `BasicEffect` and `SkinnedEffect` golden-image tests still `[PASS]` on both.
- ✅ **`SkinnedEffect`/`SkinnedPbrEffect` now supported under `WEBGL1`** — fixed as a same-plan
  follow-up (originally left as a known gap, then investigated further and found narrower than
  expected). `layout(location=N) in uvec4 aBoneIndices;` (GLSL ES 1.00 has no integer vertex
  attribute type at all) is now rewritten to `attribute vec4 aBoneIndices;`, with its 4
  `uBones[aBoneIndices.x/y/z/w]` index expressions rewritten to `uBones[int(aBoneIndices.x)]`/etc.
  (`RewriteBoneIndicesForEs100()`). On the C++ side, `DescribeVertexElementFormat()`'s
  `VertexElementFormat::Byte4` case (`BLENDINDICES`'s only user) reads the identical underlying
  `UnsignedByte` bytes as floats under this profile instead of as a true integer — **no
  `VertexBuffer` upload-path change needed at all**, only the attribute-pointer read mode at its 2
  call sites, `#ifdef`-gated to `WEBGL1` only. Verified: a standalone C++ test harness confirmed the
  real `SkinnedEffect` vertex shader transforms correctly (no `uvec4` remains); no regression on
  `OPENGLES3`/`OPENGL33` (`cna_test_easygl_skinnedeffect_golden`/`skinnedpbreffect_golden`/
  `cna_test_skinned_effect`/`cna_test_skinned_integration` all still exit 0, real Mesa GL); a real
  `emcmake`/`emcc` build compiles and links cleanly, exercising the new `#ifdef` branch for real.
  **Actual GLSL ES 1.00 driver acceptance of the skinned shaders is still not verified** — same
  browser-only limitation as everything else in this doc.

## What's not yet done — the real, documented remaining gaps

- ⬜ **Real GLSL ES 1.00 driver/browser verification** — the transform's *output* was verified by
  hand (standalone harness) and the *C++ build pipeline* was verified for real (`emcc` compile +
  link), but no real WebGL 1 implementation (browser, or a headless-GL shim under Node) has ever
  actually compiled the generated GLSL text. This is a real, not-yet-closed gap — the standalone
  verification is strong evidence of correctness, not proof. This now also covers the
  `SkinnedEffect`/`SkinnedPbrEffect` fix above — its GLSL output was verified by hand and by a
  successful `emcc` compile, not by an actual WebGL 1 driver.
- ⬜ **Full example/pixel-test suite** — only one example (`cna_house3d_demo`, `BasicEffect` only)
  was built under this profile. The `cna_test_easygl_*` GTest-based pixel-comparison suite cannot
  run under Emscripten at all regardless of GL profile (`cmake/Tests/EasyGLTests.cmake` gates it to
  `NOT EMSCRIPTEN`) — a pre-existing scope limit, not new to `WEBGL1`.
- ⬜ **CI/CTest identity** (`plans/plan_glbackends.md` GLB-25, shared with `WEBGL2`) — no dedicated
  `WEBGL1`-only CTest registration.

## Relationship to the other 3 GL-family renderers

See `plans/plan_glbackends.md` §2's table and `docs/opengl33-renderer.md`/`docs/webgl2-renderer.md`'s own
versions of this section. In short: `OPENGLES3` is today's original `EasyGL` public renderer renamed
(GLES 3.0, native); `WEBGL2` is the same GLES 3.0 path under Emscripten; `OPENGL33` is a new
desktop GL 3.3 core-profile variant. `WEBGL1` is the only profile needing a real shader-body
rewrite (not just a header swap) — all ~26 stock effect shaders, including `SkinnedEffect`/
`SkinnedPbrEffect`, now have that rewrite implemented and standalone-verified; none of the 4
profiles has real browser-level WebGL/GLSL driver acceptance verified in this sandbox.
