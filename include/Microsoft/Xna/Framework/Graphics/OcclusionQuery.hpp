#pragma once

#include <memory>
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace CNA::Internal::Backends
{
    class IOcclusionQueryBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /// A GPU query that counts the number of visible pixels.
    class OcclusionQuery : public GraphicsResource
    {
    public:
        explicit OcclusionQuery(GraphicsDevice& device);
        ~OcclusionQuery() override;

        /// Gets whether the occlusion query result is available.
        [[nodiscard]] bool getIsCompleteProperty() const;

        /// Gets the number of visible pixels from the last completed query.
        /// On OpenGL ES 3.0 returns 0 (no samples) or 1 (any samples visible).
        [[nodiscard]] int getPixelCountProperty() const;

        void Begin();
        void End();

    private:
        std::unique_ptr<CNA::Internal::Backends::IOcclusionQueryBackend> backend_;
    };
}
