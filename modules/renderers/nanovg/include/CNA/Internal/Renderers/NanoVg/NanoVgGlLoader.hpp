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

    /**
     * @brief One XNA `SamplerState`'s filter/address pair, resolved to the two independent
     * decisions a GL texture object can actually store.
     *
     * XNA's `TextureFilter` names a minification AND a magnification component (plus a mip
     * component, which is inert here -- `nvgCreateImageRGBA` allocates exactly one level, so no
     * mip chain exists to select from); real desktop GL stores those two components separately
     * (`GL_TEXTURE_MIN_FILTER`/`GL_TEXTURE_MAG_FILTER`), so both are carried here rather than a
     * single "point vs linear" flag.
     */
    struct NanoVgImageSamplerState
    {
        /** @brief Whether minification samples the nearest texel instead of interpolating. */
        bool minifyPoint = false;
        /** @brief Whether magnification samples the nearest texel instead of interpolating. */
        bool magnifyPoint = false;
        /** @brief Raw `TextureAddressMode` ordinal for U (0=Wrap, 1=Clamp, 2=Mirror). */
        int addressU = 1;
        /** @brief Raw `TextureAddressMode` ordinal for V (0=Wrap, 1=Clamp, 2=Mirror). */
        int addressV = 1;
    };

    /**
     * @brief Writes @p sampler onto the GL texture object behind a NanoVG image handle.
     *
     * NanoVG's own image flags (`NVG_IMAGE_NEAREST`, `NVG_IMAGE_REPEATX`/`Y`) are applied ONCE, at
     * `nvgCreateImageRGBA` time, and are never re-applied per draw -- `glnvg__setUniforms` only
     * binds the texture. XNA's `SamplerState`, by contrast, is chosen per `SpriteBatch.Begin()`,
     * independent of which texture is drawn, so the creation-time flags cannot express it. This
     * writes the four `glTexParameteri` values directly instead, which is both exact (GL's own
     * filter/wrap enums are what NanoVG's flags reduce to anyway) and free of the pixel-storage
     * duplication a second image handle per sampler combination would cost. It also reaches
     * `GL_MIRRORED_REPEAT`, which NanoVG's flag set has no name for at all.
     *
     * Must be called with the owning context current, and between `nvgBeginFrame` and
     * `nvgEndFrame`: NanoVG records draw calls during that window and binds textures only when
     * `nvgEndFrame` flushes them, so parameters written here are the ones the flush actually
     * samples with. Its internal `boundTexture` cache is left consistent -- this restores the
     * binding to 0, which is exactly the value `glnvg__renderFlush` both leaves behind and resets
     * its cache to.
     *
     * @param ctx The owning NanoVG context.
     * @param image The NanoVG image handle whose GL texture should be reconfigured.
     * @param sampler The filter/address state to write.
     */
    void ApplyNanoVgImageSamplerState(NVGcontext* ctx, int image,
                                      const NanoVgImageSamplerState& sampler);
}
