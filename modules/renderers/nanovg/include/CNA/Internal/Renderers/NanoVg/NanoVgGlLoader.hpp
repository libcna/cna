// SPDX-License-Identifier: MS-PL
#pragma once

struct NVGcontext;

namespace CNA::Internal::Renderers::NanoVg
{
    /**
     * @brief Resolves every post-GL-1.1 entry point NanoVG's GL2 backend (`nanovg_gl.h`) calls,
     * through the platform's current GL loader (`CNA::Internal::Renderers::
     * LoadPlatformGlProcAddress`).
     *
     * `nanovg_gl.h` is not a loader itself -- unlike GLAD/GLEW it calls `gl*` names unqualified,
     * assuming the including translation unit already has them resolvable. On desktop GLX/WGL
     * only GL 1.1 is guaranteed statically linkable, so NanoVgGl.cpp (the one translation unit
     * that `#include`s `nanovg_gl.h` with `NANOVG_GL2_IMPLEMENTATION`) declares file-scope
     * function-pointer variables named exactly like the ~28 entry points NanoVG's GL2 path needs
     * beyond GL 1.1, and this function fills them in. Must be called once, with the target GL
     * context current, before the first `nvgCreateGL2()`/`nvglCreateImageFromHandleGL2()` call.
     *
     * See nanovg-spike/README.md for the existence-gate proof this exact mechanism renders real
     * pixels, and NanoVgGl.cpp's own header comment for the full entry-point list and the reason
     * `GL_GLEXT_PROTOTYPES` is deliberately never defined in that translation unit (it would
     * declare conflicting `extern` prototypes for the same names at file scope).
     */
    void LoadNanoVgGlFunctions();

    /**
     * @brief Creates a real `nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES)` context.
     *
     * NanoVG's `nvgCreateGL2`/`nvgDeleteGL2` are only DECLARED inside `nanovg_gl.h`'s
     * `#if defined NANOVG_GL2` block, which is set only when `NANOVG_GL2_IMPLEMENTATION` was
     * defined before including that header -- so no OTHER translation unit can call them
     * directly without re-defining `NANOVG_GL2_IMPLEMENTATION` itself (and duplicating the whole
     * implementation, a real ODR violation at link time). This wrapper is the one place outside
     * NanoVgGl.cpp that needs to create/destroy a GL2 context; `NanoVgRenderer.cpp` calls it
     * instead of `nvgCreateGL2` directly.
     *
     * @return The new NanoVG context, or `nullptr` on failure (mirrors `nvgCreateGL2` itself).
     */
    NVGcontext* CreateNanoVgGL2Context();

    /** @brief Destroys a context created by CreateNanoVgGL2Context(). Safe to call with `nullptr`. */
    void DeleteNanoVgGL2Context(NVGcontext* ctx);
}
