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
         * On OpenGL ES 3.0 and WebGL 2 this returns 0 (no visible samples) or 1 (any visible
         * samples): their only core occlusion target is the boolean `GL_ANY_SAMPLES_PASSED`.
         * Ask @ref isPixelCountPreciseEXT before dividing this by an area.
         *
         * @return Visible pixel count from the most recently completed query.
         */
        [[nodiscard]] int getPixelCountProperty() const;

        /**
         * @brief Gets whether @ref getPixelCountProperty is a real tally rather than a flag.
         *
         * True where the backend counts fragments the way XNA's own Direct3D 9 query does. False
         * where it can only answer "any" or "none" -- OpenGL ES 3.0 and WebGL 2, whose core query
         * target is boolean. A coverage ratio computed from a boolean count is 1/area, not a
         * fraction, so a game that needs one has to be able to ask which it is holding.
         *
         * @return true when the pixel count is a genuine per-fragment tally.
         */
        CNAEXT [[nodiscard]] bool isPixelCountPreciseEXT() const;

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
