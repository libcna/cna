# cna-street on the OpenGL4 renderer

Owner brief of 2026-09-24: build and run `cna-street` (`../cna-street`) on CNA's OpenGL4 renderer
and fix the defects it finds in CNA.

Branch `street-opengl4` from `next` `009d40f5d`. Task IDs `STREETGL4-0001`, … . The predecessors
are [`plan_street.md`](plan_street.md) (EasyGL and Vulkan), [`plan_street_webgpu.md`](plan_street_webgpu.md)
and [`plan_street_sdlgpu.md`](plan_street_sdlgpu.md), which did the same for the other renderers.

## How the comparison is run

cna-street's one build tree gained `OPENGL4` beside the four renderers it already carried:

```sh
export CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv
cmake -S . -B build -DCNA_GRAPHICS_RENDERERS="OPENGL33;VULKAN;WEBGPU;SDL_GPU;OPENGL4"   # default stays OPENGL33
cmake --build build -j8 --target cna-street compare-images

R=../cna/tools/platform/run_gpu_tests_private.sh     # never the live desktop
CNA_GRAPHICS_RENDERER=OPENGL4  $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/opengl4
CNA_GRAPHICS_RENDERER=OPENGL33 $R --exec ./build/bin/cna-street --no-audio --no-overlay --capture out/opengl33
CNA_GRAPHICS_RENDERER=OPENGL4  $R --exec ./build/bin/cna-street --no-audio --benchmark baseline
```

OpenGL4 and EasyGL's `OPENGL33` run on the same driver here (Mesa 25.0.7 radeonsi, Radeon 780M,
SDL3 platform under the private Xwayland), and they share the stock-effect GLSL corpus, so EasyGL is
the reference and the expected difference is none.

## What the street found

**The picture: nothing.** OpenGL4 started, built the scene and wrote every capture without a
warning, and the captures match EasyGL's. Compared over the 18 named viewpoints under five settings
documents (the default, `--night`, `--preset low`, `--preset medium`, and one with SSR, depth of
field, 32 SSAO samples and six bloom levels on), the 29 reflection-probe dumps, the six walkthrough
legs, the 86 lineup views, `--shadow-debug` and `--supersample 2`: identical, except for a few
hundred pixels at five views (a shop interior, a far queue of traffic seen through a bench). Those
differ between two runs of EasyGL itself -- the street's shop dressing and traffic depend on
timing -- and a second EasyGL run matched OpenGL4 there bit for bit.

**The speed: half of EasyGL's.** Same scene, same 1 212 draws a frame:

| `--benchmark baseline` | CPU frame | fps |
|---|---|---|
| OPENGL33 (EasyGL) | 31.35 ms | 31.9 |
| OPENGL4 as found | 67.85 ms | 14.7 |

Sampled with gdb (60 main-thread stacks, the street at viewpoint 1): **31 of 60** inside
`GraphicsDevice::applySamplerStatesToRenderer`, of which 13 in
`OpenGL4Renderer::EnsureCallingThreadContext` -> `Sdl3GlContext::GetCurrentBinding` and most of the
rest in the driver's `glSamplerParameter*`. EasyGL, sampled the same way, spent its time in
`SwapBuffers` -- waiting for the GPU.

## Status

| ID | Task | Status |
|---|---|---|
| STREETGL4-0001 | OpenGL4 re-wrote and re-bound all sixteen sampler objects before every draw | ✅ |
| STREETGL4-0002 | `GpuTimer` discarded every result once the CPU ran a frame ahead of the GPU | ✅ |

---

## STREETGL4-0001 — every draw re-wrote all sixteen sampler objects

**Root cause.** `GraphicsDevice::applySamplerStatesToRenderer` applies all sixteen slots before
every draw -- `ApplySamplerState`, `ApplySamplerMipState` and `ApplySamplerAddressW` each -- and
OpenGL4 answered each with unconditional GL: ten `glSamplerParameter*` writes (FX-092 makes the
object's complete state a function of the call) plus a `glBindSampler`, and one more write and
bind for each of the other two. **208 parameter writes and 48 binds a draw**, measured, whatever
had changed; ~2 000 draws a frame (shadow cascades included) is ~500 000 GL calls. Each call also
began with `EnsureCallingThreadContext()` (`GL4-0021`), which on the SDL3 platform asks SDL for the
current context *and* window and looks the window's ID up in SDL's object table under a read lock.

EasyGL does not pay this: since `GLB-41` its sampler writes go through `WriteSamplerParameter`,
which skips the GL call when the object already holds exactly the value, and `BindSamplerToOwnUnit`,
which binds once.

**Fix.** The EasyGL rule, in OpenGL4's own code:

* `SamplerSlotShadow` records, per slot, the bits of each of the ten parameters the object holds
  and whether the object is bound to its own unit. `WriteSamplerParameter` (int and float
  overloads; floats compared bit for bit) and `BindSamplerToOwnUnit` issue GL only on a
  difference, and only then ask for the context -- a call that issues no GL needs none.
* Every value is still *enforced*: REMED-GFX-174 (anisotropy written on every application) and
  FX-092 (the whole state a function of the call) hold, because the shadow skips only a write
  whose value the object already has.
* The record stays exact because only this renderer writes these objects, the shadow starts
  unknown (the first application writes everything) with each object bound at creation, and the
  one place that borrows a unit's sampler binding -- the compute dispatch (`GL4-0025`) -- restores
  the binding it found.

**Tests** (`OpenGL4SamplerShadowTests.cpp`, counting through the `gl4_*` entry points):

* re-applying an unchanged `SamplerState` issues no sampler write and no bind;
* `PointClamp` -> `LinearClamp` writes exactly the two filters;
* alternating Linear/Point, Clamp/Wrap for three rounds samples what each draw asked for (the
  guard on the record itself).

**A/B:** on the unmodified renderer the first two fail with 208 writes and 48 binds; the third
passes on both, as it should.

**The street after the fix:**

| `--benchmark baseline`, back to back | CPU frame | fps |
|---|---|---|
| OPENGL33 | 29.74 ms | 33.6 |
| OPENGL4 | 26.75 ms | 37.4 |

The re-sampled profile has `EnsureCallingThreadContext` in 1 of 80 stacks and `SwapBuffers` in
28: OpenGL4 is GPU-bound now, as EasyGL was. Making the SDL3 context check itself cheaper (asking
for the context without the window lookup) was planned as a second task and dropped on that
number -- a platform-contract change for ~1 % of a frame.

**Regression runs** (`cmake-build-opengl4`, Wayland):

| suite | result |
|---|---|
| corpus `-R '^OpenGL4_'` | **407 / 0** |
| `CnaRendererTests` | **341 / 0 / 10** (the 10 are EasyGL-only), no `[OpenGL4 GL Error]` |
| `CnaGraphicsExtTests` | **962 / 0 / 7**, no `[OpenGL4 GL Error]` |

---

## STREETGL4-0002 — a GPU timer lost every result once the CPU ran ahead

**Symptom.** With `STREETGL4-0001` in, the street's benchmark on OpenGL4 printed `GPU timing
unavailable on this renderer`, and every GPU column read -1 -- on a renderer whose timer works
(`GL4-0027`), and which had reported them all while it was slow. The run on EasyGL straight after
said the same; the earlier, slower EasyGL run had had its numbers.

**Root cause.** Renderer-neutral: `CNA::Graphics::GpuTimer` owned one renderer query. Its own
documented pattern is `begin`/`end` once a frame and `poll` the next, and the result "normally
arrives one or two frames after the range closed" -- but `begin()` reopened that one query whether
or not its result had been collected, discarding it. A CPU more than a frame ahead of the GPU (a
fast renderer without vsync: the street at 37 fps on a GPU-bound frame) reopens every range before
its answer lands, so no answer ever does. `PostProcessChain`'s per-pass timers, the street's stage
timers and anything else built on `GpuTimer` lost theirs the same way, on every renderer; which
runs had numbers depended on how far ahead the CPU happened to be.

**Fix.** `GpuTimer` keeps a ring of up to `kRangesInFlight` (4) renderer queries, made on demand
-- a caller whose results land within a frame still owns only the first. `begin()` takes the query
after the newest waiting one; with all four still waiting it times nothing (`isOpen()` stays
false) rather than overwrite one. `poll()` collects every finished range oldest first -- GPUs
finish in submission order -- and `getLastMilliseconds()` is the newest. The public surface gains
`kRangesInFlight`, so the engine-layer revision moves to **19** (`docs/cnaext-engine-changelog.md`;
the C header's marker with it, the C ABI unchanged at 0.29.0).

**Tests** (`GpuTimerTests.cpp`): two ranges closed before either is polled both report; with every
query in flight the next range is not timed, exactly `kRangesInFlight` results arrive, and after
collecting them the timer times again. **A/B:** with the single-query `begin()` restored, both
fail (1 result for 2 ranges; 1 for 4).

**Where it was run:**

| tree / renderer | `GpuTimerTest.*`, `PassTiming*`, `EngineLayerVersion*` |
|---|---|
| `cmake-build-opengl4`, OPENGL4 | 17 / 0 / 3 |
| `cmake-build-opengl4`, OPENGL33 at runtime | 13 / 0 / 3 (without `EngineLayerVersion*`) |
| `cmake-build-cnaext`, VULKAN | 17 / 0 / 3 |

The three skips are the unsupported-timer cases, on renderers that have a timer.
`CnaGraphicsExtTests` on OpenGL4 as a whole: 962 / 0 / 7.

**The street after both fixes** (back to back, `--benchmark baseline`):

| | CPU frame | fps | GPU frame |
|---|---|---|---|
| OPENGL33 | 29.60 ms | 33.8 | 35.92 ms |
| OPENGL4 | 26.62 ms | 37.6 | 35.00 ms |

The 18 captures on the final build: identical to EasyGL's (0.000 % of pixels over 8/255 at every
viewpoint).
