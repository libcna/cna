#pragma once

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// A GPU query that counts the number of visible pixels.
    class OcclusionQuery : public GraphicsResource
    {
    public:
        explicit OcclusionQuery(GraphicsDevice& device);

        /// Gets whether the occlusion query result is available.
        [[nodiscard]] bool getIsCompleteProperty() const;

        /// Gets the number of visible pixels from the last completed query.
        [[nodiscard]] int getPixelCountProperty() const;

        void Begin();
        void End();

    private:
        bool isComplete_ = false;
        int pixelCount_  = 0;
    };
}
