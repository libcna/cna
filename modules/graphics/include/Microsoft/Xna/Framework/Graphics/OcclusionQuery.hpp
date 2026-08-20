// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace CNA::Internal::Renderers
{
    class IOcclusionQueryRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A GPU query that counts the number of visible pixels rendered between Begin and End. */
    class OcclusionQuery : public GraphicsResource
    {
    public:
        /**
         * @brief Creates an occlusion query for the specified graphics device.
         *
         * @param device Graphics device that will execute the query.
         */
        explicit OcclusionQuery(GraphicsDevice& device);

        /** @brief Destructor. */
        CNAEXT ~OcclusionQuery() override;

        /** @brief Returns the fully qualified CNA type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets whether the occlusion query result is available without blocking.
         * @return true if the query result can be read without stalling the CPU.
         */
        [[nodiscard]] bool getIsCompleteProperty() const;

        /**
         * @brief Gets the number of visible pixels from the last completed query.
         *
         * On OpenGL ES 3.0 this returns 0 (no visible samples) or 1 (any visible samples).
         *
         * @return Visible pixel count from the most recently completed query.
         */
        [[nodiscard]] int getPixelCountProperty() const;

        /** @brief Begins the occlusion query; all draw calls until End() are counted. */
        void Begin();

        /** @brief Ends the occlusion query and submits it to the GPU for evaluation. */
        void End();

        /** @brief Returns true while the native query renderer is still alive (CNA extension). */
        CNAEXT [[nodiscard]] bool HasRenderer() const { return renderer_ != nullptr; }

    protected:
        /**
         * @brief Releases the native query renderer before the base class marks this resource
         * disposed (plans/plan_sokol.md SOKOL-42).
         *
         * `GraphicsDevice::Dispose()` disposes tracked resources before tearing down the renderer
         * device/GL context; without this override the renderer_ object below would only be
         * destroyed whenever this OcclusionQuery's own C++ destructor happens to run, which may be
         * long after that teardown -- e.g. a raw `glDeleteQueries` call after `sg_shutdown()` and
         * SDL GL-context destruction on the Sokol renderer.
         *
         * @param disposing True when called from Dispose(); false when called from the finalizer.
         */
        void Dispose(bool disposing) override;

    private:
        std::unique_ptr<CNA::Internal::Renderers::IOcclusionQueryRenderer> renderer_;
    };
}
