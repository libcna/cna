#pragma once
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::OpenGL1
{
    /**
     * @brief Real GPU occlusion queries for the OPENGL1 renderer (plans/plan_opengl1.md item 23).
     *
     * Backed by `ARB_occlusion_query`/core (>=1.5) -- `glGenQueries`/`glBeginQuery(GL_SAMPLES_
     * PASSED)`/`glEndQuery`/`glGetQueryObjectiv(GL_QUERY_RESULT_AVAILABLE)`/`glGetQueryObjectuiv
     * (GL_QUERY_RESULT)`, loaded via the platform GL resolver the same way
     * `TryLoadOpenGL1FramebufferObjectFunctions()` loads its own entry points. `GL_SAMPLES_PASSED`
     * (not the newer `GL_ANY_SAMPLES_PASSED`) means `PixelCount()` returns a real, exact visible-
     * sample count on a non-multisampled target -- unlike EasyGL's own GLES3-constrained
     * `GL_ANY_SAMPLES_PASSED` implementation (`IOcclusionQueryRenderer`'s own doc comment), which
     * can only report 0 or 1.
     *
     * No context-loss recovery: unlike `Texture2D`/`TextureCube`/`RenderTarget2D`, an occlusion
     * query has no persistent content a CPU shadow or GPU-side rebuild could meaningfully restore
     * -- it is an ephemeral, single-use GPU command-stream measurement, always
     * created/Begin/End/read within the scope of the game that owns it. A query in flight at the
     * moment of a real/simulated context loss simply loses its result, matching how any other
     * in-flight, unsynchronized GPU command would behave.
     */
    class OpenGL1OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        OpenGL1OcclusionQueryRenderer();
        ~OpenGL1OcclusionQueryRenderer() override;

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int PixelCount() const override;

    private:
        unsigned int id_ = 0;
    };

    /**
     * @brief Loads the ARB_occlusion_query/core-1.5 entry points via the platform GL resolver.
     *
     * Must be called once after a GL context is current, gated on
     * OpenGL1Capabilities::occlusionQuery. Idempotent.
     *
     * @return True only if every required entry point resolved to a non-null address.
     */
    bool TryLoadOpenGL1OcclusionQueryFunctions();
}
