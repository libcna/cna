// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace CNA::Internal::Backends
{
    class IOcclusionQueryBackend;
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

        /** @brief Destroys the occlusion query and releases GPU resources. */
        ~OcclusionQuery() override;

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

    private:
        std::unique_ptr<CNA::Internal::Backends::IOcclusionQueryBackend> backend_;
    };
}
