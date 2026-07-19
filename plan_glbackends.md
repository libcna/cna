# GL Backends Restructuring Plan — EasyGL becomes internal, 4 new public backends

> **Origin (2026-07-19):** project owner decision — `EasyGL` is a good internal implementation
> name but means nothing to a `libcna.com` user. Public backend selection should use names users
> already know: `OPENGLES`, `OPENGL33`, `WEBGL1`, `WEBGL2`. All four are implemented internally by
> the existing `EasyGL` code (`src/CNA/Internal/Backends/EasyGL/`, on top of the sibling `easy-gl`
> library) — this is **not** four independent backend implementations, it is one shared
> implementation family driven by a GL profile choice. See `../easygl_cna.md` for the original
> Czech discussion that produced this direction.
>
> **Decisions locked in for this plan (asked and answered 2026-07-19):**
> 1. Track work here first, as a plan document, before large-scale implementation (this project's
>    normal pattern for new backend-shaped initiatives — see `plan_webgpu.md`, `plan_dx3.md`).
> 2. `EASYGL` is **removed entirely** from the public `CNA_GRAPHICS_BACKEND` CMake selection (no
>    deprecated alias). Internal directory/target names (`Backends/EasyGL`,
>    `cna_backend_graphics_easygl`) stay as-is — only the public-facing selection string changes.
> 3. WebGL 1 support is genuinely missing in `easy-gl` today (analyzed in
>    `../easy-glrvc/webgl.md`) and **is in scope** for this plan, implemented directly in the
>    `easy-gl` repo. **Build against `../easy-glrvc` (branch `rvc`) temporarily**, not
>    `../easy-gl` — `cnagl`'s `CMakeLists.txt`/`BackendSelection.cmake` must
>    `add_subdirectory(../easy-glrvc easy-gl)` for the duration of this plan. Switch back to
>    `../easy-gl` once `easy-glrvc`'s WebGL1 work lands upstream (tracked as `GLB-30`).

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not
started.

---

## 1. Current state (verified 2026-07-19)

- `CNA_GRAPHICS_BACKEND=EASYGL` is today's only public GL-family backend. It hardcodes an
  **OpenGL ES 3.0** context (`SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
  SDL_GL_CONTEXT_PROFILE_ES)`, major 3 / minor 0,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:1297-1299`) and every embedded
  shader is written as `#version 300 es` (~25 shader source blocks in the same file).
- `EASYGL` is also picked as the *default* backend on Linux and Emscripten
  (`cmake/BackendSelection.cmake:1-10`) — i.e. today's "EasyGL on Emscripten" build is already,
  functionally, what this plan calls `WEBGL2` (GLES 3.0 maps 1:1 to WebGL 2.0). It has not been
  given its own selectable name or its own CI/test identity.
- `easy-gl` (the sibling library EasyGL calls into) is layered on `meta-gl`, which already has
  full `ApiKind::WebGL`/`webgl1`/`webgl2`/ANGLE detection (`meta-gl` `Context.hpp`/`Context.cpp`,
  confirmed present in `../meta-gl-followup-audit`). `easy-gl` itself does not use any of that yet
  — its own `ContextInfo::ApiKind` has only `Unknown/OpenGL/OpenGLES` (no `WebGL`), and
  `Device::initialize()` re-derives the GL version by hand-parsing `GL_VERSION` instead of calling
  `meta-gl`'s already-correct detection. Full gap list: `../easy-glrvc/webgl.md`.
- `easy-glrvc` (branch `rvc` of `easy-gl`) is a working checkout already prepared for this:
  `CMakeLists.txt` already points its `meta-gl` subdirectory at `../meta-gl-followup-audit`
  (uncommitted change), and `webgl.md` (untracked) is the WebGL1 gap analysis referenced above.
- GLSL ES 3.00 (`#version 300 es`) and desktop GLSL 3.30 core (`#version 330 core`) are close
  enough in body syntax (`in`/`out`, `texture()`, no `varying`/`attribute`) that **OPENGL33 does
  not need a second copy of every shader** — only the `#version`/`precision` header lines need to
  differ per profile. Confirmed by inspection of the shader bodies in
  `EasyGLGraphicsBackend.cpp`; verify this holds for all ~25 shaders during `GLB-20`, not just
  assumed.

## 2. Target public/internal architecture

```text
Public CNA_GRAPHICS_BACKEND values:      OPENGLES | OPENGL33 | WEBGL1 | WEBGL2
                                                 \      |        |      /
                                                  \     |        |     /
                                          BACKEND_DIR = src/CNA/Internal/Backends/EasyGL
                                          BACKEND_TARGET = cna_backend_graphics_easygl
                                                       |
                                          compiled with one of:
                                          CNA_GL_PROFILE_OPENGLES
                                          CNA_GL_PROFILE_OPENGL33
                                          CNA_GL_PROFILE_WEBGL1
                                          CNA_GL_PROFILE_WEBGL2
                                                       |
                                                easy-gl (../easy-glrvc during this plan)
                                                       |
                                                  meta-gl
```

| Public name | Context requested                    | Platform gate                    | Relationship to today |
|-------------|---------------------------------------|-----------------------------------|------------------------|
| `OPENGLES`  | GLES 3.0, desktop/mobile              | any non-Emscripten platform       | today's `EASYGL`, renamed, no behavior change |
| `OPENGL33`  | Desktop GL 3.3 core profile           | any non-Emscripten platform       | new — needs `SDL_GL_CONTEXT_PROFILE_CORE` + per-profile shader header |
| `WEBGL2`    | GLES 3.0 (maps to WebGL 2.0)          | Emscripten only                   | today's "EasyGL under emcmake", given its own name/gate/tests |
| `WEBGL1`    | GLES 2.0 (maps to WebGL 1.0)          | Emscripten only                   | new — needs easy-gl WebGL1 fixes (`GLB-30`-`GLB-35`) + `#version 100` shader header |

## 3. Tasks

### Phase A — CMake plumbing (rename public selection, no behavior change for OPENGLES/WEBGL2)

> **Status (2026-07-19): GLB-1 through GLB-8 done and verified; GLB-9 not started.**
> `-DCNA_GRAPHICS_BACKEND=OPENGLES`
> and `=OPENGL33` both configure cleanly against `../easy-glrvc` and print
> `"CNA: Using <NAME> graphics backend (internal implementation: EasyGL)"`;
> `cna_backend_graphics_easygl` builds cleanly under `OPENGLES` (confirms the new
> `#if defined(CNA_GL_PROFILE_...)` context-attribute branch in
> `EasyGLGraphicsBackend.cpp` compiles and the per-profile compile definition reaches the source
> file). `-DCNA_GRAPHICS_BACKEND=WEBGL2` on a native (non-Emscripten) configure correctly hits the
> new `FATAL_ERROR` gate. **Not yet verified:** an actual `emcmake` build for `WEBGL1`/`WEBGL2`
> (no Emscripten SDK in this sandbox), and `OPENGL33` was only configured, not built — it will
> fail to *build* until Phase B/D's shader header work lands, since every shader is still
> hardcoded `#version 300 es` (GLES-only), incompatible with the `SDL_GL_CONTEXT_PROFILE_CORE`
> context `OPENGL33` now requests. GLB-9's Emscripten linker-flag conditionalization not started
> (needs a real Emscripten toolchain to verify against).
> `CMakePresets.json` also updated (`web` preset → `WEBGL2`, sanitizer/`tests` presets → `OPENGLES`).

- ✅ **GLB-1** — In `cmake/BackendSelection.cmake`, replace the `EASYGL` entry in
  `CNA_GRAPHICS_BACKEND`'s `STRINGS` property and default-backend logic with `OPENGLES`,
  `OPENGL33`, `WEBGL1`, `WEBGL2`. Default stays "GL-family on Linux/Emscripten": Linux →
  `OPENGLES`, Emscripten → `WEBGL2`, everything else → `SDL_RENDERER` (unchanged).
- ✅ **GLB-2** — Replace `CNA_BACKEND_EASY_GL` option with four options
  (`CNA_BACKEND_OPENGLES`/`CNA_BACKEND_OPENGL33`/`CNA_BACKEND_WEBGL1`/`CNA_BACKEND_WEBGL2`) in the
  same file's explicit-selection block (mirrors the existing `CNA_BACKEND_D3D11`-style options).
- ✅ **GLB-3** — In `cmake/BackendSelection.cmake`'s per-backend `elseif` chain
  (today's `EASYGL` branch, ~line 161), replace with four branches, each setting
  `BACKEND_DIR=src/CNA/Internal/Backends/EasyGL`, `BACKEND_TARGET=cna_backend_graphics_easygl`,
  `CNA_BACKEND_DEFINE=CNA_BACKEND_EASYGL` (internal identity unchanged — existing
  `#ifdef CNA_BACKEND_EASYGL` guards elsewhere in the codebase keep working unmodified), **plus** a
  new second compile definition selecting the GL profile
  (`CNA_GL_PROFILE_OPENGLES`/`_OPENGL33`/`_WEBGL1`/`_WEBGL2`). Add the Emscripten-only gate for
  `WEBGL1`/`WEBGL2` (`message(FATAL_ERROR ...)` if not `EMSCRIPTEN`, mirroring the existing
  `CANVAS`-is-Emscripten-only check at `BackendSelection.cmake:105`) and the
  not-Emscripten gate for `OPENGLES`/`OPENGL33`.
- ✅ **GLB-4** — Update `cmake/BackendLibraries.cmake`'s `EASYGL` branch (~line 71) the same way —
  four `elseif` arms, all pointing at the same `cna_backend_graphics_easygl` target/link setup.
- ✅ **GLB-5** — Update `cmake/Examples.cmake` (line ~278, `CNA_GRAPHICS_BACKEND STREQUAL
  "EASYGL"`) and `cmake/Harnesses.cmake` to check all four new values instead.
- ✅ **GLB-6** — Rename `cmake/Tests/EasyGLTests.cmake`'s guard condition (and decide: keep as one
  file gated on "any of the 4 GL backends", or split per-backend — recommend keeping one file for
  now since the underlying test binaries are backend-agnostic GTest suites already parametrized by
  `CNA_GRAPHICS_BACKEND`; just widen the `STREQUAL "EASYGL"` check to an `OR` across the 4 names).
- ✅ **GLB-7** — Update the sibling-repo existence/clone-instructions check (today's
  `BackendSelection.cmake:112-129`, `../easy-gl/CMakeLists.txt` check) to point at
  `../easy-glrvc` per the locked-in decision above, with a code comment explaining this is
  temporary (references `GLB-30`).
- ✅ **GLB-8** — Add `#if defined(CNA_GL_PROFILE_OPENGL33) / _OPENGLES / _WEBGL1 / _WEBGL2` to
  `EasyGLGraphicsBackend.cpp`'s context-creation code (today's hardcoded
  `SDL_GL_CONTEXT_PROFILE_ES` block, lines ~1297-1299): `OPENGLES`/`WEBGL2` keep today's GLES 3.0
  request; `OPENGL33` requests `SDL_GL_CONTEXT_PROFILE_CORE` major 3 minor 3; `WEBGL1` requests
  GLES 2.0 (Emscripten maps this to a WebGL 1 context automatically via
  `-s USE_WEBGL2=0` / `-s FULL_ES2=1`, confirm exact emcc flags during GLB-9).
- ⬜ **GLB-9** — Confirm/adjust Emscripten linker flags per profile (`WEBGL1` needs
  `-s USE_WEBGL2=0`, `WEBGL2` needs `-s USE_WEBGL2=1 -s FULL_ES3=1` or equivalent — check current
  emcc flags in `cmake/` for the existing Emscripten EasyGL path first, they may already be
  WebGL2-shaped and just need to become conditional on the new profile).

### Phase B — Shared shader header, not shader duplication

- ⬜ **GLB-10** — Verify the "shared shader body" assumption from §1 across all ~25 shader blocks
  in `EasyGLGraphicsBackend.cpp` (grep `#version 300 es`). Note any shader whose *body* (not just
  header) is GLES-specific (e.g. relies on default `mediump` precision behavior a desktop core
  profile shader must declare explicitly, or vice versa) — those need real per-profile bodies, not
  just header swaps.
- ⬜ **GLB-11** — Introduce a small helper (e.g. `static const char* GlShaderHeader(GlProfile)`)
  that returns the right `#version`/`precision` preamble per active profile
  (`300 es`+`precision mediump float;` for `OPENGLES`/`WEBGL2`, `330 core` with no precision
  qualifiers for `OPENGL33`, `100`+`precision mediump float;` for `WEBGL1` — note GLSL ES 1.00 also
  needs `attribute`/`varying` instead of `in`/`out`, a real body difference, not just a header
  swap: scope this explicitly in `GLB-12`, don't assume it's free).
- ⬜ **GLB-12** — For `WEBGL1` specifically: GLSL ES 1.00 (`#version 100`) uses `attribute`/
  `varying`/`texture2D()`/`textureCube()` instead of `in`/`out`/`texture()` — a real syntax
  difference from the ES 3.00/330-core shader bodies, not header-only. Decide and implement: either
  (a) keep one shader body written against GLSL ES 1.00-compatible subset syntax for all 4
  profiles when possible (desktop 330 core and ES 3.00 both still accept `attribute`/`varying` as
  compatibility... **no they don't**, core profile forbids them) — so realistically WEBGL1 needs
  its own real header+body preamble per shader (macro-based: `#define in attribute` etc. is a known
  working pattern, consider it) rather than three-out-of-four sharing.

### Phase C — `OPENGLES` (rename only)

- ⬜ **GLB-13** — Confirm `OPENGLES` profile produces byte-identical behavior to today's `EASYGL`
  (same context flags, same shader headers) — this task should require zero shader changes, only
  the CMake renaming from Phase A.
- ⬜ **GLB-14** — Update `cmake/Tests/EasyGLTests.cmake` test registration / CTest labels and
  `examples/easygl_*.cpp` file naming convention decision: keep `easygl_*` prefix (internal name,
  still accurate — these tests exercise the EasyGL implementation regardless of which public
  profile they're compiled under) or rename to `opengles_*`. **Ask the project owner** before doing
  a mechanical rename of ~50 example files — low value, high diff noise, not worth doing without
  explicit sign-off.
- ⬜ **GLB-15** — Update `README.md`'s backend table entry (today: "`EASYGL` backend... Most mature
  backend overall") to describe the 4 public names with the "shared internal implementation" framing
  from `../easygl_cna.md`'s own suggested capability-matrix wording.
- ⬜ **GLB-16** — Update `docs/` references to `EASYGL`/`CNA_GRAPHICS_BACKEND=EASYGL` (grep first;
  likely `docs/xna-4-api-coverage.md`, `docs/graphics-compatibility-report.md`, any backend-list
  doc) to the new 4 names, keeping "internal EasyGL implementation" as an explanatory footnote
  rather than removing the term everywhere (users debugging a stack trace still see `EasyGL` symbol
  names).

### Phase D — `OPENGL33` (new desktop core-profile backend)

- ⬜ **GLB-20** — Implement `GLB-11`/`GLB-12`'s header-swap mechanism for the `OPENGL33` profile
  specifically (closest to today's ES path syntactically — likely just `in`/`out`/`texture()` all
  still valid, only `#version`/precision-qualifier differences). Build and smoke-test one simple
  scene (e.g. `easygl_textured_quad_test`) under `-DCNA_GRAPHICS_BACKEND=OPENGL33` before touching
  the rest.
- ⬜ **GLB-21** — Work through remaining shaders one by one, fixing any real GLES-only construct
  found in `GLB-10`'s survey.
- ⬜ **GLB-22** — Run the full EasyGL-family GTest suite under `OPENGL33` and record pass/fail
  (expect this to be the fastest-converging of the 2 new profiles — desktop GL 3.3 is the most
  forgiving/mature driver target).
- ⬜ **GLB-23** — `docs/opengl33-backend.md` — new doc, same shape as `docs/webgpu-backend.md`/
  `docs/dx3-backend.md`: what's verified, what's deferred, how it differs from `OPENGLES`.

### Phase E — `WEBGL2` (give today's Emscripten/EasyGL path its own identity)

- ⬜ **GLB-24** — Get a clean `emcmake`/`emcc` configure+build with
  `-DCNA_GRAPHICS_BACKEND=WEBGL2` (should need zero source changes beyond Phase A's CMake
  plumbing, since this is today's Emscripten+EasyGL path under a new name).
  Follow the same "compiles under node, not yet browser-pixel-verified" standard the `CANVAS`
  backend already documents (`README.md`'s `CANVAS` bullet) — don't overclaim.
- ⬜ **GLB-25** — Add a `cmake/Tests/WebGl2Tests.cmake` (or extend `EasyGLTests.cmake`'s guard) so
  CI/CTest can distinguish `WEBGL2` runs from `OPENGLES`/`OPENGL33` runs.
- ⬜ **GLB-26** — `docs/webgl2-backend.md`.

### Phase F — `WEBGL1` (new, needs easy-gl fixes first)

Ordered per `../easy-glrvc/webgl.md`'s own "what could be done" list (items 1-6 in that doc);
task numbers below map 1:1 to that list. **All of GLB-30 through GLB-35 are `easy-gl` repo work**
(on `easy-glrvc`, branch `rvc`), not `cnagl` work.

- ⬜ **GLB-30** — `easy-gl`: replace `Device::initialize()`'s manual `GL_VERSION`-string parsing
  with a call to `meta-gl`'s existing `GetContextInfo()`/`GetCapabilities()` (which already has
  correct `ApiKind::WebGL`/`webgl1`/`webgl2` detection). Removes the "accidentally correct because
  Emscripten's `GL_VERSION` string happens to parse right" fragility noted in `webgl.md` item 1.
- ⬜ **GLB-31** — `easy-gl`: add `ApiKind::WebGL` to `include/easygl/ContextInfo.hpp` (currently
  only `Unknown/OpenGL/OpenGLES`), surfaced from `meta-gl`'s already-correct detection (`webgl.md`
  item 1 + item 5).
- ⬜ **GLB-32** — `easy-gl`: add `Feature`/`require()` gates to `Query`, `Sampler`,
  `TransformFeedback`, `ProgramPipeline`, `Sync`, and any other ES-3.x+-only call site currently
  called unconditionally (`webgl.md` item 2) — must raise a clear exception on a WebGL1/GLES2
  context instead of crashing on a null function pointer. This is the highest-risk item on the list
  (touches 5 classes); budget the most review time here.
- ⬜ **GLB-33** — `easy-gl`: document `ProgramPipeline` as permanently unavailable on any WebGL
  context (`webgl.md` item 3) — not a WebGL1-vs-2 distinction, a hard browser-GL limitation.
- ⬜ **GLB-34** — `easy-gl`: make `Device::initialize()`'s `Feature::VertexArrayObject` requirement
  explicit about depending on `OES_vertex_array_object` on WebGL1/GLES2 contexts (`webgl.md` item
  4), rather than an implicit assumption.
- ⬜ **GLB-35** — `easy-gl`: add an Emscripten CMake preset + a minimal WebGL1 smoke
  example/test (`webgl.md` item 6) — this is what actually proves `GLB-30`-`GLB-34`, not just code
  review. Needs a real `emcmake` build with `-s USE_WEBGL2=0`.
- ⬜ **GLB-36** — Back in `cnagl`: once `GLB-30`-`GLB-35` land, implement `GLB-12`'s
  `attribute`/`varying`/`texture2D()` GLSL ES 1.00 shader header+body variant in
  `EasyGLGraphicsBackend.cpp` for the `WEBGL1` profile.
- ⬜ **GLB-37** — Full EasyGL-family GTest suite under `-DCNA_GRAPHICS_BACKEND=WEBGL1` via
  `emcmake`, `docs/webgl1-backend.md`.
- ⬜ **GLB-38** — Once `easy-glrvc`'s `GLB-30`-`GLB-35` changes are reviewed and merged to
  `easy-gl`'s real `main`/default branch, switch `cnagl`'s sibling-repo dependency in
  `cmake/BackendSelection.cmake` back from `../easy-glrvc` to `../easy-gl` (reverts `GLB-7`'s
  temporary redirect) and delete/archive `easy-glrvc` per the project owner's normal sibling-repo
  cleanup convention.

## 4. Open questions for the project owner (do not guess — ask before these tasks)

- `GLB-14`: rename `examples/easygl_*.cpp` → `examples/opengles_*.cpp` (and similar) or keep the
  `easygl_` prefix as an internal-implementation-name convention? ~50 files affected either way.
- Whether `OPENGLES` should default to GLES 3.0 (today's behavior, kept in this plan) or whether a
  true "OpenGL ES" public name should support ES 2.0 as well — this plan assumes ES 3.0-only
  scope for `OPENGLES` (matching current behavior) and treats ES2-class support as exclusively the
  `WEBGL1` profile's problem, since no current CNA use case needs desktop/mobile ES2.
