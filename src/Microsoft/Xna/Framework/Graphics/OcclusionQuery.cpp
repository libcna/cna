#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    OcclusionQuery::OcclusionQuery(GraphicsDevice& device)
        : GraphicsResource(&device)
    {
    }

    bool OcclusionQuery::getIsCompleteProperty() const { return isComplete_; }
    int OcclusionQuery::getPixelCountProperty() const { return pixelCount_; }

    void OcclusionQuery::Begin() { isComplete_ = false; }
    void OcclusionQuery::End()   { isComplete_ = true; }
}
