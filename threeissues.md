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

## 2. `OPENGL33` segfaults in a glTF scene-graph test — NOT REPRODUCIBLE at HEAD

**Where:** `modules/content/tests/CNA/Internal/GltfImport/GltfSceneGraphBonesTests.cpp`, test
`GltfSceneGraphBones.SharedMeshGetsOneBonePerInstancingNode`.

**Status: could not be reproduced on 2026-08-15**, in any of four combinations. The original report
stated the crash was reproducible even in isolation. It is not, here, now.

### What was tried

An `OPENGL33` build did not exist on this machine at all — which is why the renderer went unexercised
through the whole campaign, every gate having used `OPENGLES3`, `HEADLESS`, `SOFTWARE`,
`SDL_RENDERER` or `VULKAN`. One was built (`cmake-build-opengl33`) specifically for this.

| Scenario | Result |
|---|---|
| `GltfSceneGraphBones.*` on the real display `:0` | 6/6 passed, 776 ms |
| `GltfSceneGraphBones.*` under Xvfb `:99` | 6/6 passed, 480 ms |
| Full corpus on `:0` | **ran to completion**, 6417 tests, 6365 passed, no crash |
| Full corpus under Xvfb `:99` | ran to completion, 6417 tests, 6368 passed, no crash |

The context is a real GL 4.6 core profile (Mesa 25.0.7), so this is not a driver falling back below
what the renderer needs.

### What the crash had been hiding

Because the run always died early, **nobody had ever seen a complete `OPENGL33` corpus result**. The
first full run shows four failures on the real display:

- `NonIndexedDrawRangeTest.EverySupportedTopologyHonorsVertexStartAndExactCount`
- `NonIndexedDrawRangeTest.TopologySwitchesKeepTheirOwnRangesInOneFrame`
- `PointListPrimitiveTest.PointListRespectsViewportScissorAndBlendState`
- `Texture2DCacheReconstructionTest.RenderTargetReadbackComesFromTheSurfaceNotAnUploadShadow`

They are **specific to the real display**, not to load and not to this campaign's changes:

| Run | `:0` | Xvfb `:99` |
|---|---|---|
| inside the full corpus | 4 fail | 0 fail |
| the same four, standalone | 3 fail | 0 fail |

Standalone reproduction on `:0` rules out test-order contamination; passing under Xvfb rules out a
renderer defect independent of the display. This is the same shape as the compositor-specific
failure this repository already documents for `EasyGL_RealWindowResize` (REMED-BUILD-010): a real
GNOME/Mutter compositor, not an isolated X server.

Three of the four suites were converted to runtime gating earlier the same day, so "it was probably
always like this" was not assumed — the display split is what settles it, since a conversion defect
could not pass under one X server and fail under another.

### What this means

The finding as originally written is no longer true and should not be carried forward as an open
crash. What replaces it is narrower and verified: **`OPENGL33` has four display-dependent pixel
failures on a real compositor**, worth their own investigation, and no crash.

## 3. The Emscripten build does not configure, for two reasons in `sharp-runtime`

**Where:** the sibling `sharp-runtime` repository, not this one.

**Status: FIXED upstream on 2026-08-15** in `sharp-runtime` `bc8dbf41` (branch `develop`), both
halves. Neither workaround below is needed any more; they are kept here because they document what
the failure looked like and how it was diagnosed.

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

**Fixed:** that component now requests the port itself when targeting Emscripten and keeps
`find_package(ZLIB)` everywhere else. The link option is `PUBLIC`, not `PRIVATE`, because
`sharp_runtime_io_compression` is a STATIC library and `-sUSE_ZLIB=1` has to reach the final
executable's link line as well — `PRIVATE` would configure and compile cleanly and then fail
whoever links the result, which is the worse of the two failures.

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

**Fixed** by the first of those: `ThrowNative` is now compiled only where it is used. That keeps the
dead code out of the wasm binary as well, which `[[maybe_unused]]` would not.

`NativeDetail` had to move inside the same guard. Its only caller is `ThrowNative`, so excluding just
`ThrowNative` moves the identical error one line up — a detail worth recording, because the compiler
reports only the first of the two.

### Why this matters beyond Emscripten

Both are Clang-vs-GCC and target-specific, which is exactly the class of breakage that survives
unnoticed until someone tries the target. CNA's Emscripten preset (`CMakePresets.json`, preset
`web`) carries neither workaround — it could not configure before, and needs no change now.

### How the fix was verified

With emsdk 6.0.6, against this repository as the consumer, and with **no workaround flags of any
kind**:

* `emcmake cmake` configures — no `Could NOT find ZLIB`
* `sharp_runtime_io` compiles — this was the `-Werror` failure
* `sharp_runtime_io_compression` compiles and links
* native is untouched: CNA's HEADLESS suite is identical before and after the change, 6389 tests,
  6172 passed, 217 skipped, 0 failed

---

## Summary

| # | Issue | Confirmed? | Caused by renderer-selection work? | Fixed? |
|---|---|---|---|---|
| 1 | `WEBGL1` may take ES 3.0 paths | no — needs a browser | no | no, preserved verbatim and documented |
| 2 | `OPENGL33` glTF segfault | **no longer reproducible** (4 scenarios, 2026-08-15) | no | superseded: 4 display-dependent pixel failures instead |
| 3 | Emscripten build blocked in `sharp-runtime` | yes | no | **fixed upstream**, `sharp-runtime` `bc8dbf41` |
| 4 | Backslash comment dropped `PORTABLEGL` from a test's renderer list | yes; benign in effect | no | **fixed**, trap removed |
| 5 | Skia's Texture2D validation and its test disagree about DXT/BC7 | yes, at `a749fdce3` | no | **fixed** — the test was wrong |
| 6 | `BLEND2D`/`OPENVG` arms define no constants, so those builds cannot compile the suite | yes, by reading | no | **fixed**, RTR-P9-4 |
| 7 | A guard with identical arms made every renderer claim SDL_GPU's boundary | yes | no | **fixed**, RTR-P9-10 |
| 8 | A caller-supplied window was abandoned on fallback; the guard against it was dead code | yes, by test + reading | **yes** — found by RTR-P5-13's own test | **fixed**, RTR-P5-13 |

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

---

## 5. Skia's Texture2D validation and its own test disagree about compressed formats

**Where:** `modules/graphics/src/Xna/Texture2D.cpp` (the Skia surface-format list, now
`SkiaRenderer::ClassifySurfaceFormatEXT`) versus
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp`
(`UnsupportedFormatConstructionTest`, the `kAllFormats` loop).

**Status: FIXED on 2026-08-15 — the TEST was wrong, the implementation was right.**

Decided from the source rather than by preference. `SkiaTextureRenderer.cpp` carries
`IsCompressedTextureFormat`, the correct block sizes (8 bytes for `Dxt1`, 16 for the rest) and
REAL decoders -- `DxtUtil::DecompressDxt1`/`Dxt3`/`Dxt5` and `Bc7Util::DecompressBc7` -- which
decode to RGBA for the CPU raster surface, and it throws `NotSupportedException` for a format it
has no decoder for. That is genuine support, not silent acceptance: if Skia had merely swallowed
the bytes and drawn nonsense, the test would have been right and the validation would need fixing.
It is the other way round, so the five block-compressed formats were added to the test's
Skia-supported list and the implementation was left untouched.

Gates after the change, all unchanged: HEADLESS 6172, SOFTWARE 6253, OPENGLES3 6369, 0 failures.

### What was found

Under `SKIA`, constructing a `Texture2D` with `Dxt1` (SurfaceFormat ordinal 4) or `Dxt3` (5)
succeeds, while the test asserts it must throw `std::runtime_error` — "must throw, not silently
succeed with the wrong GPU format".

Both halves are stated explicitly in the pre-campaign source, and they disagree:

| At `a749fdce3` | `Dxt1`/`Dxt3`/`Dxt5`/`Bc7EXT`/`Bc7SrgbEXT` |
|---|---|
| `ValidateTexture2DFormatEXT`'s Skia branch | **accepts** all five |
| `UnsupportedFormatConstructionTest`'s supported list | **omits** all five |

So the test has been failing on `SKIA` for as long as both lists have had their current contents.

### Why it is not caused by the renderer-selection work

Verified two ways. A clean detached build at `a749fdce3` reproduces the neighbouring `SKIA` failures
(`SetRenderTargets_FourTargets_DoesNotThrow`,
`RenderTargetCubeSetDataContractTest.StoresTheFaceOrRefusesButNeverSilentlyDiscardsIt`), and the two
lists above are quoted directly from that commit — the disagreement is in the original source, not
introduced by moving either list.

Phase P3 moved the validation list verbatim into `SkiaRenderer::ClassifySurfaceFormatEXT` and phase
P9 moved the test's list verbatim into a runtime predicate. Neither changed a member.

### How to settle it

Decide which list is right — whether Skia genuinely stores DXT/BC7 blocks (in which case the test's
list is missing five entries) or does not (in which case the validation should refuse them) — and
correct the other. `SkiaRenderer::IsCompressedTransferFormatEXT` already claims the compressed
transfer path for exactly those five formats, which suggests the validation is the accurate half and
the test simply was never updated for them.

## 6. Two arms of a capability chain define nothing, so those two renderers cannot compile the suite

`GraphicsDeviceCapabilityTests.cpp` chose three expected answers through one `#if`/`#elif` chain:

```cpp
#elif defined(CNA_RENDERER_SKIA)
// ... reasoning ...
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_BLEND2D)
// Another 2D-only renderer, same shape as Skia immediately above: ... All three are honest
// structural refusals, not gaps.
#elif defined(CNA_RENDERER_OPENVG)
// OpenVG is a 2D vector-graphics API with no 3D pipeline, no MRT, and no occlusion-query concept
// at all ...
#elif defined(CNA_RENDERER_PORTABLEGL)
constexpr bool kExpectMultipleRenderTargets = false;
...
```

The `BLEND2D` and `OPENVG` arms carry the full reasoning for their answers and then **define no
constants at all** — the comment is followed directly by the next `#elif`.

`#elif` arms are mutually exclusive, so this is not a fall-through to the catch-all. A Blend2D build
selects the empty Blend2D arm, the `#else` is never reached, and `kExpectMultipleRenderTargets`,
`kExpectOcclusionQuery` and `kExpectCustomEffects` do not exist. They are used unguarded a few lines
below:

```cpp
TEST(GraphicsDeviceCapabilityTest, SupportsMultipleRenderTargets)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets),
              kExpectMultipleRenderTargets);
}
```

so `CnaTests` does not compile for `CNA_GRAPHICS_RENDERER=BLEND2D` or `=OPENVG`. Verified by
reading: the three names are defined in that one chain and nowhere else in the file.

Neither renderer is buildable in this environment, so this is a defect found by inspection, not one
reproduced by a failing build. That also explains how it survived: nothing on this machine compiles
either configuration, and the arms look complete because the reasoning is there.

**Handled in RTR-P9-4.** The chain is now a lookup keyed on the active renderer, where every arm
returns a value and a renderer that is not named falls to a documented default. Blend2D and OpenVG
return `{false, false, false}`, which is what their comments always claimed. Those two values are
the **documented intent, not a measurement** — they cannot be checked here until one of those
renderers builds. If either turns out to report a capability as true, the test will now say so
instead of failing to compile.

## 7. A renderer guard with the same value in both arms made every renderer claim SDL_GPU's boundary

`modules/graphics/examples/rendertarget_effect_source_test.cpp` chose a capability constant like
this:

```cpp
constexpr bool kSecondSampleableFormat =
#if defined(CNA_RENDERER_SDL_GPU)
    false;
#else
    false;
#endif
```

Both arms are `false`, so the guard decides nothing. Its only use is:

```cpp
if (!kSecondSampleableFormat)
    boundary(std::string("J1 ") + kRendererName + " creates every Texture2D and render target as "
         "one fixed native colour format, so SurfaceFormat::Color is the only sampleable "
         "colour format it offers -- capability boundary, not measured");
```

so **every** renderer that runs this example printed that sentence about itself. The claim is true
of SDL_GPU — the doc comment above the constant explains exactly why, in SDL_GPU's terms — and is
false of the others. The example is built by five renderer families (EasyGL, Vulkan, WebGPU,
SDL_GPU, LLGL), so on an OPENGLES3 build it stated a fixed-single-format boundary for EasyGL, which
is not one of EasyGL's boundaries at all.

This is worse than a redundant guard. A "capability boundary, not measured" line is how this corpus
records a renderer's real limits, so a wrong one is indistinguishable from a genuine finding by
anyone reading the log later.

**Fixed in RTR-P9-10** by making the `#else` arm `true`, which is what the constant's own comment
always described: only SDL_GPU declares that boundary. Verified by building and running
`cna_test_easygl_rt_effect_source` on OPENGLES3 — the sentence no longer appears and the example
still passes 20/20 legs.

**How it was found, and what else was checked.** Scanning for guards whose `#if` and `#else` bodies
are textually identical: 96 renderer guards with an `#else` across `modules/`, exactly one of them
dead. A separate scan for guards naming a `CNA_RENDERER_<X>` macro that CMake never generates found
none — so no guard is stranded on a removed renderer identity.

## 8. A caller-supplied window was silently abandoned on fallback, and the guard against it was dead code

`GraphicsDevice::resolveRenderer()` walks the fallback chain. When a candidate fails to initialise,
the catch block recorded the failure and called `discardOwnedWindow()`.

That helper is careful about the right thing and careless about the rest:

```cpp
if (ownsWindow_)
{
    ...
    SDL_DestroyWindow(window_);      // correctly NOT done for a caller-supplied window
}

window_ = nullptr;                                          // done unconditionally
ownsWindow_ = false;                                        // done unconditionally
presentationParameters_.setDeviceWindowHandleProperty(0);   // done unconditionally
```

So a window the caller passed in through `PresentationParameters.DeviceWindowHandle` was never
destroyed — but after any initialisation failure CNA forgot it, zeroed the caller's own handle
field, and the next candidate created a window of its own. The caller's window was left orphaned:
still alive, no longer drawn into, with no signal that this had happened.

**It also made design decision 8 unreachable.** The cross-window-kind check at the top of the
candidate loop is guarded by `window_ != nullptr`:

```cpp
if (window_ != nullptr && !AreWindowKindsCompatible(activeWindowKind_, candidate->windowKind))
{
    if (!ownsWindow_) { record WindowKindConflict; continue; }
    discardOwnedWindow();
}
```

Reading the whole loop shows nothing could leave `window_` non-null across an iteration: every exit
after a window is attached goes through the catch, and the earlier `continue`s (not compiled in,
probe unavailable) happen before any window exists. So `WindowKindConflict` — the whole "never
silently reuse an incompatible window" contract — was **dead code**, and had been since it was
written.

**Fixed in RTR-P5-13:** the failure path discards the window only when this device actually owns it.
A caller's window now survives the failure, and the next candidate either accepts it or is refused
with `WindowKindConflict` naming the reason.

**How it was found, which is the part worth repeating.** The test written for that contract skipped
three times, each time for a different reason: the origin renderer chosen did not need a window at
all; then the renderer set had no two renderers wanting windows of different kinds (`None` is
compatible with everything, so no conflict was possible even in principle); and only after fixing
both did the skip message — deliberately made to report what it observed — show that the device had
been constructed with no conflict recorded at all. Accepting the first "SKIPPED" as a pass would
have missed the defect entirely, and the test would have been a permanent green that asserted
nothing.
