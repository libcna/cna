# OpenVG existence-gate spike

Standalone probe proving ShivaVG (the chosen OpenVG 1.1 implementation, see
`cmake/ThirdPartyOpenVG.cmake` and `docs/openvg-renderer.md`) actually renders under this
project's toolchain and Xvfb-based headless validation environment, before any CNA renderer code
was written.

## What it proves

* ShivaVG's 11 core `.c` files (`src/sh*.c`) compile against a modern (2020s) mesa/GLX toolchain,
  once two small compatibility gaps are bridged (documented in `cmake/ThirdPartyOpenVG.cmake`,
  "Gap 1"/"Gap 2" -- a generated one-line `config.h` and a `GLintptr`/`GLsizeiptr` shim). No
  upstream ShivaVG source is patched; both fixes are compiler flags/generated headers.
* `vgCreateContextSH()` (ShivaVG's own extension for attaching an OpenVG context on top of a
  caller-created GL context, since it does not implement EGL) succeeds against a real GLX context
  created under Xvfb.
* A real path is created (`vgCreatePath`/`vguRect`), a real solid-color paint is set through
  `vgSetParameterfv(paint, VG_PAINT_COLOR, ...)` (the underlying primitive `vgSetColor`/
  `vgGetColor` are declared in ShivaVG's `openvg.h` but never actually implemented in this
  revision's `shPaint.c` -- a genuine upstream gap, not a CNA choice), and `vgDrawPath` actually
  rasterizes it.
* `glReadPixels` against the same GL framebuffer ShivaVG rendered into reads back the exact
  expected clear color and fill color -- i.e. this is real pixel output, not a fabricated success.

## Running it

```sh
gcc -c -include <(printf '#ifndef S\n#define S\n#include <stddef.h>\ntypedef ptrdiff_t GLintptr;\ntypedef ptrdiff_t GLsizeiptr;\n#endif\n') \
    -DHAVE_CONFIG_H -I<shivavg>/include/vg -I<shivavg>/src -I<shivavg> -c <shivavg>/src/*.c
ar rcs libshivavg.a *.o
gcc openvg_smoke.c -I<shivavg>/include/vg -o openvg_smoke libshivavg.a -lGL -lGLU -lX11 -lm
xvfb-run -a ./openvg_smoke
```

Expected output:

```
center pixel RGBA = 255 0 0 255
corner pixel RGBA = 26 51 76 255
SMOKE TEST PASSED
```

(`26 51 76` is the clear color `(0.1, 0.2, 0.3)` quantized to 8-bit -- confirms `vgClear` really
ran, not just that the process exited 0.)

## Outcome

Confirmed ShivaVG is a viable, genuine OpenVG implementation for this environment. CNA's real
`OPENVG` renderer (`modules/renderers/openvg/`) builds on exactly this same
context-creation-then-draw-then-readback shape, driven through SDL3's window/GL-context management
instead of raw Xlib/GLX.
