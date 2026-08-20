#pragma once
#include <vector>

namespace CNA::Internal::Renderers::OpenGL1
{
    /**
     * @brief Interface for an OPENGL1 GPU resource that can be recreated after a simulated or
     * real GL context loss.
     *
     * Mirrors the concept easy-gl's own RecoverableResource/ResourceRegistry establish, but
     * implemented independently and with no dependency on that library or its `metagl` context-
     * event plumbing -- plans/plan_opengl1.md's own scope rule is that OPENGL1 MUST NOT depend on
     * EasyGL. Every method here is called synchronously by OpenGL1ResourceRegistry from
     * OpenGL1Renderer::DebugSimulateContextLoss()/DebugRestoreContext() (desktop GL has no
     * genuine asynchronous context-lost/restored event the way WebGL does -- both those methods
     * perform one atomic destroy+recreate cycle).
     */
    class IOpenGL1Recoverable
    {
    public:
        virtual ~IOpenGL1Recoverable() = default;

        /**
         * @brief Drops this resource's GL handle(s) without issuing any gl* calls.
         *
         * Called while the old GL context is still current but about to be destroyed. The
         * resource must retain whatever CPU-side data it needs to recreate itself afterward.
         */
        virtual void ReleaseGLHandleOnly() = 0;

        /**
         * @brief Recreates the GL resource against the newly-current context, from retained
         * CPU-side data.
         *
         * Called after the new context is current and any extension entry points it needs have
         * been reloaded.
         */
        virtual void RecreateGLResource() = 0;
    };

    /**
     * @brief Tracks every IOpenGL1Recoverable resource owned by one OPENGL1 renderer instance.
     *
     * One instance lives on OpenGL1Renderer; resources register themselves at
     * construction (when context recovery is enabled -- see
     * IGraphicsRenderer::SetContextRecoveryEnabled()) and unregister at destruction.
     */
    class OpenGL1ResourceRegistry
    {
    public:
        /** @brief Registers a resource. Caller retains ownership; a null pointer is ignored. */
        void Add(IOpenGL1Recoverable* resource);
        /** @brief Unregisters a resource. No-op if not currently registered. */
        void Remove(IOpenGL1Recoverable* resource);
        /** @brief Calls ReleaseGLHandleOnly() on every registered resource. */
        void NotifyContextLost();
        /** @brief Calls RecreateGLResource() on every registered resource. */
        void NotifyContextRestored();

    private:
        std::vector<IOpenGL1Recoverable*> resources_;
    };
}
