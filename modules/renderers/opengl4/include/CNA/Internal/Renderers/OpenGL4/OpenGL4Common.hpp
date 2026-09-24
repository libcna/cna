// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_opengl4_modern_graphics.md GL4-0011: the small set of types and helpers the OpenGL4
// renderer core and its resource classes share -- the one record of which render target is bound,
// the render-target row-order rule, depth-buffer precision and the GL error-queue discipline.
// Split out of OpenGL4Renderer.hpp so the resource translation units do not depend on the
// renderer class itself.

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::OpenGL4
{
    class OpenGL4RenderTargetRenderer;
    class OpenGL4RenderTargetCubeRenderer;

    /**
     * @brief One slot of a multi-render-target set: a RenderTarget2D, or one face of a cube.
     *
     * Identity only -- the slot owns nothing. A target that dies while bound clears its own slot
     * (see @ref OpenGL4BoundTarget).
     */
    struct OpenGL4MrtBinding
    {
        /** @brief Bound 2D target when this slot names a RenderTarget2D. */
        OpenGL4RenderTargetRenderer* rt2D = nullptr;
        /** @brief Bound cube target when this slot names one of its faces. */
        OpenGL4RenderTargetCubeRenderer* cube = nullptr;
        /** @brief Cube-face ordinal used when `cube` is non-null. */
        int cubeFace = 0;

        /** @brief Returns whether this slot names no live attachment. */
        [[nodiscard]] bool IsEmpty() const { return rt2D == nullptr && cube == nullptr; }
    };

    /**
     * @brief The one record of which OpenGL4 render target is currently bound.
     *
     * Held in its own heap allocation, shared between the renderer and every render target it
     * created (the render targets keep a `weak_ptr`). A target that is destroyed while bound clears
     * its own slots here directly, without calling back into a renderer that may already be gone;
     * a renderer that dies first takes the record with it and leaves every surviving target's
     * `weak_ptr` expired. This is the design EasyGL adopted in REMED-GFX-168, because
     * `RenderTarget2D`'s destructor never routes through `Dispose()` and a scoped target leaving
     * scope while bound otherwise leaves a dangling pointer the next target switch dispatches
     * through.
     */
    struct OpenGL4BoundTarget
    {
        /** @brief Currently bound single `RenderTarget2D` renderer, or nullptr. */
        IRenderTargetRenderer* rt2D = nullptr;
        /** @brief Currently bound `RenderTargetCube` renderer, or nullptr. */
        IRenderTargetCubeRenderer* cube = nullptr;
        /** @brief Currently bound ordered multi-target set; entries beyond `mrtCount` are unused. */
        std::array<OpenGL4MrtBinding, 4> mrt = {};
        /** @brief Number of live slots in `mrt`; 0 when no multi-target set is bound. */
        int mrtCount = 0;
        /** @brief Native draw FBO of the active MRT set, used to restore it after direct readback. */
        unsigned int mrtFramebuffer = 0;
        /** @brief Raw XNA `DepthFormat` ordinal of the active destination. */
        int depthFormat = 3;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int width = 0;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int height = 0;
    };

    /**
     * @brief Depth precision of a `DepthFormat` ordinal (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3).
     *
     * @param depthFormat Raw `DepthFormat` ordinal.
     * @return Bits of depth precision; 24 for `None`, where a bias cannot be observed anyway.
     */
    [[nodiscard]] constexpr int OpenGL4DepthBufferBits(const int depthFormat)
    {
        return depthFormat == 1 ? 16 : 24;
    }

    /**
     * @brief REMED-GFX-147: whether sampling @p texture needs a vertical coordinate correction.
     *
     * An OpenGL framebuffer's origin is bottom-left, so a render target's colour texture stores the
     * logical image bottom-up: texel row v=0 is the LAST logical row. Ordinary textures are
     * uploaded top-down and are unaffected. Every OpenGL4 sampling path -- SpriteBatch's sprite
     * UVs, the stock 3D effects' `uRtFlipV`, a custom effect -- asks this one question.
     *
     * @param texture Sampled source, or nullptr.
     * @return True when @p texture is a render target's colour attachment.
     */
    [[nodiscard]] inline bool SampledRowOrderIsBottomUp(const ITextureRenderer* texture)
    {
        return dynamic_cast<const IRenderTargetRenderer*>(texture) != nullptr;
    }

    /**
     * @brief Drains GL's error queue before an operation whose success is judged by it.
     *
     * glTexSubImage and friends return nothing, so the error queue is the only completion signal.
     * Draining first keeps a stale error left by unrelated code from being read as this call failing.
     */
    inline void DrainGlErrors()
    {
        for (int i = 0; i < 16; ++i)
            if (glGetError() == GL_NO_ERROR) return;
    }

    /**
     * @brief Reads whether the GL operation issued since the last @ref DrainGlErrors succeeded.
     *
     * @return True when GL reported no error.
     */
    [[nodiscard]] inline bool GlOperationSucceeded()
    {
        return glGetError() == GL_NO_ERROR;
    }

    /**
     * @brief Turns the scissor test off for one scope and restores it afterwards.
     *
     * glBlitFramebuffer honours the scissor test, but an XNA multisample resolve covers the whole
     * surface whatever RasterizerState.ScissorTestEnable says; every resolve blit is made inside one.
     */
    class ScopedScissorTestDisabled
    {
    public:
        ScopedScissorTestDisabled() : wasEnabled_(glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE)
        {
            if (wasEnabled_) glDisable(GL_SCISSOR_TEST);
        }
        ~ScopedScissorTestDisabled()
        {
            if (wasEnabled_) glEnable(GL_SCISSOR_TEST);
        }
        ScopedScissorTestDisabled(const ScopedScissorTestDisabled&) = delete;
        ScopedScissorTestDisabled& operator=(const ScopedScissorTestDisabled&) = delete;

    private:
        bool wasEnabled_;
    };

    /**
     * @brief Rewrites a GLSL ES 3.00 shader into the desktop core dialect this renderer compiles.
     *
     * CNA's stock programs and the CNAEXT engine layer author GLSL ES 3.00. Its body syntax is
     * shared with desktop GLSL 3.30+ core; only the version line and the default-precision line that
     * follows it differ. A source that does not start with `#version 300 es` is returned unchanged,
     * so desktop-authored GLSL (a `GlslDesktop` shader-package variant) passes through as written.
     *
     * @param source GLSL source.
     * @return The source with `#version 300 es` replaced by `#version 410 core` and its immediately
     *         following `precision ... float;` statement blanked, its line kept so compiler
     *         diagnostics keep the author's line numbers.
     */
    [[nodiscard]] std::string AdaptGlslEs300ForDesktopCore(const std::string& source);
}
