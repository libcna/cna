# NanoVG existence-gate spike

Standalone probe proving [NanoVG](https://github.com/memononen/nanovg) (memononen/nanovg, pinned
commit `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`, zlib license) actually renders under this
project's toolchain and Xvfb-based headless validation environment, before any CNA renderer code
was written. Mirrors `openvg-spike/`'s own GLX/Xlib existence-gate shape.

## What it proves

* NanoVG's `nanovg.c` + `nanovg_gl.h` **GL2 backend** (`NANOVG_GL2_IMPLEMENTATION`) compile and
  link against a real desktop OpenGL 2.1 compatibility context created directly through
  GLX/Xlib -- no SDL, no CNA code involved yet.
* `nanovg_gl.h` is **not** a loader (unlike GLAD): it calls `gl*` entry points unqualified,
  assuming the including translation unit already has them resolvable. On desktop GLX only GL 1.1
  is guaranteed statically linkable from `libGL.so` -- everything from GL 1.2 onward (buffer
  objects, shader objects, `glActiveTexture`, `glBlendFuncSeparate`, `glStencilOpSeparate`, ...)
  needs `glXGetProcAddress` at runtime. This spike's own tiny loader resolves the exact ~28-entry
  set the GL2 backend calls and `#define`s each `gl*` name to the loaded pointer BEFORE
  `nanovg_gl.h` is included, so its plain calls resolve correctly -- the same macro-substitution
  shape a real loader (GLAD/GLEW) uses, scoped to exactly what NanoVG's GL2 path needs. The real
  CNA renderer (`modules/renderers/nanovg/`) reuses this exact mechanism, driven through
  `CNA::Internal::Renderers::LoadPlatformGlProcAddress` instead of raw `glXGetProcAddress`.
* `nvgCreateGL2()` succeeds against that context, a real path is filled (`nvgBeginPath`/`nvgRect`/
  `nvgFillColor`/`nvgFill`), and `nvgEndFrame()` actually rasterizes it through NanoVG's own GLSL
  1.10 shader pipeline (compiled, linked, and driven with real vertex-attribute/uniform calls --
  not a stub).
* `glReadPixels` against the same GL framebuffer NanoVG rendered into reads back the exact
  expected clear color and fill color -- i.e. this is real pixel output, not a fabricated success.

## Running it

```sh
git clone --depth 1 https://github.com/memononen/nanovg.git /tmp/nanovg-src
# (optional) git -C /tmp/nanovg-src checkout ce3bf745eb2d2dbc14a50bf2446783f691ac4353
gcc -o nanovg_smoke nanovg_smoke.c -I/tmp/nanovg-src/src -lGL -lX11 -lm
xvfb-run -a ./nanovg_smoke
```

Expected output:

```
center pixel RGBA = 255 0 0 255
corner pixel RGBA = 26 51 76 255
SMOKE TEST PASSED
```

(`26 51 76` is the clear color `(0.1, 0.2, 0.3)` quantized to 8-bit -- confirms the real GL clear
ran, not just that the process exited 0.)

## Outcome

Confirmed NanoVG's GL2 backend is a viable, genuine vector-graphics renderer for this environment.
CNA's real `NANOVG` renderer (`modules/renderers/nanovg/`) builds on exactly this same
context-creation-then-draw-then-readback shape, driven through SDL3's window/GL-context management
(`CNA::Internal::Renderers::PlatformGlContextOwner`, the same "own GL context, no EasyGL" pattern
`OPENGL1`/`OPENGL2`/`OPENVG` already use) instead of raw Xlib/GLX, and through
`CNA::Internal::Renderers::NanoVg::NanoVgGlLoader` instead of this spike's own standalone loader.
