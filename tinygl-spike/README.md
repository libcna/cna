# TinyGL existence-gate spike (`TINYGL-0`)

Standalone probe run **before** any CNA `TINYGL` renderer code was written, following the same
existence-gate rule as `dx9-spike/`, `dx1-spike/` and the other renderer spikes (see `CLAUDE.md`
§"Existence-Gate Spikes"). Its job was to establish, by execution rather than by reading headers,
that TinyGL can satisfy a useful CNA graphics contract — and where exactly its ceiling is.

Upstream: [C-Chads/tinygl](https://github.com/C-Chads/tinygl), the maintained fork of Fabrice
Bellard's TinyGL. Fixed-function OpenGL 1.x subset, pure CPU, no GPU and no window system.

## What it proved

| # | Claim | Result |
|---|---|---|
| 1 | `ZB_open(ZB_MODE_RGBA)` + `glInit()` create a real context with a CPU color buffer, no GPU, no display server | PASS |
| 2 | `glClearColor`/`glClear` paint the requested color | PASS |
| 3 | `glVertexPointer`/`glColorPointer`/`glDrawArrays` rasterize a real colored triangle under caller-supplied `glLoadMatrixf` matrices | PASS |
| 4 | `glGenTextures`/`glTexImage2D`/`glTexCoordPointer` sample a real texture | PASS |
| 5 | `glArrayElement` inside `glBegin`/`glEnd` replays an indexed draw — TinyGL has **no `glDrawElements`** | PASS |
| 6 | The framebuffer reads back deterministically | PASS |

`GL_VERSION` reports `1.0 TinyGLv1.0`, `GL_RENDERER` reports `TinyGL`, and the framebuffer is
4 bytes/pixel.

## Boundary facts it pinned down

These are the reason the renderer's capability contract looks the way it does. Each was observed by
running the probe, not inferred from documentation.

**A. `glReadPixels` is an upstream stub.** `src/misc.c` validates its arguments and then returns
without writing anything (`/* TODO: implement read pixels. */`). The probe poisons its destination
with `0xdeadbeef` and confirms the poison survives. Readback must therefore go through the
`ZBuffer`'s own `pbuf`/`linesize`, which is what `TinyGLRenderer::ReadBackbuffer()` does.

**B. `glTexImage2D` accepts `GL_RGB` + `GL_UNSIGNED_BYTE`, level 0, no border — and nothing else.**
There is no RGBA upload path at all. Texture alpha does not exist in TinyGL.

**B′. An unsupported argument terminates the process.** This is the single most important finding.
TinyGL does not set an error flag and return for the combinations it cannot handle — it calls
`gl_fatal_error()`, prints `TinyGL: fatal error: glTexImage2D: combination of parameters not
handled!!` and kills the process. Run `./tinygl_existence_gate --prove-rgba-fatal` to observe it.

Consequence for the renderer: **every unsupported argument must be rejected by CNA before it
reaches TinyGL.** There is no recoverable native error path to fall back on, so the renderer
validates first and throws `System::NotSupportedException`, and only then calls into TinyGL.

**C. Blending has no alpha factors.** `zbuffer.h`'s `TGL_BLEND_FUNC` switch implements exactly
`GL_ONE`, `GL_ZERO`, `GL_ONE_MINUS_SRC_COLOR` (source slot only) and `GL_ONE_MINUS_DST_COLOR`
(destination slot only), with `GL_FUNC_ADD`/`GL_FUNC_SUBTRACT`/`GL_FUNC_REVERSE_SUBTRACT` as the
equations. Every other factor falls through to the switch's `default:` and silently behaves as
`GL_ONE`. The probe shows `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` being *stored*
happily by the API even though the rasterizer cannot execute it — which is precisely why the
renderer refuses those factors instead of forwarding them.

**D. There is no stencil plane, no scissor, no color mask and no selectable depth function.** The
`ZBuffer` struct carries `zbuf` (depth) and `pbuf` (color) and nothing else; `glDepthFunc` is not
implemented at all (the comparison is hardcoded `GL_LESS`).

## Building and running

```bash
git clone https://github.com/C-Chads/tinygl.git ~/deps/tinygl
cmake -S ~/deps/tinygl -B ~/deps/tinygl/build -DCMAKE_BUILD_TYPE=Release \
      -DTINYGL_BUILD_SHARED=OFF -DTINYGL_BUILD_STATIC=ON
cmake --build ~/deps/tinygl/build -j4

g++ -std=c++23 -O1 -I ~/deps/tinygl/include \
    tinygl_existence_gate.cpp -L ~/deps/tinygl/build/src -ltinygl-static -lm \
    -o tinygl_existence_gate
./tinygl_existence_gate
```

`-fopenmp` is optional. `glopCopyTexImage2D`, `glopDrawPixels` and the math helpers contain OpenMP
pragmas, but each pragma is guarded by `_OPENMP`. A build without OpenMP therefore produces a
complete single-threaded archive with no `GOMP_*`/`omp_*` references; CNA enables and links the
OpenMP C target only when the toolchain provides it.

Per the repository's spike rule, the `.cpp` and this `README.md` stay committed; built binaries and
object files are gitignored.
