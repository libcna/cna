# GL Backends Restructuring Plan — EasyGL becomes internal, 4 new public backends

> **TL;DR for a clean-context morning read (2026-07-20, overnight session):** All 4 public
> backends (`OPENGLES`, `OPENGL33`, `WEBGL1`, `WEBGL2`) exist, build, and are verified as far as
> this sandbox allows. `OPENGLES`/`OPENGL33` are real-driver-verified on Linux (Mesa) — `OPENGL33`
> ran its **full 241-binary example suite for real**: 235 pass, 6 fail (5 pre-existing/unrelated,
> 1 real-but-expected — see Phase D). `WEBGL1`/`WEBGL2` build and link for real via a genuine
> `emcmake`/`emcc` toolchain found in this sandbox at `~/emsdk` (not on `PATH` by default) and run
> under Node up to a real browser-only DOM API limit (no way to verify actual GL rendering without
> a real browser here). **3 real bugs were found and fixed tonight**, not just the planned
> restructuring: `GLB-17` (startup log printed the old internal `EASYGL` name), `GLB-39` (a real
> Emscripten link failure, wrong exception-handling ABI), and `GLB-9`-revisited (a hardcoded
> `MIN/MAX_WEBGL_VERSION=2` that would have silently forced `WEBGL1` into a `WEBGL2` context).
> **Deliberately not done, by explicit decision** (see §4): `GLB-14` (no example-file rename) and
> `GLB-38` (no autonomous repo merge/push). Everything is committed on `feature/gl`; read the
> phase-by-phase detail below for exactly what each task verified and how.
>
> **2026-07-20 morning update:** the `SkinnedEffect`/`SkinnedPbrEffect` `uvec4`-bone-index gap
> mentioned above as "needing a go-ahead" — go-ahead given, **now done** (turned out narrower than
> estimated: no `VertexBuffer` upload-path change needed, just a `WEBGL1`-only attribute-read-mode
> branch plus the shader rewrite). `GLB-40` (silent custom `ShaderEffect` compile failures under
> `OPENGL33`) also authorized and investigated. See Phase F's `GLB-36` status and the dedicated
> `GLB-40` note for detail.
>
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

> **2026-07-19, later same day — another agent's commits landed on this branch (`feature/gl`)
> after `GLB-1`-`GLB-8`**: `ff1f3087` (EasyGL adapted to an unrelated upstream texture-API change,
> no overlap with the context-creation code this plan touches), `5aa6b348` (new configurable
> `Unsupported3DCallBehavior` policy, backend-agnostic, no overlap), `98693409` (backend-name
> startup log — **exposed a real gap**, tracked as `GLB-17` above), `8521c0ad`/`6610b88f` (NEXT.md
> doc refresh only). None conflict with this plan's Phase A changes; `GLB-17` is the one concrete
> follow-up they created.

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

> **Superseded in one respect by `plan_runtimerenderer.md` phase P11 (2026-08-15): the GL profile is
> no longer a compile-time choice.** The architecture below is otherwise unchanged and its
> invariants still hold — one implementation family serving several public identities, one shader
> corpus adapted per profile, the Emscripten-only gate on the WebGL pair.
>
> What changed: `CNA_GL_PROFILE_*` was a compile definition, so the same translation units would
> have had to be compiled twice for two profiles to coexist — an ODR violation, which is why
> `cmake/RendererCombinations.cmake` used to reject that combination outright. The profile is now a
> runtime value (`CNA::Internal::Renderers::EasyGL::GlProfile`, passed to `EasyGLRenderer`'s
> constructor and published as the thread's active profile), so all five identities can be compiled
> into one binary and chosen at runtime. That rejection rule is deleted.
>
> Verified on `OPENGLES3;OPENGLES2;OPENGL33;SOFTWARE;HEADLESS`: 6385 tests passed, and the profiles
> create genuinely different contexts — `OPENGL33` reports "OpenGL 4.6 (Core Profile)" where the ES
> profiles report an ES context, from the same binary.
>
> The one compile definition that remains is `kCompileTimeGlProfile`, which supplies a
> single-renderer build's default so nothing else had to change.


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

> **Status: ALL of GLB-1 through GLB-9 done and verified** (superseded this stale note, which
> originally said GLB-9 was blocked on "no Emscripten SDK" — that assumption was wrong, see
> Phase E's status note for the real SDK location and GLB-9's actual resolution). `OPENGLES`,
> `OPENGL33`, `WEBGL1`, `WEBGL2` all configure and build correctly; see each phase's own status
> note below for what's verified for each specific profile. `CMakePresets.json` also updated
> (`web` preset → `WEBGL2`, sanitizer/`tests` presets → `OPENGLES`).

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
- ✅ **GLB-9** — Confirm/adjust Emscripten linker flags per profile. **Resolved: no linker-flag
  change needed at all.** Read the actual vendored SDL3 source
  (`third_party/SDL/src/video/emscripten/SDL_emscriptenopengles.c:87-98`,
  `Emscripten_GLES_CreateContext`): SDL3's Emscripten backend calls
  `emscripten_webgl_create_context()` directly (the native context-attributes API, not the
  `-s USE_WEBGL2`/`FULL_ES2`/`FULL_ES3` GLES-emulation-shim flags, which only matter for a
  different, unused code path) and derives WebGL1-vs-WebGL2 purely from whether
  `_this->gl_config.major_version == 3` (bumps `attribs.majorVersion` from its default of 1 to 2).
  `GLB-8`'s `SDL_GL_CONTEXT_MAJOR_VERSION` already set per profile (2 for `WEBGL1`, 3 for
  `WEBGL2`/`OPENGLES`) is therefore already sufficient — no `emcc`/CMake link-flag work required.
  **Verified against a real `emcmake` build** (this sandbox does have a working Emscripten SDK at
  `~/emsdk`, not on `PATH` by default but fully functional once sourced — corrects this plan's
  earlier "no Emscripten SDK available" assumption): `-DCNA_GRAPHICS_BACKEND=WEBGL2` configures
  cleanly and the `CNA` static library target compiles cleanly to completion. A full-binary link
  (`cna_house3d_demo`) surfaced a **separate, real, pre-existing blocker** unrelated to this
  finding — see Phase E's status note.

### Phase B — Shared shader header, not shader duplication

> **Status (2026-07-19): GLB-10/GLB-11 done and verified; GLB-12 (WEBGL1 body) still open,
> intentionally deferred.** Added `AdaptGlslEs300ForActiveProfile()` in
> `EasyGLGraphicsBackend.cpp` (rewrites `#version 300 es` → `#version 330 core` and drops the
> following `precision ... float;` line for `CNA_GL_PROFILE_OPENGL33`; passes through unchanged
> for `OPENGLES`/`WEBGL2`), wired into `CompileAndLink()` (24 of 26 shader blocks) and
> `EasyGLSpriteBatchBackend::InitializeResources()`'s two raw-string shaders (the only other call
> sites — confirmed via grep, no shader source bypasses `CompileAndLink`). **Real verification**,
> not just "it compiles": built and ran 7 shader-exercising examples (BasicEffect,
> DualTextureEffect, SkinnedEffect, SkinnedPbrEffect, AlphaTestEffect, EnvironmentMapEffect,
> SpriteBatch) under `-DCNA_GRAPHICS_BACKEND=OPENGL33` against a real desktop Mesa 4.5 core-profile
> GL context (`DISPLAY=:99` Xvfb) — all exit 0, zero shader/link errors, and every golden
> pixel-comparison check `[PASS]`es byte-identical to the `OPENGLES`/GLES3 reference images. This
> also empirically answers §1's open question about inline `highp`/`mediump` qualifiers scattered
> in a few shader bodies (`uniform highp vec4 uDiffuseColor;` etc., outside the stripped
> `precision` line) — Mesa's desktop core-profile compiler accepts them as no-ops without any
> extra stripping needed, so `GLB-10`'s survey concludes no shader body (beyond the WEBGL1 case
> below) needs real per-profile content, only the header two lines.

- ✅ **GLB-10** — Verify the "shared shader body" assumption from §1 across all ~25 shader blocks
  in `EasyGLGraphicsBackend.cpp` (grep `#version 300 es`). Note any shader whose *body* (not just
  header) is GLES-specific (e.g. relies on default `mediump` precision behavior a desktop core
  profile shader must declare explicitly, or vice versa) — those need real per-profile bodies, not
  just header swaps.
- ✅ **GLB-11** — Introduce a small helper (e.g. `static const char* GlShaderHeader(GlProfile)`)
  that returns the right `#version`/`precision` preamble per active profile
  (`300 es`+`precision mediump float;` for `OPENGLES`/`WEBGL2`, `330 core` with no precision
  qualifiers for `OPENGL33`, `100`+`precision mediump float;` for `WEBGL1` — note GLSL ES 1.00 also
  needs `attribute`/`varying` instead of `in`/`out`, a real body difference, not just a header
  swap: scope this explicitly in `GLB-12`, don't assume it's free).
- ✅ **GLB-12** — Resolved as part of `GLB-36` (see Phase F below) — implemented a real
  text-transform (`TransformGlslEs300BodyToEs100()`), not a macro-based `#define in attribute`
  trick. WEBGL1 gets its own real per-shader body rewrite at first-use time, not shared with the
  other 3 profiles.

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
- ✅ **GLB-15** — Update `README.md`'s backend table entry (today: "`EASYGL` backend... Most mature
  backend overall") to describe the 4 public names with the "shared internal implementation" framing
  from `../easygl_cna.md`'s own suggested capability-matrix wording.
- ✅ **GLB-16** — Update `docs/` references to `EASYGL`/`CNA_GRAPHICS_BACKEND=EASYGL` (grep first;
  likely `docs/xna-4-api-coverage.md`, `docs/graphics-compatibility-report.md`, any backend-list
  doc) to the new 4 names, keeping "internal EasyGL implementation" as an explanatory footnote
  rather than removing the term everywhere (users debugging a stack trace still see `EasyGL` symbol
  names).
- ✅ **GLB-17** — **New, found 2026-07-19** (see `NEXT.md` commit `98693409`, "feat: report
  graphics backend at startup", landed on this branch after `GLB-1`-`GLB-8`): `GraphicsDevice`
  now logs `"CNA: graphics backend: <name>"` on first backend creation via
  `CNA::getCurrentGraphicsBackendName()` (`include/CNA/GraphicsBackendType.hpp`). That function
  still only knows the internal identity (`#elif defined(CNA_BACKEND_EASYGL) ... return
  "EASYGL";`) — it has no awareness of the new `CNA_GL_PROFILE_OPENGLES`/`_OPENGL33`/`_WEBGL1`/
  `_WEBGL2` compile definitions this plan added. **Concrete bug this causes**: a build configured
  with `-DCNA_GRAPHICS_BACKEND=OPENGL33` (or `WEBGL1`/`WEBGL2`) prints `"CNA: graphics backend:
  EASYGL"` at startup — directly undermining this plan's whole premise that `EASYGL` is not a
  name public users should see. Fix: add a `GraphicsBackendType::OpenGLES/OpenGL33/WebGL1/WebGL2`
  split (or a second `#if defined(CNA_GL_PROFILE_...)` layer inside the existing `EasyGL` case)
  so `getCurrentGraphicsBackendName()` returns the actual public name selected in
  `cmake/BackendSelection.cmake`, not the internal one. Also update
  `GraphicsBackendTypeTests.cpp`'s static asserts/tests, which currently assume the compile-time
  name equals the `CNA_BACKEND_*` define name 1:1.

### Phase D — `OPENGL33` (new desktop core-profile backend)

> **Status: GLB-20/GLB-21/GLB-22 all done.** The `GLB-11` header-swap mechanism turned out to be
> exactly what `OPENGL33` needed (no shader in `EasyGLGraphicsBackend.cpp` had a real GLES-only
> body construct beyond the header two lines — see Phase B's status note for the empirical
> `highp`/`mediump` finding). **`GLB-22` upgraded from a 7-binary sample to the full registered
> suite, all 241 `cna_easygl_test` binaries, built and run for real**: originally `235/241 pass,
> 0 crash, 6 fail`. 5 of the 6 failures were confirmed pre-existing (identical failure under
> `OPENGLES`, including 2 already-documented bugs, `plan_graphics.md` Tasks 1115/872). The 6th
> (`cna_test_easygl_shipgame_particle_shader`) was real and `OPENGL33`-specific — root-caused
> (missing `glEnable(GL_VERTEX_PROGRAM_POINT_SIZE)` for `gl_PointSize`-driven point-sprite
> rendering, **not** the shader-compile-failure explanation first guessed here, which was verified
> wrong) and fixed as `GLB-40`. **Final re-verified count: `236/241 pass, 0 crash, 5 fail`**, the
> same 5 pre-existing failures, zero new regressions. Full detail in `docs/opengl33-backend.md` and
> the `GLB-40` entry below.
- ✅ **GLB-20** — Implement `GLB-11`/`GLB-12`'s header-swap mechanism for the `OPENGL33` profile
  specifically (closest to today's ES path syntactically — likely just `in`/`out`/`texture()` all
  still valid, only `#version`/precision-qualifier differences). Build and smoke-test one simple
  scene (e.g. `easygl_textured_quad_test`) under `-DCNA_GRAPHICS_BACKEND=OPENGL33` before touching
  the rest.
- ✅ **GLB-21** — Work through remaining shaders one by one, fixing any real GLES-only construct
  found in `GLB-10`'s survey.
- ✅ **GLB-22** — Run the full EasyGL-family GTest suite under `OPENGL33` and record pass/fail —
  **done: 236/241 (final, post-GLB-40), see status note above and `docs/opengl33-backend.md` for
  the full breakdown.**
- ✅ **GLB-23** — `docs/opengl33-backend.md` — new doc, same shape as `docs/webgpu-backend.md`/
  `docs/dx3-backend.md`: what's verified, what's deferred, how it differs from `OPENGLES`.

### Phase E — `WEBGL2` (give today's Emscripten/EasyGL path its own identity)

> **Status (2026-07-19, updated later same night): GLB-24 done and verified; GLB-39 fixed.** A
> working Emscripten SDK exists in this sandbox at `~/emsdk` (source `~/emsdk/emsdk_env.sh`, plus
> manually add `~/emsdk/upstream/emscripten` and the bundled `~/emsdk/node/.../bin` to `PATH` —
> `emsdk list --installed` reports nothing activated, which is misleading; the toolchain works
> when invoked directly). `emcmake cmake -DCNA_GRAPHICS_BACKEND=WEBGL2` configures cleanly.
>
> Building the real, already-Emscripten-gated `cna_house3d_demo` target first surfaced a genuine
> link failure (`wasm-ld: undefined symbol: __cpp_exception` from `libmeta-gl.a`). **Root-caused
> and fixed as `GLB-39`, entirely within `cnagl`** (no sibling-repo change needed): CNA's own
> top-level `CMakeLists.txt` set legacy JS-based `-fexceptions` for Emscripten, while
> `easy-gl`'s `cmake/EasyGlPlatform.cmake` sets the modern Wasm-EH `-fwasm-exceptions` in its own
> directory scope (for its standalone Emscripten preset) — directory-scoped CMake compile/link
> options only propagate downward through `add_subdirectory()`, so `meta-gl`/`easy-gl` objects got
> compiled for Wasm EH while CNA's own final-link step (defined in CNA's own top-level scope) was
> still configured for legacy JS EH, producing the ABI mismatch. Matched CNA's own flag to
> `-fwasm-exceptions` (`CMakeLists.txt`, commit `ec58f383`). **After the fix**:
> `cna_house3d_demo` under `WEBGL2` links cleanly, producing a real `.wasm`/`.js`/`.html`; running
> it under Node executes real C++ code through SDL3's window-creation path before hitting the
> expected `window is not defined` (a genuine browser-only DOM API gap, already documented for the
> `CANVAS` backend — not a new bug). Checked for regressions: `cna_demo_2d` under `CANVAS` (a
> previously-verified-working Emscripten configuration) still builds and links cleanly with the
> new flag.
>
> `CnaTests` was also attempted under both `WEBGL2` and `CANVAS` and fails with an unrelated
> `'SDL3/SDL.h' file not found` in both — but this is not a regression: the project's own `web`
> CMake preset already sets `CNA_BUILD_TESTS=OFF`, meaning a test build under Emscripten was never
> a supported/exercised configuration to begin with, pre-existing this plan entirely.
- ✅ **GLB-24** — Get a clean `emcmake`/`emcc` configure+build with
  `-DCNA_GRAPHICS_BACKEND=WEBGL2` (should need zero source changes beyond Phase A's CMake
  plumbing, since this is today's Emscripten+EasyGL path under a new name).
  Follow the same "compiles under node, not yet browser-pixel-verified" standard the `CANVAS`
  backend already documents (`README.md`'s `CANVAS` bullet) — don't overclaim.
- ⬜ **GLB-25** — Add a `cmake/Tests/WebGl2Tests.cmake` (or extend `EasyGLTests.cmake`'s guard) so
  CI/CTest can distinguish `WEBGL2` runs from `OPENGLES`/`OPENGL33` runs.
- ✅ **GLB-39** — **New, found and fixed 2026-07-19.** Fixed the `__cpp_exception` undefined-symbol
  link failure — see this Phase's status note above for the full root cause and fix (CNA's own
  top-level `CMakeLists.txt`, not `meta-gl-followup-audit` as first suspected).
- ✅ **GLB-26** — `docs/webgl2-backend.md`.
- ✅ **GLB-40** — **New, found 2026-07-19, root-caused and fixed 2026-07-20.** Originally filed as
  "custom `ShaderEffect` compile failures are silent under `OPENGL33`"
  (`cna_test_easygl_shipgame_particle_shader` failing with no diagnostic). **That description was
  wrong** — investigated further before implementing anything, since `ShaderEffect.cpp`'s
  constructor already does print `effectBackend_->GetCompileError()` via `std::cerr` whenever
  `IsValid()` is false, which should have caught a real compile failure. Verified directly against
  a real Mesa desktop core-profile GL context (a standalone test program linking `easy-gl`/
  `meta-gl`, bypassing the whole CNA stack) that Mesa's compiler **silently accepts
  `"#version 300 es"` even under a `#version 330 core`-negotiated context** (`is_compiled: 1`, no
  error log) — so there never was a compile failure to report.
  **Real root cause**: `cna_test_easygl_shipgame_particle_shader` is a `GL_POINTS`/`gl_PointSize`/
  `gl_PointCoord` particle-sprite shader. GLES/WebGL always honor a vertex shader's `gl_PointSize`
  output automatically; desktop GL (both compatibility and core profile) requires
  `glEnable(GL_VERTEX_PROGRAM_POINT_SIZE)` to be called explicitly, or `gl_PointSize` is silently
  ignored and every point renders at the fixed 1.0-pixel default — invisible at the test's sampled
  pixel, exactly matching the observed "no rendered content" symptom. `EasyGLGraphicsBackend` never
  called this (confirmed via a full grep — no `PROGRAM_POINT_SIZE` anywhere in the file before this
  fix), and it isn't in `meta-gl`'s typed `Capability` enum at all (GLES/WebGL have no equivalent
  constant, so `meta-gl` never needed to expose it).
  **Fix**: a new `EnableVertexProgramPointSize()` helper (`EasyGLGraphicsBackend.cpp`,
  `#ifdef CNA_GL_PROFILE_OPENGL33`-gated), calling `glEnable(0x8642)` via a runtime function
  pointer loaded through `SDL_GL_GetProcAddress` (matching this project's existing "no static
  `libGL` linkage" convention — `meta-gl` itself loads every GL entry point the same way), called
  after both `device.initialize()` sites (normal startup and context-loss recovery).
  **Verified**: `cna_test_easygl_shipgame_particle_shader` now `[PASS]`es all 4 checks under
  `OPENGL33` (previously 2/4 `[FAIL]`); re-verified still `[PASS]`es under `OPENGLES` (no
  regression, the new code is `#ifdef`-gated to `OPENGL33` only); re-ran the **full 241-binary
  `OPENGL33` suite** from `GLB-22` — **`236/241 pass, 0 crash, 5 fail`** (up from 235/241), the
  same 5 pre-existing/unrelated failures as before, zero new regressions.

### Phase F — `WEBGL1` (new, needs easy-gl fixes first)

> **Status (2026-07-19): GLB-30 through GLB-35 already done — found landed on `easy-glrvc`
> (commit `14109db`, "Fix WebGL correctness gaps...", co-authored by another agent/session while
> this plan was in progress) and independently verified here, not just trusted from the commit
> message (per this project's own "verify agent approval claims" lesson). Verified by building
> `easy-glrvc` natively (`cmake -S . -B ...` against `../meta-gl-followup-audit`) and running all 4
> of its test binaries against a real GL context (`DISPLAY=:99` Xvfb): `easy-gl-smoke-tests`,
> `easy-gl-resource-smoke-tests`, `easy-gl-context-lifecycle-tests` all pass, and the new
> `easy-gl-webgl-tests` (a WebGL1-shaped mock-loader suite added by this commit) passes all 8 of
> its checks — `test_webgl1_context_detected_and_gated`,
> `test_webgl1_without_vao_extension_fails_clearly`, and one `_throws_instead_of_crashing` check
> each for `Query`/`Sampler`/`TransformFeedback`/`Sync`/`ProgramPipeline`/
> `Texture::get_level_parameter`. Read the actual diffs (not just the summary) for `GLB-30`-`GLB-34`
> and confirmed each matches its `webgl.md` finding exactly. `webgl.md` itself was deleted by that
> commit (folded into `easy-glrvc/NEXT.md`) — the "already-verified-true" judgment made about it
> earlier in this plan's history still stands, it just now lives in `NEXT.md` instead.
>
> **GLB-35 update, later the same night: now fully verified for real.** The "no Emscripten SDK
> available" premise above was wrong (see Phase E's status note — a working SDK exists at
> `~/emsdk`, just not on `PATH` by default). Ran `easy-glrvc`'s own `cmake --preset emscripten` /
> `cmake --build --preset emscripten` / `ctest --preset emscripten`: both its Emscripten-only test
> binaries (`easy-gl-context-lifecycle-tests`, `easy-gl-webgl-tests`) build and pass 100% under
> real Node execution via a real `emcmake`/`emcc` toolchain.
>
> **GLB-36/37/38 remain not started** — seed `GLB-36`'s WEBGL1 GLSL ES 1.00 shader work now that
> its prerequisite (`GLB-30`-`GLB-35`) is confirmed done; see §4 below, this needs a design
> decision before implementation, not a mechanical follow-on. `GLB-38` (switching `cnagl` off the
> temporary `../easy-glrvc` dependency) needs `easy-glrvc`'s commits actually merged to `easy-gl`'s
> real default branch first — a repo/branch decision, not made here.

Ordered per `../easy-glrvc/webgl.md`'s own "what could be done" list (items 1-6 in that doc, now
folded into `easy-glrvc/NEXT.md` since `webgl.md` was deleted); task numbers below map 1:1 to that
list. **All of GLB-30 through GLB-35 are `easy-gl` repo work** (on `easy-glrvc`, branch `rvc`), not
`cnagl` work.

- ✅ **GLB-30** — `easy-gl`: replace `Device::initialize()`'s manual `GL_VERSION`-string parsing
  with a call to `meta-gl`'s existing `GetContextInfo()`/`GetCapabilities()` (which already has
  correct `ApiKind::WebGL`/`webgl1`/`webgl2` detection). Removes the "accidentally correct because
  Emscripten's `GL_VERSION` string happens to parse right" fragility noted in `webgl.md` item 1.
- ✅ **GLB-31** — `easy-gl`: add `ApiKind::WebGL` to `include/easygl/ContextInfo.hpp` (currently
  only `Unknown/OpenGL/OpenGLES`), surfaced from `meta-gl`'s already-correct detection (`webgl.md`
  item 1 + item 5).
- ✅ **GLB-32** — `easy-gl`: add `Feature`/`require()` gates to `Query`, `Sampler`,
  `TransformFeedback`, `ProgramPipeline`, `Sync`, and any other ES-3.x+-only call site currently
  called unconditionally (`webgl.md` item 2) — must raise a clear exception on a WebGL1/GLES2
  context instead of crashing on a null function pointer. This is the highest-risk item on the list
  (touches 5 classes); budget the most review time here.
- ✅ **GLB-33** — `easy-gl`: document `ProgramPipeline` as permanently unavailable on any WebGL
  context (`webgl.md` item 3) — not a WebGL1-vs-2 distinction, a hard browser-GL limitation.
- ✅ **GLB-34** — `easy-gl`: make `Device::initialize()`'s `Feature::VertexArrayObject` requirement
  explicit about depending on `OES_vertex_array_object` on WebGL1/GLES2 contexts (`webgl.md` item
  4), rather than an implicit assumption.
- ✅ **GLB-35** — `easy-gl`: add an Emscripten CMake preset + a minimal WebGL1 smoke
  example/test (`webgl.md` item 6) — this is what actually proves `GLB-30`-`GLB-34`, not just code
  review. **Fully verified**: `cmake --preset emscripten && cmake --build --preset emscripten &&
  ctest --preset emscripten` — both Emscripten-only test binaries build and pass 100% under real
  Node execution via a real `emcmake`/`emcc` toolchain.
- ✅ **GLB-36** — **Done 2026-07-19 (authorized to start same night the project owner went to
  sleep — see §4's original open question, now resolved).** Implemented
  `TransformGlslEs300BodyToEs100()` in `EasyGLGraphicsBackend.cpp`: real per-shader GLSL ES 1.00
  rewrite (`attribute`/`varying`/`texture2D()`/`textureCube()`/`gl_FragColor`), plus
  `ExtractVertexAttribLocations()` + `Program::bind_attrib_location()` calls in `CompileAndLink()`
  and `EasyGLSpriteBatchBackend::InitializeResources()` to replace the `layout(location=N)`
  qualifiers GLSL ES 1.00 doesn't support — this is exactly the "design decision" §4 flagged
  (rebinding locations from the C++ side rather than the shader text), now implemented and
  verified rather than merely decided.
  **Verified**: a standalone C++ test harness (no GL/Emscripten needed) confirmed the string
  transform's correctness against real shader text extracted from this file, and a real
  `emcmake`/`emcc` build of `cna_house3d_demo` under `-DCNA_GRAPHICS_BACKEND=WEBGL1` compiles and
  links cleanly, running under Node exactly as far as `WEBGL2` already does (same DOM-only
  limitation, not GL/shader-related). **Not verified**: actual GLSL ES 1.00 driver acceptance
  (needs a real browser). **`SkinnedEffect`/`SkinnedPbrEffect`'s `uvec4 aBoneIndices` gap — closed
  as a same-session follow-up** (project owner authorized it 2026-07-20 morning after the narrower
  scope below was found): `DescribeVertexElementFormat()`'s `Byte4` case now reads the same
  underlying bytes as floats under `WEBGL1` (no `VertexBuffer` upload change needed — `Byte4` is
  `BLENDINDICES`'s only user), and a new `RewriteBoneIndicesForEs100()` converts the shader's
  `uvec4` attribute and its 4 `uBones[]` index expressions. Verified via the same standalone
  harness plus a real `OPENGLES`/`OPENGL33` regression run (`cna_test_easygl_skinnedeffect_golden`/
  `skinnedpbreffect_golden`/`cna_test_skinned_effect`/`cna_test_skinned_integration`, all exit 0)
  and a real `emcmake`/`emcc` compile+link under `WEBGL1`. See `docs/webgl1-backend.md` for full
  detail.
  **Found and fixed a real, separate bug while verifying this**: `cna_house3d_demo`'s Emscripten
  link options hardcoded `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2` unconditionally — exactly
  the class of gap the original `GLB-9` was looking for, just in a per-target CMake option instead
  of the graphics backend's own context-creation code. Fixed and verified (see `cmake/Examples.cmake`
  commit `d44ed617`).
- 🟨 **GLB-37** — Full EasyGL-family GTest suite under `-DCNA_GRAPHICS_BACKEND=WEBGL1` via
  `emcmake`, `docs/webgl1-backend.md`. **Partially done**: `docs/webgl1-backend.md` written; the
  full GTest suite itself is NOT runnable under Emscripten at all regardless of GL profile
  (`cmake/Tests/EasyGLTests.cmake` gates `cna_test_easygl_*` targets to `NOT EMSCRIPTEN` — same
  pre-existing, unrelated scope limit already hit for `WEBGL2`/`CANVAS`).
- ✅ **GLB-38** — **Done 2026-08-07, at Batch 4 integration under direct project-owner
  instruction.** The completed MetaGL follow-up-audit work and EasyGL `rvc` work (`GLB-30`-`GLB-35`
  among it) reached their `develop` branches as attribution-clean replays (MetaGL `develop`
  `c964e73`, EasyGL `develop` `9b831de`; both trees byte-identical to the as-authored heads,
  which remain preserved on `feature/followup-audit`/`rvc` and the signed
  `archive/preintegration/*` tags). `cmake/BackendSelection.cmake` now builds against the
  canonical `../easy-gl` sibling checkout again; the `GLB-7` temporary `../easy-glrvc` redirect
  is retired, and no `easy-glrvc` reference remains in the build. The `easy-glrvc` worktree's
  disposal is the owner's normal sibling-repo cleanup, outside this repository.

## 4. Open questions for the project owner — resolved 2026-07-19

- `GLB-14` (rename `examples/easygl_*.cpp` → `opengles_*.cpp`?): **Decided: keep `easygl_` —
  do not rename.** The name correctly refers to the internal implementation regardless of public
  profile; not worth ~50 files of diff noise.
- `GLB-36` (WEBGL1 shader body — real architecture work, dropping `layout(location=N)` and
  rebinding vertex-attribute locations from C++ across ~26 programs, unverifiable without a real
  browser/headless-GL): **Decided: start on this next session.** The `emcc` compile step alone
  (even without a way to fully runtime-verify a WebGL1 context) is real signal worth having.
  **Done, same night** (see Phase F's `GLB-36` status above) — implemented, standalone-verified,
  and real-`emcc`-build-verified. New follow-up open question this created, not yet asked:
  `SkinnedEffect`/`SkinnedPbrEffect` need a real `vec4`-encoded-bone-index architecture change to
  work under `WEBGL1` at all — see `docs/webgl1-backend.md`'s "What's not yet done" section.
  **Investigated further the same night (not implemented — still asking first, per the commitment
  just above)**: this turns out to be narrower than first described. The GPU-side vertex bytes for
  `BLENDINDICES` (`VertexElementFormat::Byte4`) are already stored as plain `UnsignedByte` —
  `DescribeVertexElementFormat()`'s `isInteger` flag (`EasyGLGraphicsBackend.cpp:2452`, consumed at
  exactly 2 call sites, lines ~2493/4970) is what currently selects `glVertexAttribIPointer` (true
  integer read, matching the shader's `uvec4`) vs. `glVertexAttribPointer` (float-converting read).
  Setting `isInteger=false` for this one case *under `WEBGL1` only* would read the exact same
  underlying bytes as floats (0–255 range, far more than the ≤72 bone count needs, exactly
  float-representable) with **zero change to `VertexBuffer`'s upload path** — only the attribute-
  pointer setup at those 2 call sites needs a `WEBGL1`-only branch, plus the shader needs
  `attribute vec4 aBoneIndices;` (not `uvec4`) and `int(aBoneIndices.x)`-style casts at its 4
  `uBones[...]` index expressions (3 shaders, `EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram`/
  `EnsurePbrSkinnedProgram`). **Decided 2026-07-20 morning: go ahead — done.** Implemented and
  verified exactly as scoped above; see Phase F's `GLB-36` status note and `docs/webgl1-backend.md`
  for full verification detail.
- `GLB-38` (push/merge `easy-glrvc`'s commits into `easy-gl`'s real default branch so `cnagl` can
  drop the temporary `../easy-glrvc` dependency): **Decided: leave to the project owner — do not
  attempt to merge/push between repos autonomously.** (Reconfirmed 2026-07-20 morning.)
- `GLB-40` (originally filed as "custom `ShaderEffect` compile failures are silent under
  `OPENGL33`"): **Decided 2026-07-20 morning: investigate — done, and the original description
  turned out to be wrong.** There was no silent compile failure at all (Mesa accepts
  `"#version 300 es"` leniently even under core profile); the real bug was a missing
  `glEnable(GL_VERTEX_PROGRAM_POINT_SIZE)` for `gl_PointSize`-driven point-sprite rendering under
  desktop GL. Fixed — see the `GLB-40` task entry under Phase D for full root cause and
  verification. (The `CreateEffectBackend()` error-handling contract across backends turned out
  not to be the relevant question here — noted in case it resurfaces for a real future case.)
- Whether `OPENGLES` should default to GLES 3.0 (today's behavior, kept in this plan) or whether a
  true "OpenGL ES" public name should support ES 2.0 as well — this plan assumes ES 3.0-only
  scope for `OPENGLES` (matching current behavior) and treats ES2-class support as exclusively the
  `WEBGL1` profile's problem, since no current CNA use case needs desktop/mobile ES2. **Not asked
  this round — still open if it turns out to matter.**

## 5. Unrelated finding (not fixed, out of scope, noted for completeness)

Running the full `CnaTests` suite under `OPENGLES` (both before and after all of this session's
`GLB-36` changes — confirmed identical, so not a regression from this plan) segfaults partway
through `MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` (`exit=139`). Zero relation to
graphics/GL-family code — a `Microsoft::Xna::Framework::Media` subsystem test, same family as this
sandbox's other known pre-existing Media/Content/XNB gaps (no real media library/devices here).
Not investigated further — flagging so it isn't mistaken for something this plan's work caused.
