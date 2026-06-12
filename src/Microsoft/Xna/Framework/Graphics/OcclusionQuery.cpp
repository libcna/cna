// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    OcclusionQuery::OcclusionQuery(GraphicsDevice& device)
        : GraphicsResource(&device)
        , backend_(device.GetBackend().CreateOcclusionQuery())
    {
    }

    OcclusionQuery::~OcclusionQuery() = default;

    bool OcclusionQuery::getIsCompleteProperty() const
    {
        if (backend_) return backend_->IsComplete();
        return false;
    }

    int OcclusionQuery::getPixelCountProperty() const
    {
        if (backend_) return backend_->PixelCount();
        return 0;
    }

    void OcclusionQuery::Begin()
    {
        if (backend_) backend_->Begin();
    }

    void OcclusionQuery::End()
    {
        if (backend_) backend_->End();
    }
}
