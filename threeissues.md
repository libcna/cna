# Issues found while implementing runtime renderer selection

Found on 2026-08-15 while implementing `plan_runtimerenderer.md`. **None of them is caused by
that work**, and none is fixed by it — each was surfaced by exercising code paths the campaign
happened to reach, and each is recorded here so it can be decided on its own merits rather than
silently absorbed into an unrelated change.

They are ordered by how likely they are to be a real defect. A fourth was found later, while
converting the test corpus, and is appended at the end.

---

## 1. `WEBGL1` probably takes OpenGL ES 3.0 code paths its context does not have

**Where:** `modules/renderers/easygl/src/EasyGLRenderer.cpp` (40 sites), classified in
`modules/renderers/easygl/include/CNA/Internal/Renderers/EasyGL/GlProfile.hpp` at
`UsesEs2ApiGenerationLegacyEXT()`.

**Status:** open. Suspected defect, **not confirmed** — confirming it requires running `WEBGL1` in a
real browser, which this environment does not automate.

### What was found

EasyGL serves five public renderer identities. Before phase P11 the choice between them was a
compile definition, and the implementation branched on it 61 times. Auditing those branches to
convert them to runtime checks showed an asymmetry:

| Guard spelling | Count |
|---|---:|
| `#if defined(CNA_GL_PROFILE_OPENGLES2)` — **`OPENGLES2` alone** | 44 |
| `#if defined(CNA_GL_PROFILE_WEBGL1) \|\| defined(CNA_GL_PROFILE_OPENGLES2)` — both | 11 |
| other (`OPENGL33`, `WEBGL2`, …) | 6 |

The 44 that name `OPENGLES2` alone are not about the shading language, where the two profiles could
legitimately differ. They guard **OpenGL ES 2.0 API-generation limitations that WebGL 1 shares**:

| Site | What the guard handles | True for WebGL 1? |
|---|---|---|
| `EasyGLRenderer.cpp:649` | `GL_READ_FRAMEBUFFER` is ES 3.0; ES 2.0 has only the combined `GL_FRAMEBUFFER` | yes — WebGL 1 has no separate read target |
| `EasyGLRenderer.cpp:1097` | ES 2.0 has no `GL_TEXTURE_MAX_LEVEL`; completeness is handled by a mip-term demotion instead | yes |
| `EasyGLRenderer.cpp:1201` | ES 2.0 has no `glReadBuffer` | yes |
| `EasyGLRenderer.cpp:1328` | ES 2.0 keeps sampling state on the texture object, not on a sampler object | yes |

WebGL 1 is an OpenGL ES 2.0-class API. On every one of these points it has the same limitation the
guard exists to work around, so a `WEBGL1` build appears to take the ES 3.0 path — calling entry
points its context does not provide, or relying on state objects it does not have.

### Why it probably happened

`WEBGL1` was added later than `OPENGLES2` (`plan_glbackends.md`, tasks GLB-30…GLB-35). The 11 sites
that name both are consistent with the ES-2.0 sites having been extended to `WEBGL1` **as they were
encountered**, rather than systematically.

### What was done about it

Nothing, deliberately. The P11 conversion is strictly behaviour-preserving: the 44 sites now call
`UsesEs2ApiGenerationLegacyEXT()`, which returns true for `OpenGLES2` only — reproducing
`#if defined(CNA_GL_PROFILE_OPENGLES2)` exactly, including the exclusion of `WebGL1`.

The correct predicate already exists next to it, `UsesEs2ApiGeneration()`, which returns true for
both. Both carry doc comments explaining the difference and why the wrong-looking one is still in
use. Changing `WEBGL1`'s behaviour is a decision about that renderer, not a side effect of making a
profile runtime-selectable.

### How to settle it

1. Build the browser bundle (see `docs/runtime-renderer-selection.md` for the two Emscripten
   prerequisites) and select `WEBGL1` at runtime.
2. Exercise render-target readback, mip-mapped textures and sampler-state changes — the four sites
   above — in a real WebGL 1 context.
3. If they fail, switch those call sites from `UsesEs2ApiGenerationLegacyEXT()` to
   `UsesEs2ApiGeneration()` — one predicate change per site, no restructuring — and delete the
   legacy predicate once no caller remains.

If instead they pass, that is worth knowing too: it would mean the browser's WebGL 1 implementation
tolerates those calls, and the legacy predicate should then be documented as intentional rather than
suspicious.

---

## 2. `OPENGL33` segfaults in a glTF scene-graph test

**Where:** `modules/content/tests/CNA/Internal/GltfImport/GltfSceneGraphBonesTests.cpp`, test
`GltfSceneGraphBones.SharedMeshGetsOneBonePerInstancingNode`.

**Status:** open, **confirmed pre-existing**.

### What was found

Running the full `CnaTests` corpus with `OPENGL33` as the renderer segfaults (exit 139). The crash is
reproducible in isolation by filtering to `GltfSceneGraphBones.*`: the suite runs one test, then dies
entering `SharedMeshGetsOneBonePerInstancingNode`.

### Why it is not caused by the renderer-selection work

Verified directly rather than assumed. `modules/` and `cmake/` were checked out at `c5045553b` — the
commit immediately **before** phase P11 — a single-renderer `OPENGL33` build was made from that
state, and the identical crash occurred in the identical test.

The reason it surfaced only now is that `OPENGL33` had never been run against the corpus during this
campaign. Every earlier gate used `OPENGLES3`, `HEADLESS`, `SOFTWARE`, `SDL_RENDERER` or `VULKAN`.
It was first exercised when phase P11 made the five GL identities coexist and the `OPENGL33` profile
became worth verifying on its own.

### What was done about it

Nothing. It is a defect in that renderer's own glTF path, not in renderer selection, and fixing it
inside an unrelated change would bury it. Recorded in `plan_runtimerenderer.md` next to phase P11 and
here.

### How to investigate

Build single-renderer `OPENGL33` and run
`CnaTests --gtest_filter="GltfSceneGraphBones.*"` under a debugger or ASAN. The crash is immediate
and does not need the full corpus, so the reproduction is cheap.

---

## 3. The Emscripten build does not configure, for two reasons in `sharp-runtime`

**Where:** the sibling `sharp-runtime` repository (`f827a6c5`), not this one.

**Status:** open upstream; **worked around here with configure flags only** — nothing outside this
repository was modified.

### 3a. `find_package(ZLIB)` finds nothing under Emscripten

`sharp-runtime/modules/io-compression/CMakeLists.txt:5` calls `find_package(ZLIB)`, which fails:

```
Could NOT find ZLIB (missing: ZLIB_LIBRARY ZLIB_INCLUDE_DIR)
```

Emscripten ships zlib as a **port** rather than as a system library, so nothing is on the search path
until the port is built. Building it once and pointing CMake at the result is enough:

```bash
embuilder build zlib
cmake ... \
  -DZLIB_LIBRARY=$EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/libz.a \
  -DZLIB_INCLUDE_DIR=$EMSDK/upstream/emscripten/cache/sysroot/include
```

The upstream fix would be for that component to request the port itself (`-sUSE_ZLIB=1`) when
targeting Emscripten, rather than requiring every consumer to know this.

### 3b. An unused function under `-Werror`, visible only to Clang

```
sharp-runtime/modules/io/src/System/IO/RandomAccess.cpp:93:19:
error: unused function 'ThrowNative' [-Werror,-Wunused-function]
```

`ThrowNative` is called from eleven places, but every one of them is inside a `_WIN32` or POSIX
branch of a three-way `#if defined(__EMSCRIPTEN__) / #elif defined(_WIN32) / #else`. The Emscripten
branch of each function throws `PlatformNotSupportedException` instead, so on that target the helper
genuinely has no caller. GCC does not diagnose it; Clang — which Emscripten uses — does, and the
build uses `-Werror`.

Workaround used here:

```bash
cmake ... -DCMAKE_CXX_FLAGS="-Wno-error=unused-function"
```

The upstream fix is to put `ThrowNative` inside the same `#if !defined(__EMSCRIPTEN__)` region as its
callers, or mark it `[[maybe_unused]]`.

### Why this matters beyond Emscripten

Both are Clang-vs-GCC and target-specific, which is exactly the class of breakage that survives
unnoticed until someone tries the target. CNA has an Emscripten preset (`CMakePresets.json`, preset
`web`) that does not carry either workaround, so that preset currently cannot configure.

---

## Summary

| # | Issue | Confirmed? | Caused by renderer-selection work? | Fixed? |
|---|---|---|---|---|
| 1 | `WEBGL1` may take ES 3.0 paths | no — needs a browser | no | no, preserved verbatim and documented |
| 2 | `OPENGL33` glTF segfault | yes, at `c5045553b` | no | no |
| 3 | Emscripten build blocked in `sharp-runtime` | yes | no | worked around with flags only |
| 4 | Backslash comment dropped `PORTABLEGL` from a test's renderer list | yes; benign in effect | no | **fixed**, trap removed |

---

## 4. A backslash-continued comment silently removed `PORTABLEGL` from a test's renderer list

**Where:** `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp`, the
`kCubeStorageSupported` / `kCubeLevel0ReadbackSupported` guard (before its conversion to runtime).

**Status:** confirmed mechanism, open question about the right answer. Behaviour preserved, not
changed.

### What was found

The guard listed the renderers with no cube-map storage:

```c
#if defined(CNA_RENDERER_SDL_RENDERER) || \
    ...
    defined(CNA_RENDERER_OPENVG)
// OPENVG keeps IGraphicsRenderer::CreateTextureCube's nullptr default ... above. || \
    defined(CNA_RENDERER_PORTABLEGL)
constexpr bool kCubeLevel0ReadbackSupported = false;
```

The explanatory `//` comment ends in a backslash. A backslash at the end of a `//` comment continues
the **comment** onto the next line, so `defined(CNA_RENDERER_PORTABLEGL)` was swallowed by it and
never part of the condition. Verified with a minimal preprocessor case rather than by reading:

```c
#if defined(A) || \
    defined(B)
// comment ending with backslash. || \
    defined(C)
int in_list = 1;
#else
int in_list = 0;
#endif
```

`gcc -E -DC` yields `in_list = 0` — `defined(C)` is not in the condition.

### Consequence: real bug, no observable effect *here*

Under a `PORTABLEGL` build `kCubeStorageSupported` was `true`, claiming this CPU software renderer
stores and reads back cube-map faces.

Measured, rather than assumed: a single-renderer `PORTABLEGL` build passes the cube suites **85/85
both with and without** `PortableGL` in the list. The assertions those constants drive
(`ExpectUploadStoredOrRefused`) accept either "stored" or "refused with `NotSupportedException`" —
they exist to forbid the third outcome, silently discarding the data. So the swallowed condition
never changed a verdict.

That the two lists disagree is still a defect: `modules/content/tests` has always included
`PORTABLEGL` in what its own comment calls "the same reviewed renderer set", and states that
"PortableGL keeps the same nullptr `CreateTextureCube` default — no cube resource exists there
either".

### What was done about it

The guard is now a runtime function — no line continuations, so the trap cannot recur — and
`PortableGL` is back in the list, matching the sibling list and the surrounding comments. Verified
against a real single-renderer `PORTABLEGL` build: 85/85 either way.

It is corrected rather than left alone because the list is also read by humans as the statement of
which renderers own cube pixels, and because a future assertion that *does* distinguish "stored"
from "refused" would have silently inherited the wrong answer.

### Worth checking elsewhere

The same pattern — a `//` comment ending in a backslash inside a multi-line `#if` — would silently
drop a condition anywhere it occurs. A grep for `//.*\\$` inside preprocessor conditionals across
the corpus would find any others.
